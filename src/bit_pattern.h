#ifndef BIT_PATTERN_H
#define BIT_PATTERN_H

#include <stdint.h>
#include <stddef.h>
#include <netcdf.h>

/* Maximum variable name length */
#define MAX_VAR_NAME 256
#define MAX_PATTERN_LEN 64

/* Data structures */
typedef struct {
    char name[MAX_VAR_NAME];
    int ndims;
    size_t *dims;
    nc_type dtype;
    int varid;
    size_t total_elements;
} variable_info_t;

typedef struct {
    char pattern[MAX_PATTERN_LEN];  /* MSB->LSB pattern with spaces */
    int bit_width;
    int all_zeros_count;
    int all_ones_count;
    int mixed_count;
} bit_pattern_result_t;

/* Function prototypes */

/* Core analysis functions */
int analyze_variable_bits(int ncid, variable_info_t *var, bit_pattern_result_t *result);
int analyze_2d_slice(float *data, size_t len, bit_pattern_result_t *result);
int analyze_float_bits(float *data, size_t len, uint8_t *bit_pattern, int bit_width);
char* format_bit_pattern(uint8_t *pattern, int bit_width, char *output);

/* NetCDF utilities */
int open_netcdf_file(const char *filename, int *ncid);
int get_variable_info(int ncid, int varid, variable_info_t *var);
int get_variable_count(int ncid, int *nvars);
int read_variable_data(int ncid, variable_info_t *var, void **data);
int read_2d_slice(int ncid, variable_info_t *var, size_t slice_index, float **data);

/* Memory management */
void free_variable_info(variable_info_t *var);
void* safe_malloc(size_t size);
void* safe_calloc(size_t nmemb, size_t size);

/* Output formatting */
void print_table_header(void);
void print_variable_result(const char *var_name, const char *shape_str, const char *pattern);
void print_slice_result(const char *slice_name, const char *shape_str, const char *pattern);
void print_summary(int total_vars, int multi_vars);

/* Utility functions */
void format_shape_string(size_t *dims, int ndims, char *output);
int is_multidimensional(variable_info_t *var);
void cleanup_on_error(int ncid, variable_info_t *var, void *data);

#endif /* BIT_PATTERN_H */