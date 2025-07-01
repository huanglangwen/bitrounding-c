#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <netcdf.h>
#include "bitrounding_bitinfo.h"
#include "bitrounding_stats.h"

#define MAX_VARS 100
#define MAX_NAME_LEN 256

static long file_size(FILE *f) {
    long cur, size;

    /* remember current position */
    cur = ftell(f);
    if (cur < 0) return -1;

    /* seek to end */
    if (fseek(f, 0, SEEK_END) != 0) return -1;

    /* get position = size */
    size = ftell(f);
    if (size < 0) return -1;

    /* restore file position */
    if (fseek(f, cur, SEEK_SET) != 0) return -1;

    return size;
}

static int analyze_and_get_nsb(size_t len, float *v, double infLevel, int use_monotonic) {
    if (len < 2) return 1;
    
    float *v_copy = malloc(len * sizeof(float));
    if (!v_copy) return -1;
    
    memcpy(v_copy, v, len * sizeof(float));
    signed_exponent(v_copy, len);
    
    MutualInformation bitInfo = bitinformation(v_copy, len);
    int nsb;
    if (use_monotonic) {
        nsb = get_keepbits_monotonic(&bitInfo, infLevel);
    } else {
        nsb = get_keepbits(&bitInfo, infLevel);
    }
    
    free(v_copy);
    return nsb;
}

static void bitround(int nsb, size_t len, float *v, float missval) {
    const uint32_t BIT_XPL_NBR_SGN_FLT = 23;
    
    uint32_t prc_bnr_xpl_rqr = nsb;
    uint32_t bit_xpl_nbr_zro = BIT_XPL_NBR_SGN_FLT - prc_bnr_xpl_rqr;
    
    uint32_t msk_f32_u32_zro = 0u;
    msk_f32_u32_zro = ~msk_f32_u32_zro;
    msk_f32_u32_zro <<= bit_xpl_nbr_zro;
    
    uint32_t msk_f32_u32_one = ~msk_f32_u32_zro;
    uint32_t msk_f32_u32_hshv = msk_f32_u32_one & (msk_f32_u32_zro >> 1);
    
    uint32_t *u32_ptr = (uint32_t *)v;
    
    for (size_t idx = 0; idx < len; idx++) {
        if (v[idx] != missval && !isnan(v[idx])) {
            u32_ptr[idx] += msk_f32_u32_hshv;
            u32_ptr[idx] &= msk_f32_u32_zro;
        }
    }
}

static void print_usage(const char *program_name) {
    printf("Usage: %s <inflevel> <input.nc> <output.nc> [--complevel=x] [--monotonic-bitinfo]\n", program_name);
    printf("\nNetCDF Bit Rounding Tool\n");
    printf("Applies bitrounding to float variables in NetCDF files.\n\n");
    printf("Arguments:\n");
    printf("  inflevel          - Information level threshold (0.0-1.0, typically 0.9999)\n");
    printf("  input.nc          - Input NetCDF file\n");
    printf("  output.nc         - Output NetCDF file\n");
    printf("  --complevel       - Optional compression level (1-9), enables shuffle filter\n");
    printf("  --monotonic-bitinfo - Use monotonic filtering when calculating bit information\n");
}

