#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <hdf5.h>

#define MAX_VAR_NAME 256
#define MAX_CHUNKS_PER_VAR 50

typedef struct {
    char name[MAX_VAR_NAME];
    int ndims;
    hsize_t *dims;
    H5T_class_t dtype_class;
    hid_t dtype;
    hid_t dataset_id;
    size_t element_size;
    size_t total_elements;
    size_t uncompressed_size;
    size_t compressed_size;
    int is_chunked;
    int num_chunks;
    double compression_ratio;
    size_t chunk_min_size;
    size_t chunk_max_size;
    double chunk_mean_size;
} dataset_info_t;

typedef struct {
    hsize_t *offset;
    size_t compressed_size;
    size_t uncompressed_size;
} chunk_info_t;

void print_usage(const char *program_name) {
    printf("Usage: %s <hdf5_file>\n", program_name);
    printf("\n");
    printf("HDF5 Size Statistics Tool\n");
    printf("Analyzes compressed and uncompressed sizes of HDF5 datasets.\n");
    printf("\n");
    printf("Features:\n");
    printf("  - Shows compressed vs uncompressed sizes for each dataset\n");
    printf("  - Analyzes chunk-level compression (up to 50 chunks per variable)\n");
    printf("  - Calculates compression ratios and file proportions\n");
    printf("  - Categorizes variables by dimensionality\n");
    printf("\n");
    printf("Output:\n");
    printf("  Table format showing Variable, Compressed Size (MB), Original Size (MB),\n");
    printf("  Compression Ratio, and Proportion of total file size\n");
}

size_t get_hdf5_element_size(hid_t dtype) {
    return H5Tget_size(dtype);
}

int open_hdf5_file(const char *filename, hid_t *file_id) {
    *file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    return (*file_id < 0) ? -1 : 0;
}

herr_t dataset_counter(hid_t loc_id, const char *name, const H5L_info_t *info, void *operator_data) {
    hid_t obj_id;
    H5O_info2_t obj_info;
    int *count = (int*)operator_data;
    (void)info;
    
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
    return H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, dataset_counter, ndatasets);
}

typedef struct {
    dataset_info_t *datasets;
    int count;
    int capacity;
} dataset_collector_t;

int calculate_chunk_sizes(dataset_info_t *dataset) {
    hid_t plist_id = H5Dget_create_plist(dataset->dataset_id);
    if (plist_id < 0) return -1;
    
    H5D_layout_t layout = H5Pget_layout(plist_id);
    dataset->is_chunked = (layout == H5D_CHUNKED);
    
    if (!dataset->is_chunked) {
        H5Pclose(plist_id);
        dataset->compressed_size = H5Dget_storage_size(dataset->dataset_id);
        dataset->num_chunks = 0;
        dataset->chunk_min_size = 0;
        dataset->chunk_max_size = 0;
        dataset->chunk_mean_size = 0.0;
        return 0;
    }
    
    hsize_t chunk_dims[dataset->ndims];
    if (H5Pget_chunk(plist_id, dataset->ndims, chunk_dims) < 0) {
        H5Pclose(plist_id);
        return -1;
    }
    H5Pclose(plist_id);
    
    hid_t space_id = H5Dget_space(dataset->dataset_id);
    if (space_id < 0) return -1;
    
    hsize_t num_chunks;
    if (H5Dget_num_chunks(dataset->dataset_id, space_id, &num_chunks) < 0) {
        H5Sclose(space_id);
        return -1;
    }
    H5Sclose(space_id);
    
    dataset->num_chunks = (int)num_chunks;
    
    /* Get total compressed size for the entire variable */
    dataset->compressed_size = H5Dget_storage_size(dataset->dataset_id);
    
    /* Calculate min/max/mean for ALL chunks */
    dataset->chunk_min_size = SIZE_MAX;
    dataset->chunk_max_size = 0;
    double chunk_sum = 0.0;
    int valid_chunks = 0;
    
    /* Only iterate through chunks if dataset is actually chunked */
    if (dataset->is_chunked && dataset->num_chunks > 0) {
        for (int i = 0; i < dataset->num_chunks; i++) {
            hsize_t offset[dataset->ndims];
            unsigned filter_mask;
            haddr_t addr;
            hsize_t size;
            
            if (H5Dget_chunk_info(dataset->dataset_id, H5S_ALL, i, offset, &filter_mask, &addr, &size) >= 0) {
                hsize_t chunk_compressed_size;
                if (H5Dget_chunk_storage_size(dataset->dataset_id, offset, &chunk_compressed_size) >= 0) {
                    if (chunk_compressed_size < dataset->chunk_min_size) {
                        dataset->chunk_min_size = chunk_compressed_size;
                    }
                    if (chunk_compressed_size > dataset->chunk_max_size) {
                        dataset->chunk_max_size = chunk_compressed_size;
                    }
                    chunk_sum += chunk_compressed_size;
                    valid_chunks++;
                }
            }
        }
    }
    
    if (valid_chunks > 0) {
        dataset->chunk_mean_size = chunk_sum / valid_chunks;
    } else {
        dataset->chunk_min_size = 0;
        dataset->chunk_max_size = 0;
        dataset->chunk_mean_size = 0.0;
    }
    
    return 0;
}

