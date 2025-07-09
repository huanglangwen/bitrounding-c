#include "bit_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hdf5.h>

void print_usage(const char *program_name) {
    printf("Usage: %s <hdf5_file>\n", program_name);
    printf("\n");
    printf("HDF5 Bit Precision Analysis Tool\n");
    printf("Analyzes bit usage patterns in HDF5 datasets to understand precision requirements.\n");
    printf("\n");
    printf("Features:\n");
    printf("  - Shows bit patterns for each dataset (MSB to LSB)\n");
    printf("  - Analyzes 3D+ datasets slice-by-slice (each 2D slice)\n");
    printf("  - Uses efficient C implementation with HDF5 library\n");
    printf("  - Formats bit patterns with spaces every 8 bits\n");
    printf("\n");
    printf("Output:\n");
    printf("  '0' = all values have 0 at this bit position\n");
    printf("  '1' = all values have 1 at this bit position\n");
    printf("  '-' = mixed (some values have 0, some have 1)\n");
}

/* HDF5-specific structures and utilities */
typedef struct {
    char name[MAX_VAR_NAME];
    int ndims;
    hsize_t *dims;
    H5T_class_t dtype_class;
    hid_t dtype;
    hid_t dataset_id;
    size_t total_elements;
} hdf5_dataset_info_t;

int open_hdf5_file(const char *filename, hid_t *file_id) {
    *file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (*file_id < 0) {
        return -1;
    }
    return 0;
}

herr_t dataset_iterator(hid_t loc_id, const char *name, const H5L_info_t *info, void *operator_data) {
    hid_t obj_id;
    H5O_info2_t obj_info;
    int *count = (int*)operator_data;
    (void)info; /* Suppress unused parameter warning */
    
    obj_id = H5Oopen(loc_id, name, H5P_DEFAULT);
    if (obj_id < 0) return 0;
    
    if (H5Oget_info3(obj_id, &obj_info, H5O_INFO_BASIC) >= 0 && obj_info.type == H5O_TYPE_DATASET) {
        (*count)++;
    }
    
    H5Oclose(obj_id);
    return 0;
}

int get_dataset_count(hid_t file_id, int *ndatasets) {
    *ndatasets = 0;
    if (H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, dataset_iterator, ndatasets) < 0) {
        return -1;
    }
    return 0;
}

typedef struct {
    hdf5_dataset_info_t *datasets;
    int count;
    int capacity;
} dataset_collector_t;

herr_t collect_datasets(hid_t loc_id, const char *name, const H5L_info_t *info, void *operator_data) {
    dataset_collector_t *collector = (dataset_collector_t*)operator_data;
    hid_t obj_id, space_id, type_id;
    H5O_info2_t obj_info;
    (void)info; /* Suppress unused parameter warning */
    
    obj_id = H5Oopen(loc_id, name, H5P_DEFAULT);
    if (obj_id < 0) return 0;
    
    if (H5Oget_info3(obj_id, &obj_info, H5O_INFO_BASIC) >= 0 && obj_info.type == H5O_TYPE_DATASET) {
        if (collector->count >= collector->capacity) {
            H5Oclose(obj_id);
            return 0;
        }
        
        hdf5_dataset_info_t *dataset = &collector->datasets[collector->count];
        
        /* Copy name */
        strncpy(dataset->name, name, MAX_VAR_NAME - 1);
        dataset->name[MAX_VAR_NAME - 1] = '\0';
        
        /* Get dataspace info */
        space_id = H5Dget_space(obj_id);
        if (space_id >= 0) {
            dataset->ndims = H5Sget_simple_extent_ndims(space_id);
            if (dataset->ndims > 0) {
                dataset->dims = malloc(dataset->ndims * sizeof(hsize_t));
                H5Sget_simple_extent_dims(space_id, dataset->dims, NULL);
                
                /* Calculate total elements */
                dataset->total_elements = 1;
                for (int i = 0; i < dataset->ndims; i++) {
                    dataset->total_elements *= dataset->dims[i];
                }
            }
            H5Sclose(space_id);
        }
        
        /* Get datatype info */
        type_id = H5Dget_type(obj_id);
        if (type_id >= 0) {
            dataset->dtype_class = H5Tget_class(type_id);
            dataset->dtype = H5Tcopy(type_id);
            H5Tclose(type_id);
        }
        
        dataset->dataset_id = obj_id; /* Keep dataset open for reading */
        collector->count++;
    } else {
        H5Oclose(obj_id);
    }
    
    return 0;
}

