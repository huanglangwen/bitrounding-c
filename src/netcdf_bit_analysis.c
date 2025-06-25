#include "bit_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netcdf.h>

/* NetCDF-specific data structures */
typedef struct {
    char name[MAX_VAR_NAME];
    int ndims;
    size_t *dims;
    nc_type dtype;
    int varid;
    size_t total_elements;
} variable_info_t;

/* Error handling macro */
#define NC_CHECK(status) do { \
    if (status != NC_NOERR) { \
        fprintf(stderr, "NetCDF error: %s\n", nc_strerror(status)); \
        return status; \
    } \
} while(0)

/* NetCDF-specific utility functions */
void free_variable_info(variable_info_t *var) {
    if (var && var->dims) {
        free(var->dims);
        var->dims = NULL;
    }
}

int open_netcdf_file(const char *filename, int *ncid) {
    int status = nc_open(filename, NC_NOERR, ncid);
    NC_CHECK(status);
    return NC_NOERR;
}

int get_variable_count(int ncid, int *nvars) {
    int status = nc_inq_nvars(ncid, nvars);
    NC_CHECK(status);
    return NC_NOERR;
}

int get_variable_info(int ncid, int varid, variable_info_t *var) {
    int status;
    
    /* Get variable name */
    status = nc_inq_varname(ncid, varid, var->name);
    NC_CHECK(status);
    
    /* Get variable type and dimensions */
    status = nc_inq_var(ncid, varid, NULL, &var->dtype, &var->ndims, NULL, NULL);
    NC_CHECK(status);
    
    /* Allocate and get dimension IDs */
    int *dimids = safe_malloc(var->ndims * sizeof(int));
    status = nc_inq_var(ncid, varid, NULL, NULL, NULL, dimids, NULL);
    NC_CHECK(status);
    
    /* Allocate and get dimension sizes */
    var->dims = safe_malloc(var->ndims * sizeof(size_t));
    var->total_elements = 1;
    
    for (int i = 0; i < var->ndims; i++) {
        status = nc_inq_dimlen(ncid, dimids[i], &var->dims[i]);
        NC_CHECK(status);
        var->total_elements *= var->dims[i];
    }
    
    var->varid = varid;
    free(dimids);
    
    return NC_NOERR;
}

int read_variable_data(int ncid, variable_info_t *var, void **data) {
    if (var->dtype != NC_FLOAT) {
        fprintf(stderr, "Error: Only float variables are supported\n");
        return NC_EBADTYPE;
    }
    
    *data = safe_malloc(var->total_elements * sizeof(float));
    
    int status = nc_get_var_float(ncid, var->varid, (float*)*data);
    NC_CHECK(status);
    
    return NC_NOERR;
}

int read_2d_slice(int ncid, variable_info_t *var, size_t slice_index, float **data) {
    if (var->dtype != NC_FLOAT) {
        fprintf(stderr, "Error: Only float variables are supported\n");
        return NC_EBADTYPE;
    }
    
    if (var->ndims < 3) {
        fprintf(stderr, "Error: Variable must have at least 3 dimensions for slicing\n");
        return NC_EINVAL;
    }
    
    /* Calculate 2D slice size (last two dimensions) */
    size_t slice_size = var->dims[var->ndims-2] * var->dims[var->ndims-1];
    *data = safe_malloc(slice_size * sizeof(float));
    
    /* Set up start and count arrays for hyperslab reading */
    size_t *start = safe_calloc(var->ndims, sizeof(size_t));
    size_t *count = safe_calloc(var->ndims, sizeof(size_t));
    
    /* Set slice index for first dimension, read full last two dimensions */
    start[0] = slice_index;
    count[0] = 1;
    for (int i = 1; i < var->ndims - 2; i++) {
        start[i] = 0;
        count[i] = 1;  /* For now, assume 3D only */
    }
    count[var->ndims-2] = var->dims[var->ndims-2];
    count[var->ndims-1] = var->dims[var->ndims-1];
    
    int status = nc_get_vara_float(ncid, var->varid, start, count, *data);
    
    free(start);
    free(count);
    NC_CHECK(status);
    
    return NC_NOERR;
}

int analyze_variable_bits(int ncid, variable_info_t *var, bit_pattern_result_t *result) {
    if (var->dtype != NC_FLOAT) {
        fprintf(stderr, "Warning: Skipping non-float variable '%s'\n", var->name);
        return NC_EBADTYPE;
    }
    
    float *data;
    int status = read_variable_data(ncid, var, (void**)&data);
    if (status != NC_NOERR) return status;
    
    status = analyze_data_bits(data, var->total_elements, DTYPE_FLOAT32, result);
    
    free(data);
    return status;
}