herr_t collect_datasets(hid_t loc_id, const char *name, const H5L_info_t *info, void *operator_data) {
    dataset_collector_t *collector = (dataset_collector_t*)operator_data;
    hid_t obj_id, space_id, type_id;
    H5O_info2_t obj_info;
    (void)info;
    
    if (collector->count >= collector->capacity) return 0;
    
    obj_id = H5Oopen(loc_id, name, H5P_DEFAULT);
    if (obj_id < 0) return 0;
    
    if (H5Oget_info3(obj_id, &obj_info, H5O_INFO_BASIC) >= 0 && obj_info.type == H5O_TYPE_DATASET) {
        dataset_info_t *dataset = &collector->datasets[collector->count];
        
        strncpy(dataset->name, name, MAX_VAR_NAME - 1);
        dataset->name[MAX_VAR_NAME - 1] = '\0';
        
        space_id = H5Dget_space(obj_id);
        if (space_id >= 0) {
            dataset->ndims = H5Sget_simple_extent_ndims(space_id);
            if (dataset->ndims > 0) {
                dataset->dims = malloc(dataset->ndims * sizeof(hsize_t));
                H5Sget_simple_extent_dims(space_id, dataset->dims, NULL);
                
                dataset->total_elements = 1;
                for (int i = 0; i < dataset->ndims; i++) {
                    dataset->total_elements *= dataset->dims[i];
                }
            }
            H5Sclose(space_id);
        }
        
        type_id = H5Dget_type(obj_id);
        if (type_id >= 0) {
            dataset->dtype_class = H5Tget_class(type_id);
            dataset->dtype = H5Tcopy(type_id);
            dataset->element_size = get_hdf5_element_size(type_id);
            H5Tclose(type_id);
        }
        
        dataset->dataset_id = obj_id;
        dataset->uncompressed_size = dataset->total_elements * dataset->element_size;
        
        if (calculate_chunk_sizes(dataset) == 0) {
            if (dataset->uncompressed_size > 0) {
                dataset->compression_ratio = (double)dataset->uncompressed_size / dataset->compressed_size;
            } else {
                dataset->compression_ratio = 0.0;
            }
            collector->count++;
        } else {
            H5Dclose(obj_id);
            if (dataset->dims) free(dataset->dims);
            if (dataset->dtype >= 0) H5Tclose(dataset->dtype);
        }
    } else {
        H5Oclose(obj_id);
    }
    
    return 0;
}

