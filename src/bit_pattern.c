#include "bit_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <netcdf.h>

/* Error handling macro */
#define NC_CHECK(status) do { \
    if (status != NC_NOERR) { \
        fprintf(stderr, "NetCDF error: %s\n", nc_strerror(status)); \
        return status; \
    } \
} while(0)

/* Memory management functions */
void* safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Error: Memory allocation failed for %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_calloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "Error: Memory allocation failed for %zu elements\n", nmemb);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void free_variable_info(variable_info_t *var) {
    if (var && var->dims) {
        free(var->dims);
        var->dims = NULL;
    }
}

/* NetCDF utility functions */
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

/* Core bit analysis functions */
int analyze_float_bits(float *data, size_t len, uint8_t *bit_pattern, int bit_width) {
    /* Initialize pattern array: 0=all zeros, 1=all ones, 2=mixed */
    for (int i = 0; i < bit_width; i++) {
        bit_pattern[i] = 0;  /* Start assuming all zeros */
    }
    
    if (len == 0) return 0;
    
    /* Convert floats to integer representation for bit analysis */
    uint32_t *int_data = (uint32_t*)data;
    
    /* Check each bit position */
    for (int bit_pos = 0; bit_pos < bit_width; bit_pos++) {
        uint32_t mask = 1U << bit_pos;
        int all_zero = 1, all_one = 1;
        
        for (size_t i = 0; i < len; i++) {
            /* Skip NaN and infinite values */
            if (!isfinite(data[i])) continue;
            
            int bit_set = (int_data[i] & mask) != 0;
            
            if (bit_set) all_zero = 0;
            if (!bit_set) all_one = 0;
            
            /* Early exit if mixed */
            if (!all_zero && !all_one) break;
        }
        
        if (all_zero) {
            bit_pattern[bit_pos] = 0;  /* All zeros */
        } else if (all_one) {
            bit_pattern[bit_pos] = 1;  /* All ones */
        } else {
            bit_pattern[bit_pos] = 2;  /* Mixed */
        }
    }
    
    return 0;
}

char* format_bit_pattern(uint8_t *pattern, int bit_width, char *output) {
    int pos = 0;
    
    /* Add MSB marker */
    pos += sprintf(output + pos, "(MSB) ");
    
    /* Process bits from MSB to LSB */
    for (int i = bit_width - 1; i >= 0; i--) {
        char bit_char;
        switch (pattern[i]) {
            case 0: bit_char = '0'; break;  /* All zeros */
            case 1: bit_char = '1'; break;  /* All ones */
            default: bit_char = '-'; break; /* Mixed */
        }
        
        output[pos++] = bit_char;
        
        /* Add space every 8 bits (but not at the end) */
        if (i > 0 && (bit_width - i) % 8 == 0) {
            output[pos++] = ' ';
        }
    }
    
    /* Add LSB marker */
    pos += sprintf(output + pos, " (LSB)");
    output[pos] = '\0';
    
    return output;
}

int analyze_2d_slice(float *data, size_t len, bit_pattern_result_t *result) {
    result->bit_width = 32;  /* IEEE 754 float32 */
    uint8_t bit_pattern[32];
    
    int status = analyze_float_bits(data, len, bit_pattern, 32);
    if (status != 0) return status;
    
    /* Count pattern types */
    result->all_zeros_count = 0;
    result->all_ones_count = 0;
    result->mixed_count = 0;
    
    for (int i = 0; i < 32; i++) {
        switch (bit_pattern[i]) {
            case 0: result->all_zeros_count++; break;
            case 1: result->all_ones_count++; break;
            case 2: result->mixed_count++; break;
        }
    }
    
    /* Format the pattern string */
    format_bit_pattern(bit_pattern, 32, result->pattern);
    
    return 0;
}

int analyze_variable_bits(int ncid, variable_info_t *var, bit_pattern_result_t *result) {
    if (var->dtype != NC_FLOAT) {
        fprintf(stderr, "Warning: Skipping non-float variable '%s'\n", var->name);
        return NC_EBADTYPE;
    }
    
    float *data;
    int status = read_variable_data(ncid, var, (void**)&data);
    if (status != NC_NOERR) return status;
    
    status = analyze_2d_slice(data, var->total_elements, result);
    
    free(data);
    return status;
}

/* Output formatting functions */
void print_table_header(void) {
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("%-45s %-20s %-50s\n", "Variable", "Shape", "Bit Pattern (MSB->LSB)");
    printf("------------------------------------------------------------------------------------------------------------------------\n");
}

void print_variable_result(const char *var_name, const char *shape_str, const char *pattern) {
    printf("%-45s %-20s %s\n", var_name, shape_str, pattern);
}

void print_slice_result(const char *slice_name, const char *shape_str, const char *pattern) {
    printf("  %-43s %-20s %s\n", slice_name, shape_str, pattern);
}

void print_summary(int total_vars, int multi_vars) {
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("Analysis complete for %d variables\n", total_vars);
    printf("  %d variables analyzed slice-by-slice (3D+)\n", multi_vars);
    printf("  %d variables analyzed as whole (≤2D)\n", total_vars - multi_vars);
    printf("\n");
    printf("Summary:\n");
    printf("  Bit patterns show the state of each bit position across all values\n");
    printf("  '0' = all values have 0 at this bit position\n");
    printf("  '1' = all values have 1 at this bit position\n");
    printf("  '-' = mixed (some values have 0, some have 1)\n");
    printf("  Pattern format: (MSB) xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx (LSB)\n");
}

/* Utility functions */
void format_shape_string(size_t *dims, int ndims, char *output) {
    int pos = 0;
    for (int i = 0; i < ndims; i++) {
        if (i > 0) pos += sprintf(output + pos, "x");
        pos += sprintf(output + pos, "%zu", dims[i]);
    }
}

int is_multidimensional(variable_info_t *var) {
    return var->ndims > 2;
}

void cleanup_on_error(int ncid, variable_info_t *var, void *data) {
    if (data) free(data);
    if (var) free_variable_info(var);
    if (ncid >= 0) nc_close(ncid);
}