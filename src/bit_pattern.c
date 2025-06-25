#include "bit_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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



/* Core bit analysis functions */
int analyze_bits_generic(void *data, size_t len, size_t element_size, uint8_t *bit_pattern, int bit_width, int check_finite) {
    /* Initialize pattern array: 0=all zeros, 1=all ones, 2=mixed */
    for (int i = 0; i < bit_width; i++) {
        bit_pattern[i] = 0;  /* Start assuming all zeros */
    }
    
    if (len == 0) return 0;
    
    /* Check each bit position */
    for (int bit_pos = 0; bit_pos < bit_width; bit_pos++) {
        int all_zero = 1, all_one = 1;
        
        for (size_t i = 0; i < len; i++) {
            uint8_t *element = (uint8_t*)data + i * element_size;
            int bit_set = 0;
            
            /* Skip NaN and infinite values for float types */
            if (check_finite) {
                if (element_size == sizeof(float)) {
                    float val = *(float*)element;
                    if (!isfinite(val)) continue;
                } else if (element_size == sizeof(double)) {
                    double val = *(double*)element;
                    if (!isfinite(val)) continue;
                }
            }
            
            /* Check bit at position bit_pos */
            int byte_index = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if ((size_t)byte_index < element_size) {
                bit_set = (element[byte_index] & (1 << bit_in_byte)) != 0;
            }
            
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

int analyze_data_bits(void *data, size_t len, dtype_t dtype, bit_pattern_result_t *result) {
    uint8_t bit_pattern[64];  /* Support up to 64-bit types */
    size_t element_size = 0;
    int bit_width = 0;
    int check_finite = 0;
    
    switch (dtype) {
        case DTYPE_FLOAT32:
            element_size = sizeof(float);
            bit_width = 32;
            check_finite = 1;
            break;
        case DTYPE_FLOAT64:
            element_size = sizeof(double);
            bit_width = 64;
            check_finite = 1;
            break;
        case DTYPE_INT16:
        case DTYPE_UINT16:
            element_size = sizeof(int16_t);
            bit_width = 16;
            break;
        case DTYPE_INT32:
        case DTYPE_UINT32:
            element_size = sizeof(int32_t);
            bit_width = 32;
            break;
        case DTYPE_INT64:
        case DTYPE_UINT64:
            element_size = sizeof(int64_t);
            bit_width = 64;
            break;
        default:
            fprintf(stderr, "Error: Unsupported data type %d\n", dtype);
            return -1;
    }
    
    int status = analyze_bits_generic(data, len, element_size, bit_pattern, bit_width, check_finite);
    if (status != 0) return status;
    
    result->bit_width = bit_width;
    
    /* Count pattern types */
    result->all_zeros_count = 0;
    result->all_ones_count = 0;
    result->mixed_count = 0;
    
    for (int i = 0; i < bit_width; i++) {
        switch (bit_pattern[i]) {
            case 0: result->all_zeros_count++; break;
            case 1: result->all_ones_count++; break;
            case 2: result->mixed_count++; break;
        }
    }
    
    /* Format the pattern string */
    format_bit_pattern(bit_pattern, bit_width, dtype, result->pattern);
    
    return 0;
}

char* format_bit_pattern(uint8_t *pattern, int bit_width, dtype_t dtype, char *output) {
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
        
        /* Add IEEE 754 field separators for float types */
        if (dtype == DTYPE_FLOAT32 && bit_width == 32) {
            /* Float32: S|EEEEEEEE|MMMMMMMMMMMMMMMMMMMMMMM */
            /* Sign bit at position 31, exponent at 30-23, mantissa at 22-0 */
            if (i == 31) {  /* After sign bit (position 31) */
                output[pos++] = '|';
            } else if (i == 23) {  /* After exponent (positions 30-23) */
                output[pos++] = '|';
            }
        } else if (dtype == DTYPE_FLOAT64 && bit_width == 64) {
            /* Float64: S|EEEEEEEEEEE|MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM */
            /* Sign bit at position 63, exponent at 62-52, mantissa at 51-0 */
            if (i == 63) {  /* After sign bit (position 63) */
                output[pos++] = '|';
            } else if (i == 52) {  /* After exponent (positions 62-52) */
                output[pos++] = '|';
            }
        }
        
        /* Add space every 8 bits (but not at the end, and not if we just added |) */
        if (i > 0 && (bit_width - i) % 8 == 0 && (pos > 0 && output[pos-1] != '|')) {
            output[pos++] = ' ';
        }
    }
    
    /* Add LSB marker */
    pos += sprintf(output + pos, " (LSB)");
    output[pos] = '\0';
    
    return output;
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