int get_all_datasets(hid_t file_id, dataset_info_t **datasets, int *ndatasets) {
    if (get_dataset_count(file_id, ndatasets) < 0) return -1;
    if (*ndatasets == 0) {
        *datasets = NULL;
        return 0;
    }
    
    dataset_collector_t collector;
    collector.datasets = malloc(*ndatasets * sizeof(dataset_info_t));
    collector.count = 0;
    collector.capacity = *ndatasets;
    
    if (H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, collect_datasets, &collector) < 0) {
        free(collector.datasets);
        return -1;
    }
    
    *datasets = collector.datasets;
    *ndatasets = collector.count;
    return 0;
}

void categorize_datasets(dataset_info_t *datasets, int ndatasets, 
                        dataset_info_t ***coord_vars, int *n_coord,
                        dataset_info_t ***data_2d, int *n_2d,
                        dataset_info_t ***data_nd, int *n_nd,
                        dataset_info_t ***other, int *n_other) {
    
    *coord_vars = malloc(ndatasets * sizeof(dataset_info_t*));
    *data_2d = malloc(ndatasets * sizeof(dataset_info_t*));
    *data_nd = malloc(ndatasets * sizeof(dataset_info_t*));
    *other = malloc(ndatasets * sizeof(dataset_info_t*));
    
    *n_coord = *n_2d = *n_nd = *n_other = 0;
    
    for (int i = 0; i < ndatasets; i++) {
        dataset_info_t *dataset = &datasets[i];
        /* TODO: process other 1D variables*/ 
        if (!strcmp(dataset->name, "latitude") || !strcmp(dataset->name, "longitude") ||
            !strcmp(dataset->name, "level") || strstr(dataset->name, "time") ||
            !strcmp(dataset->name, "plev") || !strcmp(dataset->name, "lev") ||
            !strcmp(dataset->name, "lat") || !strcmp(dataset->name, "lon")) {
            (*coord_vars)[(*n_coord)++] = dataset;
        } else if (dataset->ndims == 2) {
            (*data_2d)[(*n_2d)++] = dataset;
        } else if (dataset->ndims >= 3) {
            (*data_nd)[(*n_nd)++] = dataset;
        } else {
            (*other)[(*n_other)++] = dataset;
        }
    }
}

int compare_by_compressed_size(const void *a, const void *b) {
    dataset_info_t *da = *(dataset_info_t**)a;
    dataset_info_t *db = *(dataset_info_t**)b;
    if (da->compressed_size < db->compressed_size) return 1;
    if (da->compressed_size > db->compressed_size) return -1;
    return 0;
}

void format_size_mb(size_t bytes, char *output) {
    double mb = bytes / (1024.0 * 1024.0);
    if (mb < 0.01) {
        strcpy(output, "<0.01");
    } else {
        snprintf(output, 16, "%.2f", mb);
    }
}

void format_size_smart(size_t bytes, char *output) {
    double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        snprintf(output, 16, "%.2f MB", mb);
    } else {
        double kb = bytes / 1024.0;
        if (kb >= 1.0) {
            snprintf(output, 16, "%.2f KB", kb);
        } else {
            snprintf(output, 16, "%zu B", bytes);
        }
    }
}

void print_category(const char *title, dataset_info_t **datasets, int count, size_t total_file_size) {
    if (count == 0) return;
    
    qsort(datasets, count, sizeof(dataset_info_t*), compare_by_compressed_size);
    
    printf("%s:\n", title);
    for (int i = 0; i < count; i++) {
        dataset_info_t *dataset = datasets[i];
        char compressed_mb[16], uncompressed_mb[16];
        
        format_size_mb(dataset->compressed_size, compressed_mb);
        format_size_mb(dataset->uncompressed_size, uncompressed_mb);
        
        double proportion = (double)dataset->compressed_size / total_file_size * 100.0;
        
        printf("  %-45s %s%-15s %s%-15s %6.1fx%-9s %5.1f%%",
               dataset->name,
               "", compressed_mb,
               "", uncompressed_mb,
               dataset->compression_ratio, "",
               proportion);
        
        if (dataset->is_chunked && dataset->num_chunks > 1) {
            char min_size[32], max_size[32], mean_size[32];
            format_size_smart(dataset->chunk_min_size, min_size);
            format_size_smart(dataset->chunk_max_size, max_size);
            format_size_smart((size_t)dataset->chunk_mean_size, mean_size);
            
            printf(" (%d chunks: min=%s, max=%s, mean=%s)", 
                   dataset->num_chunks, min_size, max_size, mean_size);
        }
        printf("\n");
    }
    printf("\n");
}