static int copy_non_float_variable(int ncid_in, int ncid_out, int varid, nc_type vartype, size_t varsize) {
    char varname[NC_MAX_NAME + 1];
    nc_inq_varname(ncid_in, varid, varname);
    
    const char* dtype_name;
    switch (vartype) {
        case NC_BYTE: dtype_name = "NC_BYTE"; break;
        case NC_SHORT: dtype_name = "NC_SHORT"; break;
        case NC_INT: dtype_name = "NC_INT"; break;
        case NC_INT64: dtype_name = "NC_INT64"; break;
        case NC_DOUBLE: dtype_name = "NC_DOUBLE"; break;
        case NC_CHAR: dtype_name = "NC_CHAR"; break;
        case NC_STRING: dtype_name = "NC_STRING"; break;
        default: dtype_name = "UNKNOWN"; break;
    }
    
    printf("Variable %s: dtype=%s, passthrough\n", varname, dtype_name);
    switch (vartype) {
        case NC_BYTE: {
            signed char *data = malloc(varsize * sizeof(signed char));
            if (!data) return -1;
            if (nc_get_var_schar(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_schar(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_SHORT: {
            short *data = malloc(varsize * sizeof(short));
            if (!data) return -1;
            if (nc_get_var_short(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_short(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_INT: {
            int *data = malloc(varsize * sizeof(int));
            if (!data) return -1;
            if (nc_get_var_int(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_int(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_INT64: {
            long long *data = malloc(varsize * sizeof(long long));
            if (!data) return -1;
            if (nc_get_var_longlong(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_longlong(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_DOUBLE: {
            double *data = malloc(varsize * sizeof(double));
            if (!data) return -1;
            if (nc_get_var_double(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_double(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_CHAR: {
            char *data = malloc(varsize * sizeof(char));
            if (!data) return -1;
            if (nc_get_var_text(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_text(ncid_out, varid, data);
            }
            free(data);
            break;
        }
        case NC_STRING: {
            char **data = malloc(varsize * sizeof(char*));
            if (!data) return -1;
            if (nc_get_var_string(ncid_in, varid, data) == NC_NOERR) {
                nc_put_var_string(ncid_out, varid, (const char**)data);
                nc_free_string(varsize, data);
            } else {
                free(data);
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    double inflevel = atof(argv[1]);
    const char *input_file = argv[2];
    const char *output_file = argv[3];
    
    int compression_level = 0;
    int use_monotonic = 0;
    
    // Parse optional arguments
    for (int i = 4; i < argc; i++) {
        if (strncmp(argv[i], "--complevel=", 12) == 0) {
            compression_level = atoi(argv[i] + 12);
            if (compression_level < 1 || compression_level > 9) {
                fprintf(stderr, "Error: compression level must be between 1 and 9\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--monotonic-bitinfo") == 0) {
            use_monotonic = 1;
        } else {
            fprintf(stderr, "Error: Invalid argument '%s'\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    
    if (inflevel < 0.0 || inflevel > 1.0) {
        fprintf(stderr, "Error: inflevel must be between 0.0 and 1.0\n");
        return EXIT_FAILURE;
    }
    
    int ncid_in, ncid_out;
    int ndims, nvars, ngatts, unlimdimid;
    
    if (compression_level > 0) {
        printf("Processing: %s -> %s (inflevel=%.6f, compression=%d, shuffle=enabled%s)\n", 
               input_file, output_file, inflevel, compression_level, 
               use_monotonic ? ", monotonic-bitinfo=enabled" : "");
    } else {
        printf("Processing: %s -> %s (inflevel=%.6f%s)\n", input_file, output_file, inflevel,
               use_monotonic ? ", monotonic-bitinfo=enabled" : "");
    }
    
    if (nc_open(input_file, NC_NOWRITE, &ncid_in) != NC_NOERR) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", input_file);
        return EXIT_FAILURE;
    }
    
    if (nc_inq(ncid_in, &ndims, &nvars, &ngatts, &unlimdimid) != NC_NOERR) {
        fprintf(stderr, "Error: Cannot inquire input file\n");
        nc_close(ncid_in);
        return EXIT_FAILURE;
    }
    
    if (nc_create(output_file, NC_CLOBBER | NC_NETCDF4, &ncid_out) != NC_NOERR) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        nc_close(ncid_in);
        return EXIT_FAILURE;
    }
    
    int dimids_out[NC_MAX_DIMS];
    for (int i = 0; i < ndims; i++) {
        char dimname[NC_MAX_NAME + 1];
        size_t dimlen;
        
        if (nc_inq_dim(ncid_in, i, dimname, &dimlen) != NC_NOERR) continue;
        
        if (i == unlimdimid) {
            nc_def_dim(ncid_out, dimname, NC_UNLIMITED, &dimids_out[i]);
        } else {
            nc_def_dim(ncid_out, dimname, dimlen, &dimids_out[i]);
        }
    }
    
    for (int varid = 0; varid < nvars; varid++) {
        char varname[NC_MAX_NAME + 1];
        nc_type vartype;
        int varndims;
        int vardimids[NC_MAX_VAR_DIMS];
        int varnatts;
        
        if (nc_inq_var(ncid_in, varid, varname, &vartype, &varndims, vardimids, &varnatts) != NC_NOERR) {
            continue;
        }
        
        int varid_out;
        nc_def_var(ncid_out, varname, vartype, varndims, vardimids, &varid_out);
        
        // Apply compression and chunking if enabled
        if (compression_level > 0) {
            // Check if this variable is a coordinate variable or will be skipped
            int is_coordinate = 0;
            for (int d = 0; d < ndims; d++) {
                char dimname[NC_MAX_NAME + 1];
                if (nc_inq_dimname(ncid_in, d, dimname) == NC_NOERR) {
                    if (strcmp(varname, dimname) == 0) {
                        is_coordinate = 1;
                        break;
                    }
                }
            }
            
            // Only set chunking for non-coordinate float variables that will get bitrounding
            if (!is_coordinate && vartype == NC_FLOAT) {
                size_t chunksizes[NC_MAX_VAR_DIMS];
                
                if (varndims >= 3) {
                    // For 3D+ variables: chunk using last two dimensions, set others to 1
                    for (int d = 0; d < varndims; d++) {
                        if (d < varndims - 2) {
                            chunksizes[d] = 1;
                        } else {
                            size_t dimlen;
                            nc_inq_dimlen(ncid_in, vardimids[d], &dimlen);
                            chunksizes[d] = dimlen;
                        }
                    }
                } else {
                    // For 1D and 2D variables: chunk using full variable shape
                    for (int d = 0; d < varndims; d++) {
                        size_t dimlen;
                        nc_inq_dimlen(ncid_in, vardimids[d], &dimlen);
                        chunksizes[d] = dimlen;
                    }
                }
                
                nc_def_var_chunking(ncid_out, varid_out, NC_CHUNKED, chunksizes);
            }
            
            nc_def_var_deflate(ncid_out, varid_out, 1, 1, compression_level);
        }
        
        for (int a = 0; a < varnatts; a++) {
            char attname[NC_MAX_NAME + 1];
            if (nc_inq_attname(ncid_in, varid, a, attname) == NC_NOERR) {
                nc_copy_att(ncid_in, varid, attname, ncid_out, varid_out);
            }
        }
    }
    
    for (int a = 0; a < ngatts; a++) {
        char attname[NC_MAX_NAME + 1];
        if (nc_inq_attname(ncid_in, NC_GLOBAL, a, attname) == NC_NOERR) {
            nc_copy_att(ncid_in, NC_GLOBAL, attname, ncid_out, NC_GLOBAL);
        }
    }
    
    nc_enddef(ncid_out);
    
    int processed_vars = 0;
    int bitrounded_vars = 0;
    
    for (int varid = 0; varid < nvars; varid++) {
        char varname[NC_MAX_NAME + 1];
        nc_type vartype;
        int varndims;
        int vardimids[NC_MAX_VAR_DIMS];
        int varnatts;
        
        if (nc_inq_var(ncid_in, varid, varname, &vartype, &varndims, vardimids, &varnatts) != NC_NOERR) {
            continue;
        }
        
        size_t varsize = 1;
        for (int d = 0; d < varndims; d++) {
            size_t dimlen;
            nc_inq_dimlen(ncid_in, vardimids[d], &dimlen);
            varsize *= dimlen;
        }
        
        processed_vars++;
        
        if (vartype == NC_FLOAT && varsize > 0) {
            // Check if this variable is a coordinate variable
            int is_coordinate = 0;
            for (int d = 0; d < ndims; d++) {
                char dimname[NC_MAX_NAME + 1];
                if (nc_inq_dimname(ncid_in, d, dimname) == NC_NOERR) {
                    if (strcmp(varname, dimname) == 0) {
                        is_coordinate = 1;
                        break;
                    }
                }
            }
            
            if (is_coordinate) {
                printf("Variable %s: Skipping bitrounding (coordinate variable)\n", varname);
                float *data = malloc(varsize * sizeof(float));
                if (data && nc_get_var_float(ncid_in, varid, data) == NC_NOERR) {
                    nc_put_var_float(ncid_out, varid, data);
                }
                free(data);
                continue;
            }
            
            float *data = malloc(varsize * sizeof(float));
            if (!data) {
                fprintf(stderr, "Error: Memory allocation failed for variable %s\n", varname);
                continue;
            }
            
            if (nc_get_var_float(ncid_in, varid, data) == NC_NOERR) {
                float missval = NC_FILL_FLOAT;
                int has_fillvalue = 0;
                size_t attlen;
                if (nc_inq_attlen(ncid_in, varid, "_FillValue", &attlen) == NC_NOERR && attlen == 1) {
                    nc_get_att_float(ncid_in, varid, "_FillValue", &missval);
                    has_fillvalue = 1;
                }
                
                // Check if data contains fill values or NaNs
                int contains_missing = 0;
                for (size_t i = 0; i < varsize; i++) {
                    if (isnan(data[i]) || (has_fillvalue && data[i] == missval)) {
                        contains_missing = 1;
                        break;
                    }
                }
                
                if (contains_missing) {
                    printf("Variable %s: Skipping bitrounding (contains missing values or NaNs)\n", varname);
                    nc_put_var_float(ncid_out, varid, data);
                    free(data);
                    continue;
                }
                
                // Calculate chunk size based on last two dimensions
                size_t chunk_size = 1;
                size_t dims[NC_MAX_VAR_DIMS];
                for (int d = 0; d < varndims; d++) {
                    nc_inq_dimlen(ncid_in, vardimids[d], &dims[d]);
                    if (d >= varndims - 2) {
                        chunk_size *= dims[d];
                    }
                }
                
                printf("Variable %s: chunk_size=%zu", varname, chunk_size);
                
                if (varndims <= 2) {
                    // For 1D or 2D variables, process as single chunk
                    int nsb = analyze_and_get_nsb(varsize, data, inflevel, use_monotonic);
                    if (nsb > 0 && nsb <= 23) {
                        bitround(nsb, varsize, data, missval);
                        printf(", NSB=%d\n", nsb);
                        bitrounded_vars++;
                    } else {
                        printf(", NSB analysis failed or invalid\n");
                    }
                } else {
                    // For 3D+ variables, process chunk by chunk
                    size_t num_chunks = varsize / chunk_size;
                    printf(", num_chunks=%zu\n", num_chunks);
                    
                    int chunk_processed = 0;
                    int min_nsb = 1000;
                    int max_nsb = -1000;
                    
                    for (size_t chunk = 0; chunk < num_chunks; chunk++) {
                        float *chunk_data = data + (chunk * chunk_size);
                        
                        int nsb = analyze_and_get_nsb(chunk_size, chunk_data, inflevel, use_monotonic);
                        if (nsb > 0 && nsb <= 23) {
                            bitround(nsb, chunk_size, chunk_data, missval);
                            chunk_processed++;
                            
                            if (nsb < min_nsb) min_nsb = nsb;
                            if (nsb > max_nsb) max_nsb = nsb;
                        }
                    }
                    
                    if (chunk_processed > 0) {
                        printf("  Processed %d/%zu chunks, NSB min=%d max=%d\n", 
                               chunk_processed, num_chunks, min_nsb, max_nsb);
                        bitrounded_vars++;
                    } else {
                        printf("  No chunks processed successfully\n");
                    }
                }
                
                nc_put_var_float(ncid_out, varid, data);
            }
            
            free(data);
        } else if (varsize > 0) {
            if (copy_non_float_variable(ncid_in, ncid_out, varid, vartype, varsize) != 0) {
                printf("Variable %s: Failed to copy non-float variable\n", varname);
            }
        }
    }
    
    nc_close(ncid_in);
    nc_close(ncid_out);
    
    // Get file sizes for compression ratio calculation
    FILE *input_fp = fopen(input_file, "rb");
    FILE *output_fp = fopen(output_file, "rb");
    long input_size = -1, output_size = -1;
    
    if (input_fp) {
        input_size = file_size(input_fp);
        fclose(input_fp);
    }
    
    if (output_fp) {
        output_size = file_size(output_fp);
        fclose(output_fp);
    }
    
    printf("\nBitrounding complete:\n");
    printf("  Processed variables: %d\n", processed_vars);
    printf("  Bitrounded variables: %d\n", bitrounded_vars);
    printf("  Output file: %s\n", output_file);
    
    if (input_size > 0 && output_size > 0) {
        printf("  Input file size: %.2f MB\n", input_size / (1024.0 * 1024.0));
        printf("  Output file size: %.2f MB\n", output_size / (1024.0 * 1024.0));
        printf("  Compression ratio: %.2f:1\n", (double)input_size / output_size);
    }
    
    return EXIT_SUCCESS;
}