int get_all_datasets(hid_t file_id, hdf5_dataset_info_t **datasets, int *ndatasets) {
    /* First count datasets */
    if (get_dataset_count(file_id, ndatasets) < 0) {
        return -1;
    }
    
    if (*ndatasets == 0) {
        *datasets = NULL;
        return 0;
    }
    
    /* Allocate collector */
    dataset_collector_t collector;
    collector.datasets = malloc(*ndatasets * sizeof(hdf5_dataset_info_t));
    collector.count = 0;
    collector.capacity = *ndatasets;
    
    /* Collect datasets */
    if (H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, collect_datasets, &collector) < 0) {
        free(collector.datasets);
        return -1;
    }
    
    *datasets = collector.datasets;
    *ndatasets = collector.count;
    return 0;
}

dtype_t hdf5_to_dtype(H5T_class_t hdf5_class, size_t size, H5T_sign_t sign) {
    switch (hdf5_class) {
        case H5T_FLOAT:
            if (size == 4) return DTYPE_FLOAT32;
            if (size == 8) return DTYPE_FLOAT64;
            break;
        case H5T_INTEGER:
            if (size == 2) return sign == H5T_SGN_NONE ? DTYPE_UINT16 : DTYPE_INT16;
            if (size == 4) return sign == H5T_SGN_NONE ? DTYPE_UINT32 : DTYPE_INT32;
            if (size == 8) return sign == H5T_SGN_NONE ? DTYPE_UINT64 : DTYPE_INT64;
            break;
        default:
            break;
    }
    return (dtype_t)-1; /* Unsupported */
}

hid_t get_native_type(dtype_t dtype) {
    switch (dtype) {
        case DTYPE_FLOAT32: return H5T_NATIVE_FLOAT;
        case DTYPE_FLOAT64: return H5T_NATIVE_DOUBLE;
        case DTYPE_INT16: return H5T_NATIVE_SHORT;
        case DTYPE_UINT16: return H5T_NATIVE_USHORT;
        case DTYPE_INT32: return H5T_NATIVE_INT;
        case DTYPE_UINT32: return H5T_NATIVE_UINT;
        case DTYPE_INT64: return H5T_NATIVE_LLONG;
        case DTYPE_UINT64: return H5T_NATIVE_ULLONG;
        default: return -1;
    }
}

size_t get_element_size(dtype_t dtype) {
    switch (dtype) {
        case DTYPE_FLOAT32: return sizeof(float);
        case DTYPE_FLOAT64: return sizeof(double);
        case DTYPE_INT16: case DTYPE_UINT16: return sizeof(int16_t);
        case DTYPE_INT32: case DTYPE_UINT32: return sizeof(int32_t);
        case DTYPE_INT64: case DTYPE_UINT64: return sizeof(int64_t);
        default: return 0;
    }
}

int read_hdf5_dataset_data(hdf5_dataset_info_t *dataset, void **data, dtype_t *dtype) {
    /* Get HDF5 type info */
    size_t type_size = H5Tget_size(dataset->dtype);
    H5T_sign_t sign = H5T_SGN_ERROR;
    if (dataset->dtype_class == H5T_INTEGER) {
        sign = H5Tget_sign(dataset->dtype);
    }
    
    *dtype = hdf5_to_dtype(dataset->dtype_class, type_size, sign);
    if (*dtype == (dtype_t)-1) {
        return -1;
    }
    
    size_t element_size = get_element_size(*dtype);
    *data = malloc(dataset->total_elements * element_size);
    if (*data == NULL) {
        return -1;
    }
    
    hid_t native_type = get_native_type(*dtype);
    if (H5Dread(dataset->dataset_id, native_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, *data) < 0) {
        free(*data);
        *data = NULL;
        return -1;
    }
    
    return 0;
}

