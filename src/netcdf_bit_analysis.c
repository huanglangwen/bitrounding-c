#include "bit_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netcdf.h>

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
                status = analyze_2d_slice(slice_data, slice_size, &result);
                
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