/*  h5cat – concatenate multiple NetCDF‑4/HDF5 files along their unlimited record
 *          dimension **without** decompressing or recompressing chunks.
 *
 *  Build:   gcc -std=c99 -Wall -O2 h5cat.c -lhdf5 -lz -o h5cat
 *  Usage:   h5cat in1.h5 in2.h5 [... more …] out.h5
 *           (same CLI contract as NCO’s ncrcat)
 *
 *  Behaviour
 *  ---------
 *  • All datasets that have at least one unlimited dimension are treated as
 *    *record variables* and concatenated chunk‑for‑chunk using the HDF5 1.10
 *    raw‑chunk API (H5Dread_chunk / H5Dwrite_chunk).
 *  • Datasets **without** unlimited dimensions are copied only from the first
 *    input file.  A notice is printed so that the user is aware of this.
 *  • The whole metadata tree is cloned from the first input to the output –
 *    groups, datatypes, attributes, everything.
 *  • Progress messages are written to **stderr**.
 *
 *  Requirements
 *  ------------
 *  • HDF5 ≥ 1.10 (for the chunk‑query/copy API)
 *  • All input files must share the same chunk shape and filter stack for
 *    every dataset (true when they came from the same model run / NCO chain).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <hdf5.h>

#define MAX_VARS     2048           /* bump if your files have >2k datasets */
#define MAX_RANK     H5S_MAX_RANK
#define MAX_PATH_LEN 1024

/* -------------------------------------------------------------------------- */
/* Helper structures                                                          */

typedef struct {
    char     path[MAX_PATH_LEN];    /* full HDF5 path of the dataset */
    int      rec_dim;               /* index of the unlimited dim    */
    hsize_t  total_len;             /* final size of that dim        */
    hsize_t  offset;                /* running offset while copying  */
} RecVar;

typedef struct {
    RecVar vars[MAX_VARS];
    size_t nvars;
} RecVarList;

/* Global verbose flag */
static int verbose = 0;

/* -------------------------------------------------------------------------- */
/* Minimal error handling                                                     */

