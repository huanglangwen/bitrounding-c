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

        /* Shift chunk coordinate by record offset, ensuring alignment */
        coord[rec_dim] += rec_offset;
        
        /* Ensure coordinate is aligned to chunk boundary */
        coord[rec_dim] = (coord[rec_dim] / chunk_dims[rec_dim]) * chunk_dims[rec_dim];
        
        /* Write chunk with the filter mask from the read operation */
        H5Dwrite_chunk(out_ds, H5P_DEFAULT, read_filt_mask,
                       coord, comp_sz, buf);
        free(buf);
    }
}

/* -------------------------------------------------------------------------- */
/* 4. Usage helper                                                            */

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
    /* 5.6 Flush and close                                                */

    H5Fflush(fout, H5F_SCOPE_GLOBAL);
    H5Fclose(fout);

    fprintf(stderr, "[done] wrote %s – concatenation complete.\n", out_file);
    return 0;
}

