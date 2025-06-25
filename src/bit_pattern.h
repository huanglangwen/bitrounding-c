#ifndef BIT_PATTERN_H
#define BIT_PATTERN_H

#include <stdint.h>
#include <stddef.h>

/* Maximum variable name length */
#define MAX_VAR_NAME 256
#define MAX_PATTERN_LEN 256

/* Generic data type enum for bit pattern analysis */
typedef enum {
    DTYPE_FLOAT32 = 0,
    DTYPE_FLOAT64 = 1,
    DTYPE_INT16 = 2,
    DTYPE_UINT16 = 3,
    DTYPE_INT32 = 4,
    DTYPE_UINT32 = 5,
    DTYPE_INT64 = 6,
    DTYPE_UINT64 = 7
} dtype_t;

typedef struct {
    char pattern[MAX_PATTERN_LEN];  /* MSB->LSB pattern with spaces */
    int bit_width;
    int all_zeros_count;
    int all_ones_count;
    int mixed_count;
} bit_pattern_result_t;

/* Function prototypes */

/* Core analysis functions */
int analyze_bits_generic(void *data, size_t len, size_t element_size, uint8_t *bit_pattern, int bit_width, int check_finite);
int analyze_data_bits(void *data, size_t len, dtype_t dtype, bit_pattern_result_t *result);
char* format_bit_pattern(uint8_t *pattern, int bit_width, dtype_t dtype, char *output);

/* Memory management */
void* safe_malloc(size_t size);
void* safe_calloc(size_t nmemb, size_t size);

/* Output formatting */
void print_table_header(void);
void print_variable_result(const char *var_name, const char *shape_str, const char *pattern);
void print_slice_result(const char *slice_name, const char *shape_str, const char *pattern);
void print_summary(int total_vars, int multi_vars);

/* Utility functions */
void format_shape_string(size_t *dims, int ndims, char *output);

#endif /* BIT_PATTERN_H */