int is_multidimensional(variable_info_t *var) {
    return var->ndims > 2;
}

void cleanup_on_error(int ncid, variable_info_t *var, void *data) {
    if (data) free(data);
    if (var) free_variable_info(var);
    if (ncid >= 0) nc_close(ncid);
}

void print_usage(const char *program_name) {
    printf("Usage: %s <netcdf_file>\n", program_name);
    printf("\n");
    printf("NetCDF Bit Precision Analysis Tool\n");
    printf("Analyzes bit usage patterns in NetCDF variables to understand precision requirements.\n");
    printf("\n");
    printf("Features:\n");
    printf("  - Shows bit patterns for each variable (MSB to LSB)\n");
    printf("  - Analyzes 3D+ variables slice-by-slice (each 2D slice)\n");
    printf("  - Uses efficient C implementation with NetCDF library\n");
    printf("  - Formats bit patterns with spaces every 8 bits\n");
    printf("\n");
    printf("Output:\n");
    printf("  '0' = all values have 0 at this bit position\n");
    printf("  '1' = all values have 1 at this bit position\n");
    printf("  '-' = mixed (some values have 0, some have 1)\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    const char *filename = argv[1];
    int ncid = -1;
    int nvars = 0;
    int multi_vars = 0;
    
    printf("Loading NetCDF file: %s\n", filename);
    
    /* Open NetCDF file */
    int status = open_netcdf_file(filename, &ncid);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return EXIT_FAILURE;
    }
    
    /* Get number of variables */
    status = get_variable_count(ncid, &nvars);
    if (status != NC_NOERR) {
        fprintf(stderr, "Error: Cannot get variable count\n");
        nc_close(ncid);
        return EXIT_FAILURE;
    }
    
    printf("Dataset contains %d data variables\n", nvars);
    print_table_header();
    
    /* Process each variable */
    for (int varid = 0; varid < nvars; varid++) {
        variable_info_t var = {0};
        bit_pattern_result_t result = {0};
        
        /* Get variable information */
        status = get_variable_info(ncid, varid, &var);
        if (status != NC_NOERR) {
            fprintf(stderr, "Warning: Cannot get info for variable %d\n", varid);
            continue;
        }
        
        /* Skip non-float variables */
        if (var.dtype != NC_FLOAT) {
            printf("%-45s %-20s %s\n", var.name, "N/A", "(skipped - not float)");
            free_variable_info(&var);
            continue;
        }
        
        /* Format shape string */
        char shape_str[256];
        format_shape_string(var.dims, var.ndims, shape_str);
        
        /* Check if multidimensional (3D+) */
        if (is_multidimensional(&var)) {
            multi_vars++;
            printf("%s (3D+)\n", var.name);
            printf("  %-43s %-20s %-50s\n", "Slice", shape_str, "Bit Pattern (MSB->LSB)");
            
            /* Analyze each 2D slice */
            size_t num_slices = var.dims[0];
            char slice_shape_str[256];
            format_shape_string(&var.dims[var.ndims-2], 2, slice_shape_str);
            
            for (size_t slice_idx = 0; slice_idx < num_slices; slice_idx++) {
                float *slice_data = NULL;
                
                status = read_2d_slice(ncid, &var, slice_idx, &slice_data);
                if (status != NC_NOERR) {
                    fprintf(stderr, "Warning: Cannot read slice %zu of variable '%s'\n", 
                            slice_idx, var.name);
                    continue;
                }
                
                /* Analyze this slice */
                size_t slice_size = var.dims[var.ndims-2] * var.dims[var.ndims-1];
                status = analyze_data_bits(slice_data, slice_size, DTYPE_FLOAT32, &result);
                
                if (status == 0) {
                    char slice_name[64];
                    snprintf(slice_name, sizeof(slice_name), "[%zu,:,:]", slice_idx);
                    print_slice_result(slice_name, slice_shape_str, result.pattern);
                }
                
                free(slice_data);
            }
            
        } else {
            /* Analyze whole variable for 2D or 1D */
            status = analyze_variable_bits(ncid, &var, &result);
            
            if (status == 0) {
                print_variable_result(var.name, shape_str, result.pattern);
            } else {
                print_variable_result(var.name, shape_str, "(analysis failed)");
            }
        }
        
        free_variable_info(&var);
    }
    
    /* Print summary */
    print_summary(nvars, multi_vars);
    
    /* Cleanup */
    nc_close(ncid);
    
    printf("\nBit precision analysis complete for %d variables\n", nvars);
    
    return EXIT_SUCCESS;
}