int read_hdf5_2d_slice(hdf5_dataset_info_t *dataset, hsize_t *slice_indices, void **data, dtype_t *dtype) {
    if (dataset->ndims < 3) {
        return -1;
    }
    
    /* Get HDF5 type info */
    size_t type_size = H5Tget_size(dataset->dtype);
    H5T_sign_t sign = H5T_SGN_ERROR;
    if (dataset->dtype_class == H5T_INTEGER) {
        sign = H5Tget_sign(dataset->dtype);
    }
    
    *dtype = hdf5_to_dtype(dataset->dtype_class, type_size, sign);
    if (*dtype == (dtype_t)-1) {
        return -1;
    }
    
    /* Calculate slice size */
    size_t slice_size = dataset->dims[dataset->ndims-2] * dataset->dims[dataset->ndims-1];
    size_t element_size = get_element_size(*dtype);
    *data = malloc(slice_size * element_size);
    if (*data == NULL) {
        return -1;
    }
    
    /* Create memory and file dataspaces for hyperslab */
    hsize_t mem_dims[2] = {dataset->dims[dataset->ndims-2], dataset->dims[dataset->ndims-1]};
    hid_t mem_space = H5Screate_simple(2, mem_dims, NULL);
    
    hid_t file_space = H5Dget_space(dataset->dataset_id);
    
    /* Define hyperslab - select a 2D slice from the last 2 dimensions */
    hsize_t start[dataset->ndims];
    hsize_t count[dataset->ndims];
    
    /* Set indices for all dimensions except the last 2 */
    for (int i = 0; i < dataset->ndims - 2; i++) {
        start[i] = slice_indices[i];
        count[i] = 1;
    }
    
    /* Set full size for the last 2 dimensions (the 2D slice) */
    start[dataset->ndims-2] = 0;
    start[dataset->ndims-1] = 0;
    count[dataset->ndims-2] = dataset->dims[dataset->ndims-2];
    count[dataset->ndims-1] = dataset->dims[dataset->ndims-1];
    
    if (H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, NULL, count, NULL) < 0) {
        free(*data);
        H5Sclose(mem_space);
        H5Sclose(file_space);
        return -1;
    }
    
    /* Read the slice */
    hid_t native_type = get_native_type(*dtype);
    if (H5Dread(dataset->dataset_id, native_type, mem_space, file_space, H5P_DEFAULT, *data) < 0) {
        free(*data);
        H5Sclose(mem_space);
        H5Sclose(file_space);
        return -1;
    }
    
    H5Sclose(mem_space);
    H5Sclose(file_space);
    return 0;
}

void free_hdf5_dataset_info(hdf5_dataset_info_t *dataset) {
    if (dataset->dims) {
        free(dataset->dims);
        dataset->dims = NULL;
    }
    if (dataset->dtype >= 0) {
        H5Tclose(dataset->dtype);
        dataset->dtype = -1;
    }
    if (dataset->dataset_id >= 0) {
        H5Dclose(dataset->dataset_id);
        dataset->dataset_id = -1;
    }
}

void format_hdf5_shape_string(hsize_t *dims, int ndims, char *output) {
    if (ndims == 0) {
        snprintf(output, 256, "scalar");
        return;
    }
    
    char temp[256] = "(";
    for (int i = 0; i < ndims; i++) {
        char dim_str[32];
        snprintf(dim_str, sizeof(dim_str), "%llu", (unsigned long long)dims[i]);
        
        // Check if we have enough space
        if (strlen(temp) + strlen(dim_str) + 10 >= 255) {
            snprintf(output, 256, "(...)");
            return;
        }
        
        strcat(temp, dim_str);
        if (i < ndims - 1) {
            strcat(temp, ", ");
        }
    }
    strcat(temp, ")");
    snprintf(output, 256, "%s", temp);
}

int is_hdf5_multidimensional(hdf5_dataset_info_t *dataset) {
    return dataset->ndims >= 3;
}