void print_summary_table(dataset_info_t *datasets, int ndatasets, const char *filename) {
    dataset_info_t **coord_vars, **data_2d, **data_nd, **other;
    int n_coord, n_2d, n_nd, n_other;
    
    categorize_datasets(datasets, ndatasets, &coord_vars, &n_coord, 
                       &data_2d, &n_2d, &data_nd, &n_nd, &other, &n_other);
    
    size_t total_compressed = 0;
    size_t total_original = 0;
    for (int i = 0; i < ndatasets; i++) {
        total_compressed += datasets[i].compressed_size;
        total_original += datasets[i].uncompressed_size;
    }
    
    printf("\nHDF5 Size Analysis: %s\n", filename);
    printf("================================================================================================================================================================\n");
    printf("%-47s %-15s %-15s %-15s %-10s\n", 
           "Variable", "Compressed (MB)", "Original (MB)", "Compression %", "File %");
    printf("----------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
    
    print_category("3D+ Variables", data_nd, n_nd, total_compressed);
    print_category("2D Variables", data_2d, n_2d, total_compressed);
    print_category("Coordinate Variables", coord_vars, n_coord, total_compressed);
    print_category("Other Variables", other, n_other, total_compressed);
    
    char total_compressed_mb[16], total_original_mb[16];
    format_size_mb(total_compressed, total_compressed_mb);
    format_size_mb(total_original, total_original_mb);
    double overall_compression_ratio = (total_original > 0) ? (double)total_original / total_compressed : 0.0;
    
    printf("----------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
    printf("%-40s %-15s MB\n", "TOTAL COMPRESSED SIZE:", total_compressed_mb);
    printf("%-40s %-15s MB\n", "TOTAL ORIGINAL SIZE:", total_original_mb);
    printf("%-40s %.1fx\n", "COMPRESSION RATIO:", overall_compression_ratio);
    printf("================================================================================================================================================================\n");
    
    free(coord_vars);
    free(data_2d);
    free(data_nd);
    free(other);
}

void free_dataset_info(dataset_info_t *dataset) {
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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    const char *filename = argv[1];
    hid_t file_id = -1;
    dataset_info_t *datasets = NULL;
    int ndatasets = 0;
    
    printf("Loading HDF5 file: %s\n", filename);
    
    if (H5open() < 0) {
        fprintf(stderr, "Error: Cannot initialize HDF5 library\n");
        return EXIT_FAILURE;
    }
    
    if (open_hdf5_file(filename, &file_id) != 0) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        H5close();
        return EXIT_FAILURE;
    }
    
    if (get_all_datasets(file_id, &datasets, &ndatasets) != 0) {
        fprintf(stderr, "Error: Cannot get dataset information\n");
        H5Fclose(file_id);
        H5close();
        return EXIT_FAILURE;
    }
    
    printf("Found %d datasets\n", ndatasets);
    
    if (ndatasets > 0) {
        print_summary_table(datasets, ndatasets, filename);
        
        for (int i = 0; i < ndatasets; i++) {
            free_dataset_info(&datasets[i]);
        }
        free(datasets);
    }
    
    H5Fclose(file_id);
    H5close();
    
    return EXIT_SUCCESS;
}