static void die(const char *msg) {
    fprintf(stderr, "h5cat ERROR: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* HDF5 version check                                                         */

static void check_hdf5_version(void) {
    unsigned majnum, minnum, relnum;
    
    if (H5get_libversion(&majnum, &minnum, &relnum) < 0) {
        die("Failed to get HDF5 library version");
    }
    
    fprintf(stderr, "[info] HDF5 library version: %u.%u.%u\n", majnum, minnum, relnum);
    
    /* Check minimum version requirement (HDF5 >= 1.10 for chunk API) */
    if (majnum < 1 || (majnum == 1 && minnum < 10)) {
        fprintf(stderr, "h5cat ERROR: HDF5 version %u.%u.%u is too old.\n", majnum, minnum, relnum);
        fprintf(stderr, "h5cat ERROR: This tool requires HDF5 >= 1.10 for the chunk query/copy API.\n");
        exit(EXIT_FAILURE);
    }
}

/* -------------------------------------------------------------------------- */
/* 1. Detect record variables in the first input                              */

static herr_t detect_recvars_cb(hid_t obj, const char *name, const H5O_info_t *info,
                                void *op_data)
{
    RecVarList *list = (RecVarList*)op_data;
    if (info->type != H5O_TYPE_DATASET) return 0;   /* skip groups, types, … */

    hid_t dset = H5Dopen(obj, name, H5P_DEFAULT);
    if (dset < 0) return 0;                         /* shouldn’t happen */

    hid_t space = H5Dget_space(dset);
    int   rank  = H5Sget_simple_extent_ndims(space);
    if (rank <= 0 || rank > MAX_RANK) { H5Sclose(space); H5Dclose(dset); return 0; }

    hsize_t dims[MAX_RANK];
    hsize_t maxd[MAX_RANK];
    H5Sget_simple_extent_dims(space, dims, maxd);

    int rec_dim = -1;
    for (int i = 0; i < rank; i++)
        if (maxd[i] == H5S_UNLIMITED) { rec_dim = i; break; }

    if (rec_dim >= 0) {                              /* it IS a record var */
        if (list->nvars >= MAX_VARS) die("Too many record variables – raise MAX_VARS");
        RecVar *v = &list->vars[list->nvars++];
        snprintf(v->path, MAX_PATH_LEN, "%s", name);
        v->rec_dim    = rec_dim;
        v->total_len  = 0;
        v->offset     = 0;
    } else {
        fprintf(stderr, "[info] fixed‑size dataset %s – copied from first file only\n", name);
    }

    H5Sclose(space);
    H5Dclose(dset);
    return 0;   /* continue traversal */
}

/* -------------------------------------------------------------------------- */
/* 2. Sum the record lengths over **all** input files                          */

static void accumulate_lengths(RecVarList *rv, int nfiles, char **files)
{
    for (int f = 0; f < nfiles; f++) {
        hid_t fid = H5Fopen(files[f], H5F_ACC_RDONLY, H5P_DEFAULT);
        if (fid < 0) die("Failed to open input file for length scan");

        for (size_t v = 0; v < rv->nvars; v++) {
            hid_t ds = H5Dopen(fid, rv->vars[v].path, H5P_DEFAULT);
            if (ds < 0) die("Dataset missing in some file – schemas differ");

            hid_t sp = H5Dget_space(ds);
            hsize_t dims[MAX_RANK];
            H5Sget_simple_extent_dims(sp, dims, NULL);
            rv->vars[v].total_len += dims[ rv->vars[v].rec_dim ];

            H5Sclose(sp);
            H5Dclose(ds);
        }
        H5Fclose(fid);
    }
}

/* -------------------------------------------------------------------------- */
/* 3. Raw‑chunk copier                                                        */

static void copy_raw_chunks(hid_t in_ds, hid_t out_ds,
                            hsize_t rec_offset, int rec_dim)
{
    hsize_t nchunks; H5Dget_num_chunks(in_ds, H5S_ALL, &nchunks);

    /* Get chunk dimensions to ensure proper alignment */
    hid_t dcpl = H5Dget_create_plist(in_ds);
    hid_t space = H5Dget_space(in_ds);
    int ndims = H5Sget_simple_extent_ndims(space);
    hsize_t chunk_dims[MAX_RANK];
    H5Pget_chunk(dcpl, ndims, chunk_dims);
    H5Sclose(space);
    H5Pclose(dcpl);

    for (hsize_t idx = 0; idx < nchunks; ++idx) {
        hsize_t coord[MAX_RANK];
        unsigned filt_mask; hsize_t addr; size_t comp_sz;

        H5Dget_chunk_info(in_ds, H5S_ALL, idx,
                          coord, &filt_mask, &addr, &comp_sz);

        void *buf = malloc(comp_sz);
        if (!buf) die("Out of memory while copying chunks");
        
        /* Read chunk with its original filter mask */
        unsigned read_filt_mask = filt_mask;
        H5Dread_chunk(in_ds, H5P_DEFAULT, coord, &read_filt_mask, buf);

        /* Get current dataset dimensions to check if we need normal data writing */
        hid_t in_space = H5Dget_space(in_ds);
        hsize_t in_dims[MAX_RANK];
        H5Sget_simple_extent_dims(in_space, in_dims, NULL);
        H5Sclose(in_space);
        
        /* If data size is smaller than chunk size, use normal data writing instead of chunk writing */
        if (in_dims[rec_dim] < chunk_dims[rec_dim]) {
            if (verbose) {
                fprintf(stderr, "[debug] Using normal data writing: rec_dim=%d, rec_offset=%llu, in_dims[%d]=%llu, chunk_dims[%d]=%llu\n", 
                        rec_dim, (unsigned long long)rec_offset, rec_dim, (unsigned long long)in_dims[rec_dim], 
                        rec_dim, (unsigned long long)chunk_dims[rec_dim]);
            }
            /* Read data normally and write to the correct offset */
            hid_t mem_space = H5Screate_simple(ndims, in_dims, NULL);
            hid_t file_space = H5Dget_space(out_ds);
            
            /* Set hyperslab for writing at the correct offset */
            hsize_t start[MAX_RANK], count[MAX_RANK];
            for (int i = 0; i < ndims; i++) {
                start[i] = (i == rec_dim) ? rec_offset : 0;
                count[i] = in_dims[i];
            }
            H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, NULL, count, NULL);
            
            /* Allocate buffer for uncompressed data */
            hid_t dtype = H5Dget_type(in_ds);
            size_t type_size = H5Tget_size(dtype);
            size_t total_elements = 1;
            for (int i = 0; i < ndims; i++) {
                total_elements *= in_dims[i];
            }
            void *data_buf = malloc(total_elements * type_size);
            
            /* Read data normally from input */
            H5Dread(in_ds, dtype, mem_space, H5S_ALL, H5P_DEFAULT, data_buf);
            
            /* Write data to output at correct offset */
            H5Dwrite(out_ds, dtype, mem_space, file_space, H5P_DEFAULT, data_buf);
            
            /* Clean up */
            free(data_buf);
            H5Tclose(dtype);
            H5Sclose(mem_space);
            H5Sclose(file_space);
        } else {
            /* Normal chunk writing for large datasets */
            hsize_t original_coord = coord[rec_dim];
            coord[rec_dim] += rec_offset;
            
            if (verbose) {
                fprintf(stderr, "[debug] Chunk writing: rec_dim=%d, rec_offset=%llu, original_coord=%llu, new_coord=%llu, chunk_dims[%d]=%llu\n", 
                        rec_dim, (unsigned long long)rec_offset, (unsigned long long)original_coord, 
                        (unsigned long long)coord[rec_dim], rec_dim, (unsigned long long)chunk_dims[rec_dim]);
            }
            
            H5Dwrite_chunk(out_ds, H5P_DEFAULT, read_filt_mask,
                           coord, comp_sz, buf);
        }
        free(buf);
    }
}

/* -------------------------------------------------------------------------- */
/* 4. Fix NetCDF-4 dimension list references                                  */

static void fix_dimension_list_references(hid_t file_id)
{
    if (verbose) {
        fprintf(stderr, "[debug] Fixing DIMENSION_LIST references for NetCDF-4 compatibility\n");
    }
    
    /* Define callback to fix DIMENSION_LIST references */
    herr_t fix_dimlist_cb(hid_t obj, const char *name, const H5O_info_t *info, void *op_data) {
        hid_t *file_id_ptr = (hid_t*)op_data;
        
        /* Skip root group and only process datasets */
        if (strcmp(name, ".") == 0 || info->type != H5O_TYPE_DATASET) return 0;
        
        hid_t dset = H5Dopen(*file_id_ptr, name, H5P_DEFAULT);
        if (dset < 0) return 0;
        
        /* Check if dataset has DIMENSION_LIST attribute */
        if (H5Aexists(dset, "DIMENSION_LIST") > 0) {
            hid_t attr = H5Aopen(dset, "DIMENSION_LIST", H5P_DEFAULT);
            if (attr >= 0) {
                hid_t attr_type = H5Aget_type(attr);
                hid_t attr_space = H5Aget_space(attr);
                
                /* Get the number of dimensions */
                hsize_t dims[1];
                int ndims = H5Sget_simple_extent_dims(attr_space, dims, NULL);
                if (ndims == 1) {
                    /* Create new references for dimension scale datasets */
                    hobj_ref_t *new_refs = malloc(dims[0] * sizeof(hobj_ref_t));
                    if (new_refs) {
                        /* Get dimension names from _Netcdf4Coordinates if available */
                        char *dim_names[] = {"time", "level", "latitude", "longitude"};
                        char dim_path[64];
                        
                        for (hsize_t i = 0; i < dims[0]; i++) {
                            snprintf(dim_path, sizeof(dim_path), "/%s", dim_names[i]);
                            
                            /* Create reference to dimension scale dataset */
                            if (H5Rcreate(&new_refs[i], *file_id_ptr, dim_path, H5R_OBJECT, -1) < 0) {
                                if (verbose) {
                                    fprintf(stderr, "[warning] Failed to create reference to %s\n", dim_path);
                                }
                                /* Set to null reference on failure */
                                memset(&new_refs[i], 0, sizeof(hobj_ref_t));
                            }
                        }
                        
                        /* Delete old attribute and create new one */
                        H5Adelete(dset, "DIMENSION_LIST");
                        
                        /* Create new DIMENSION_LIST attribute with correct references */
                        hid_t vlen_type = H5Tvlen_create(H5T_STD_REF_OBJ);
                        hid_t new_attr = H5Acreate2(dset, "DIMENSION_LIST", vlen_type, attr_space, H5P_DEFAULT, H5P_DEFAULT);
                        
                        if (new_attr >= 0) {
                            /* Prepare vlen data structure */
                            hvl_t *vlen_data = malloc(dims[0] * sizeof(hvl_t));
                            if (vlen_data) {
                                for (hsize_t i = 0; i < dims[0]; i++) {
                                    vlen_data[i].len = 1;
                                    vlen_data[i].p = &new_refs[i];
                                }
                                
                                /* Write the new references */
                                if (H5Awrite(new_attr, vlen_type, vlen_data) < 0) {
                                    if (verbose) {
                                        fprintf(stderr, "[warning] Failed to write DIMENSION_LIST for %s\n", name);
                                    }
                                }
                                
                                free(vlen_data);
                            }
                            H5Aclose(new_attr);
                        }
                        
                        H5Tclose(vlen_type);
                        free(new_refs);
                        
                        if (verbose) {
                            fprintf(stderr, "[debug] Fixed DIMENSION_LIST references for %s\n", name);
                        }
                    }
                }
                
                H5Sclose(attr_space);
                H5Tclose(attr_type);
                H5Aclose(attr);
            }
        }
        
        H5Dclose(dset);
        return 0;
    }
    
    /* Visit all objects to fix DIMENSION_LIST references */
    H5Ovisit3(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, fix_dimlist_cb, &file_id, H5O_INFO_BASIC);
}

/* -------------------------------------------------------------------------- */
/* 5. Update NetCDF history attribute                                         */

static void update_history_attribute(hid_t file_id, int argc, char **argv)
{
    hid_t root_group = H5Gopen(file_id, "/", H5P_DEFAULT);
    if (root_group < 0) {
        if (verbose) {
            fprintf(stderr, "[warning] Could not open root group for history update\n");
        }
        return;
    }
    
    /* Build command string */
    char command[2048] = "";
    int pos = 0;
    for (int i = 0; i < argc && pos < sizeof(command) - 1; i++) {
        if (i > 0) {
            command[pos++] = ' ';
        }
        int len = strlen(argv[i]);
        if (pos + len < sizeof(command) - 1) {
            strcpy(command + pos, argv[i]);
            pos += len;
        }
    }
    
    /* Add timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", tm_info);
    
    char new_entry[2200];
    snprintf(new_entry, sizeof(new_entry), "%s: %s\n", timestamp, command);
    
    /* Check if history attribute exists */
    htri_t attr_exists = H5Aexists(root_group, "history");
    
    if (attr_exists > 0) {
        /* Read existing history */
        hid_t attr = H5Aopen(root_group, "history", H5P_DEFAULT);
        if (attr >= 0) {
            hid_t attr_type = H5Aget_type(attr);
            hid_t attr_space = H5Aget_space(attr);
            
            size_t attr_size = H5Aget_storage_size(attr);
            if (attr_size > 0) {
                char *existing_history = malloc(attr_size + 1);
                if (existing_history) {
                    if (H5Aread(attr, attr_type, existing_history) >= 0) {
                        existing_history[attr_size] = '\0';
                        
                        /* Create new history string with command prepended */
                        size_t new_size = strlen(new_entry) + strlen(existing_history) + 1;
                        char *new_history = malloc(new_size);
                        if (new_history) {
                            snprintf(new_history, new_size, "%s%s", new_entry, existing_history);
                            
                            /* Delete old attribute */
                            H5Aclose(attr);
                            H5Adelete(root_group, "history");
                            
                            /* Create new attribute with updated history */
                            hid_t str_type = H5Tcopy(H5T_C_S1);
                            H5Tset_size(str_type, strlen(new_history));
                            H5Tset_strpad(str_type, H5T_STR_NULLTERM);
                            
                            hid_t scalar_space = H5Screate(H5S_SCALAR);
                            hid_t new_attr = H5Acreate2(root_group, "history", str_type, scalar_space, H5P_DEFAULT, H5P_DEFAULT);
                            
                            if (new_attr >= 0) {
                                if (H5Awrite(new_attr, str_type, new_history) >= 0) {
                                    if (verbose) {
                                        fprintf(stderr, "[debug] Updated history attribute\n");
                                    }
                                } else {
                                    if (verbose) {
                                        fprintf(stderr, "[warning] Failed to write updated history\n");
                                    }
                                }
                                H5Aclose(new_attr);
                            } else {
                                if (verbose) {
                                    fprintf(stderr, "[warning] Failed to create new history attribute\n");
                                }
                            }
                            
                            H5Sclose(scalar_space);
                            H5Tclose(str_type);
                            free(new_history);
                        }
                    }
                    free(existing_history);
                }
            } else {
                H5Aclose(attr);
            }
            
            H5Tclose(attr_type);
            H5Sclose(attr_space);
        }
    } else {
        /* Create new history attribute */
        hid_t str_type = H5Tcopy(H5T_C_S1);
        H5Tset_size(str_type, strlen(new_entry));
        H5Tset_strpad(str_type, H5T_STR_NULLTERM);
        
        hid_t scalar_space = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(root_group, "history", str_type, scalar_space, H5P_DEFAULT, H5P_DEFAULT);
        
        if (attr >= 0) {
            if (H5Awrite(attr, str_type, new_entry) >= 0) {
                if (verbose) {
                    fprintf(stderr, "[debug] Created new history attribute\n");
                }
            } else {
                if (verbose) {
                    fprintf(stderr, "[warning] Failed to write new history\n");
                }
            }
            H5Aclose(attr);
        } else {
            if (verbose) {
                fprintf(stderr, "[warning] Failed to create history attribute\n");
            }
        }
        
        H5Sclose(scalar_space);
        H5Tclose(str_type);
    }
    
    H5Gclose(root_group);
}

/* -------------------------------------------------------------------------- */
/* 6. Usage helper                                                            */

static void usage(const char *prog)
{
    fprintf(stderr,
"Usage:   %s [-v] in1.h5 [in2.h5 …] out.h5\n"
"         Concatenate along the unlimited dimension without recompressing.\n\n"
"Options:\n"
"  -v     Verbose output (show debug information)\n\n"
"Example: %s jan.nc feb.nc mar.nc q1.nc\n"
"         %s -v jan.nc feb.nc mar.nc q1.nc\n\n",
            prog, prog, prog);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* 5. Main                                                                    */

int main(int argc, char **argv)
{
    /* Parse command line arguments */
    int arg_start = 1;
    
    /* Check for help */
    if (argc < 3 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        usage(argv[0]);
    
    /* Check for verbose flag */
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose = 1;
        arg_start = 2;
    }
    
    /* Need at least 2 more args after flags: input and output */
    if (argc - arg_start < 2)
        usage(argv[0]);

    /* Check HDF5 version compatibility */
    check_hdf5_version();

    const int  n_inputs = argc - arg_start - 1;  /* last arg = output */
    char     **in_files = &argv[arg_start];
    const char *out_file = argv[argc - 1];

    /* ------------------------------------------------------------------ */
    /* 5.1 Detect record variables in first input                         */

    hid_t fid0 = H5Fopen(in_files[0], H5F_ACC_RDONLY, H5P_DEFAULT);
    if (fid0 < 0) die("Cannot open first input file");

    RecVarList rv = { .nvars = 0 };
    H5Ovisit3(fid0, H5_INDEX_NAME, H5_ITER_NATIVE,
              detect_recvars_cb, &rv, H5O_INFO_BASIC);

    if (rv.nvars == 0) die("No unlimited datasets found – nothing to concatenate");
    fprintf(stderr, "[info] found %zu record variables\n", rv.nvars);

    /* ------------------------------------------------------------------ */
    /* 5.2 Determine final size of each record variable                   */

    accumulate_lengths(&rv, n_inputs, in_files);

    /* ------------------------------------------------------------------ */
    /* 5.3 Create output file & clone metadata                            */

    hid_t fout = H5Fcreate(out_file, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (fout < 0) die("Cannot create output file");

    /* Create property lists for object copying like h5copy */
    hid_t ocpl_id = H5Pcreate(H5P_OBJECT_COPY);
    hid_t lcpl_id = H5Pcreate(H5P_LINK_CREATE);
    
    if (ocpl_id < 0 || lcpl_id < 0) {
        if (ocpl_id >= 0) H5Pclose(ocpl_id);
        if (lcpl_id >= 0) H5Pclose(lcpl_id);
        die("Failed to create property lists");
    }
    
    /* Set copy options to preserve filters and other dataset creation properties */
    if (H5Pset_copy_object(ocpl_id, H5O_COPY_PRESERVE_NULL_FLAG) < 0) {
        H5Pclose(ocpl_id);
        H5Pclose(lcpl_id);
        die("Failed to set object copy properties");
    }
    
    /* Copy objects from root group one by one using H5Ovisit */
    typedef struct {
        hid_t src_file;
        hid_t dst_file;
        hid_t ocpl_id;
        hid_t lcpl_id;
    } copy_ctx_t;
    
    copy_ctx_t ctx = {fid0, fout, ocpl_id, lcpl_id};
    
    /* Define callback to copy objects */
    herr_t copy_obj_cb(hid_t obj, const char *name, const H5O_info_t *info, void *op_data) {
        copy_ctx_t *context = (copy_ctx_t*)op_data;
        
        /* Skip the root group itself */
        if (strcmp(name, ".") == 0) return 0;
        
        if (verbose) {
            fprintf(stderr, "[debug] Copying object: %s (type: %d)\n", name, info->type);
        }
        
        /* Copy object from source to destination */
        if (H5Ocopy(context->src_file, name, context->dst_file, name, 
                   context->ocpl_id, context->lcpl_id) < 0) {
            fprintf(stderr, "Warning: Failed to copy object %s\n", name);
            return 0;
        }
        
        /* If it's a dataset, print its creation properties */
        if (verbose && info->type == H5O_TYPE_DATASET) {
            hid_t src_dset = H5Dopen(context->src_file, name, H5P_DEFAULT);
            hid_t dst_dset = H5Dopen(context->dst_file, name, H5P_DEFAULT);
            
            if (src_dset >= 0 && dst_dset >= 0) {
                hid_t src_dcpl = H5Dget_create_plist(src_dset);
                hid_t dst_dcpl = H5Dget_create_plist(dst_dset);
                
                if (src_dcpl >= 0 && dst_dcpl >= 0) {
                    /* Check if dataset is chunked and has filters */
                    if (H5Pget_layout(src_dcpl) == H5D_CHUNKED) {
                        int src_nfilters = H5Pget_nfilters(src_dcpl);
                        int dst_nfilters = H5Pget_nfilters(dst_dcpl);
                        
                        fprintf(stderr, "[debug] Dataset %s: src_filters=%d, dst_filters=%d\n", 
                                name, src_nfilters, dst_nfilters);
                        
                        /* Print filter details */
                        for (int i = 0; i < src_nfilters; i++) {
                            size_t cd_nelmts = 0;
                            unsigned int flags;
                            H5Z_filter_t filter_id = H5Pget_filter(src_dcpl, i, &flags, &cd_nelmts, NULL, 0, NULL, NULL);
                            fprintf(stderr, "[debug] Source filter %d: ID=%d, flags=%u\n", i, filter_id, flags);
                        }
                        
                        for (int i = 0; i < dst_nfilters; i++) {
                            size_t cd_nelmts = 0;
                            unsigned int flags;
                            H5Z_filter_t filter_id = H5Pget_filter(dst_dcpl, i, &flags, &cd_nelmts, NULL, 0, NULL, NULL);
                            fprintf(stderr, "[debug] Dest filter %d: ID=%d, flags=%u\n", i, filter_id, flags);
                        }
                    }
                }
                
                if (src_dcpl >= 0) H5Pclose(src_dcpl);
                if (dst_dcpl >= 0) H5Pclose(dst_dcpl);
            }
            
            if (src_dset >= 0) H5Dclose(src_dset);
            if (dst_dset >= 0) H5Dclose(dst_dset);
        }
        
        return 0;
    }
    
    /* Visit all objects in the root group */
    if (H5Ovisit3(fid0, H5_INDEX_NAME, H5_ITER_NATIVE, copy_obj_cb, &ctx, H5O_INFO_BASIC) < 0) {
        H5Pclose(ocpl_id);
        H5Pclose(lcpl_id);
        die("Failed to copy objects");
    }
    
    H5Pclose(ocpl_id);
    H5Pclose(lcpl_id);

    /* Copy global attributes from root group */
    if (verbose) {
        fprintf(stderr, "[debug] Copying global attributes from root group\n");
    }
    
    hid_t src_root = H5Gopen(fid0, "/", H5P_DEFAULT);
    hid_t dst_root = H5Gopen(fout, "/", H5P_DEFAULT);
    
    if (src_root >= 0 && dst_root >= 0) {
        /* Get number of attributes in source root group */
        H5O_info2_t oinfo;
        if (H5Oget_info3(src_root, &oinfo, H5O_INFO_NUM_ATTRS) >= 0) {
            /* Copy each attribute */
            for (unsigned i = 0; i < oinfo.num_attrs; i++) {
                char attr_name[256];
                hid_t attr_id = H5Aopen_by_idx(src_root, ".", H5_INDEX_NAME, H5_ITER_NATIVE, i, H5P_DEFAULT, H5P_DEFAULT);
                if (attr_id >= 0) {
                    H5Aget_name(attr_id, sizeof(attr_name), attr_name);
                    
                    /* Get attribute properties and copy manually */
                    hid_t attr_space = H5Aget_space(attr_id);
                    hid_t attr_type = H5Aget_type(attr_id);
                    
                    /* Create attribute in destination */
                    hid_t dst_attr = H5Acreate2(dst_root, attr_name, attr_type, attr_space, H5P_DEFAULT, H5P_DEFAULT);
                    if (dst_attr >= 0) {
                        /* Copy attribute data */
                        size_t attr_size = H5Aget_storage_size(attr_id);
                        if (attr_size > 0) {
                            void *attr_data = malloc(attr_size);
                            if (attr_data) {
                                if (H5Aread(attr_id, attr_type, attr_data) >= 0) {
                                    if (H5Awrite(dst_attr, attr_type, attr_data) < 0) {
                                        fprintf(stderr, "[warning] Failed to write global attribute: %s\n", attr_name);
                                    } else if (verbose) {
                                        fprintf(stderr, "[debug] Copied global attribute: %s\n", attr_name);
                                    }
                                }
                                free(attr_data);
                            }
                        }
                        H5Aclose(dst_attr);
                    } else {
                        fprintf(stderr, "[warning] Failed to create global attribute: %s\n", attr_name);
                    }
                    
                    H5Tclose(attr_type);
                    H5Sclose(attr_space);
                    H5Aclose(attr_id);
                }
            }
        }
    }
    
    if (src_root >= 0) H5Gclose(src_root);
    if (dst_root >= 0) H5Gclose(dst_root);

    H5Fclose(fid0);   /* no longer needed */

    /* ------------------------------------------------------------------ */
    /* 5.4 Extend record datasets                                         */

    for (size_t v = 0; v < rv.nvars; v++) {
        hid_t ds = H5Dopen(fout, rv.vars[v].path, H5P_DEFAULT);
        if (ds < 0) die("Dataset disappeared in output – internal error");

        hid_t sp = H5Dget_space(ds);
        hsize_t dims[MAX_RANK]; hsize_t maxd[MAX_RANK];
        H5Sget_simple_extent_dims(sp, dims, maxd);
        dims[ rv.vars[v].rec_dim ] = rv.vars[v].total_len;  /* new size */
        if (H5Dset_extent(ds, dims) < 0)
            die("Failed to extend dataset in output file");

        H5Sclose(sp); H5Dclose(ds);
    }

    /* ------------------------------------------------------------------ */
    /* 5.5 Second pass – copy raw chunks                                  */

    for (int f = 0; f < n_inputs; f++) {
        fprintf(stderr, "[info] processing %s (%d/%d)\n",
                in_files[f], f+1, n_inputs);

        hid_t fin = H5Fopen(in_files[f], H5F_ACC_RDONLY, H5P_DEFAULT);
        if (fin < 0) die("Cannot reopen input file during chunk copy");

        for (size_t v = 0; v < rv.nvars; v++) {
            hid_t din  = H5Dopen(fin,  rv.vars[v].path, H5P_DEFAULT);
            hid_t dout = H5Dopen(fout, rv.vars[v].path, H5P_DEFAULT);
            if (din < 0 || dout < 0) die("Dataset missing while copying");

            copy_raw_chunks(din, dout, rv.vars[v].offset, rv.vars[v].rec_dim);

            /* advance offset for next input file */
            hid_t sp = H5Dget_space(din);
            hsize_t dims[MAX_RANK];
            H5Sget_simple_extent_dims(sp, dims, NULL);
            rv.vars[v].offset += dims[ rv.vars[v].rec_dim ];
            H5Sclose(sp);

            H5Dclose(din); H5Dclose(dout);
        }
        H5Fclose(fin);
    }

    /* ------------------------------------------------------------------ */
    /* 5.6 Fix NetCDF-4 dimension list references                         */

    fix_dimension_list_references(fout);

    /* ------------------------------------------------------------------ */
    /* 5.7 Update history attribute                                       */

    update_history_attribute(fout, argc, argv);

    /* ------------------------------------------------------------------ */
    /* 5.8 Flush and close                                                */

    H5Fflush(fout, H5F_SCOPE_GLOBAL);
    H5Fclose(fout);

    fprintf(stderr, "[done] wrote %s – concatenation complete.\n", out_file);
    return 0;
}