int analyze_hdf5_dataset_bits(hdf5_dataset_info_t *dataset, bit_pattern_result_t *result) {
    void *data = NULL;
    dtype_t dtype;
    
    if (read_hdf5_dataset_data(dataset, &data, &dtype) != 0) {
        return -1;
    }
    
    /* Use generic bit analysis function */
    int status = analyze_data_bits(data, dataset->total_elements, dtype, result);
    
    free(data);
    return status;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    const char *filename = argv[1];
    hid_t file_id = -1;
    hdf5_dataset_info_t *datasets = NULL;
    int ndatasets = 0;
    int multi_datasets = 0;
    
    printf("Loading HDF5 file: %s\n", filename);
    
    /* Initialize HDF5 */
    if (H5open() < 0) {
        fprintf(stderr, "Error: Cannot initialize HDF5 library\n");
        return EXIT_FAILURE;
    }
    
    /* Open HDF5 file */
    if (open_hdf5_file(filename, &file_id) != 0) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        H5close();
        return EXIT_FAILURE;
    }
    
    /* Get all datasets */
    if (get_all_datasets(file_id, &datasets, &ndatasets) != 0) {
        fprintf(stderr, "Error: Cannot get dataset information\n");
        H5Fclose(file_id);
        H5close();
        return EXIT_FAILURE;
    }
    
    printf("Dataset contains %d data variables\n", ndatasets);
    print_table_header();
    
    /* Process each dataset */
    for (int i = 0; i < ndatasets; i++) {
        hdf5_dataset_info_t *dataset = &datasets[i];
        bit_pattern_result_t result = {0};
        
        /* Skip datasets with only a single value */
        if (dataset->total_elements <= 1) {
            printf("%-45s %-20s %s\n", dataset->name, "N/A", "(skipped - single value)");
            continue;
        }
        
        /* Check if data type is supported */
        size_t type_size = H5Tget_size(dataset->dtype);
        H5T_sign_t sign = H5T_SGN_ERROR;
        if (dataset->dtype_class == H5T_INTEGER) {
            sign = H5Tget_sign(dataset->dtype);
        }
        
        dtype_t dtype = hdf5_to_dtype(dataset->dtype_class, type_size, sign);
        if (dtype == (dtype_t)-1) {
            printf("%-45s %-20s %s\n", dataset->name, "N/A", "(skipped - unsupported type)");
            continue;
        }
        
        /* Format shape string */
        char shape_str[256];
        format_hdf5_shape_string(dataset->dims, dataset->ndims, shape_str);
        
        /* Check if multidimensional (3D+) */
        if (is_hdf5_multidimensional(dataset)) {
            multi_datasets++;
            printf("%s (3D+)\n", dataset->name);
            printf("  %-43s %-20s %-50s\n", "Slice", shape_str, "Bit Pattern (MSB->LSB)");
            
            /* Analyze each 2D slice */
            char slice_shape_str[256];
            format_hdf5_shape_string(&dataset->dims[dataset->ndims-2], 2, slice_shape_str);
            
            /* Calculate total number of slices (product of all dimensions except last 2) */
            size_t total_slices = 1;
            for (int dim = 0; dim < dataset->ndims - 2; dim++) {
                total_slices *= dataset->dims[dim];
            }
            
            /* Iterate through all combinations of indices */
            hsize_t slice_indices[dataset->ndims - 2];
            for (int i = 0; i < dataset->ndims - 2; i++) {
                slice_indices[i] = 0;
            }
            
            for (size_t slice_num = 0; slice_num < total_slices; slice_num++) {
                void *slice_data = NULL;
                dtype_t slice_dtype;
                
                if (read_hdf5_2d_slice(dataset, slice_indices, &slice_data, &slice_dtype) != 0) {
                    fprintf(stderr, "Warning: Cannot read slice %zu of dataset '%s'\n", 
                            slice_num, dataset->name);
                    
                    /* Increment indices for next iteration */
                    int carry = 1;
                    for (int dim = dataset->ndims - 3; dim >= 0 && carry; dim--) {
                        slice_indices[dim]++;
                        if (slice_indices[dim] < dataset->dims[dim]) {
                            carry = 0;
                        } else {
                            slice_indices[dim] = 0;
                        }
                    }
                    continue;
                }
                
                /* Analyze this slice */
                size_t slice_size = dataset->dims[dataset->ndims-2] * dataset->dims[dataset->ndims-1];
                int status = analyze_data_bits(slice_data, slice_size, slice_dtype, &result);
                
                if (status == 0) {
                    /* Build slice name string */
                    char slice_name[128];
                    strcpy(slice_name, "[");
                    for (int dim = 0; dim < dataset->ndims - 2; dim++) {
                        char dim_str[16];
                        snprintf(dim_str, sizeof(dim_str), "%llu", (unsigned long long)slice_indices[dim]);
                        strcat(slice_name, dim_str);
                        if (dim < dataset->ndims - 3) {
                            strcat(slice_name, ",");
                        }
                    }
                    strcat(slice_name, ",:,:]");
                    
                    print_slice_result(slice_name, slice_shape_str, result.pattern);
                }
                
                free(slice_data);
                
                /* Increment indices for next iteration */
                int carry = 1;
                for (int dim = dataset->ndims - 3; dim >= 0 && carry; dim--) {
                    slice_indices[dim]++;
                    if (slice_indices[dim] < dataset->dims[dim]) {
                        carry = 0;
                    } else {
                        slice_indices[dim] = 0;
                    }
                }
            }
            
        } else {
            /* Analyze whole dataset for 2D or 1D */
            int status = analyze_hdf5_dataset_bits(dataset, &result);
            
            if (status == 0) {
                print_variable_result(dataset->name, shape_str, result.pattern);
            } else {
                print_variable_result(dataset->name, shape_str, "(analysis failed)");
            }
        }
    }
    
    /* Print summary */
    print_summary(ndatasets, multi_datasets);
    
    /* Cleanup */
    for (int i = 0; i < ndatasets; i++) {
        free_hdf5_dataset_info(&datasets[i]);
    }
    free(datasets);
    
    H5Fclose(file_id);
    H5close();
    
    printf("\nBit precision analysis complete for %d datasets\n", ndatasets);
    
    return EXIT_SUCCESS;
}
