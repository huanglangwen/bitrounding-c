#!/usr/bin/env python3
"""
NetCDF Bit Rounding Tool (Python version)

Applies bitrounding to float variables in NetCDF files using xarray.
This is a Python implementation of the C version in src/netcdf_bit_rounding.c
"""

import sys
import argparse
import os
import numpy as np
import xarray as xr
from bit_rounding import analyze_and_get_nsb, bitround


def get_file_size(filepath):
    """Get file size in bytes"""
    try:
        return os.path.getsize(filepath)
    except OSError:
        return -1

def is_coordinate_variable(var_name, ds):
    """Check if variable is a coordinate variable"""
    return var_name in ds.coords

def contains_missing_values(data_array):
    """Check if data array contains NaN or fill values"""
    if np.isnan(data_array.values).any():
        return True
    
    # Check for fill values
    if hasattr(data_array, '_FillValue') and data_array._FillValue is not None:
        fill_val = data_array._FillValue
        if np.any(data_array.values == fill_val):
            return True
    
    return False

def process_float32_variable(var_name, data_array, inflevel, monotonic=False, exp_lambda=0.5):
    """Process a single float32 variable with bit rounding"""
    print(f"Variable {var_name}: ", end="", flush=True)
    
    # Ensure data is float32
    if data_array.dtype != np.float32:
        print("Skipping bitrounding (not float32)")
        return data_array, False
    
    # Get data as float32 numpy array
    data = data_array.values
    original_shape = data.shape
    
    # Get fill value
    missval = np.float32(-999.0)  # default
    if hasattr(data_array, '_FillValue') and data_array._FillValue is not None:
        missval = np.float32(data_array._FillValue)
    
    # Check for missing values
    if contains_missing_values(data_array):
        print("Skipping bitrounding (contains missing values or NaNs)")
        return data_array, False
    
    # Calculate dimensions for chunking strategy
    ndims = len(original_shape)
    total_size = data.size
    
    if ndims <= 2:
        # For 1D or 2D variables, process as single chunk
        data_flat = data.flatten()
        nsb = analyze_and_get_nsb(data_flat, inflevel, monotonic=monotonic, exp_lambda=exp_lambda)
        
        if nsb > 0 and nsb <= 23:
            bitround(nsb, data_flat, missval)
            print(f"NSB={nsb}")
            
            # Reshape back to original shape
            processed_data = data_flat.reshape(original_shape)
            
            # Create new data array with same attributes
            result = data_array.copy()
            result.values = processed_data
            return result, True
        else:
            print("NSB analysis failed or invalid")
            return data_array, False
    
    else:
        # For 3D+ variables, process chunk by chunk using last two dimensions
        # Calculate chunk size based on last two dimensions
        chunk_size = 1
        for i in range(ndims - 2, ndims):
            chunk_size *= original_shape[i]
        
        num_chunks = total_size // chunk_size
        print(f"chunk_size={chunk_size}, num_chunks={num_chunks}")
        
        # Flatten data for chunk processing
        data_flat = data.flatten()
        
        chunk_processed = 0
        min_nsb = 1000
        max_nsb = -1000
        
        # Process each chunk
        for chunk_idx in range(num_chunks):
            start_idx = chunk_idx * chunk_size
            end_idx = start_idx + chunk_size
            chunk_data = data_flat[start_idx:end_idx]
            
            nsb = analyze_and_get_nsb(chunk_data, inflevel, monotonic=monotonic, exp_lambda=exp_lambda)
            if nsb > 0 and nsb <= 23:
                bitround(nsb, chunk_data, missval)
                chunk_processed += 1
                
                if nsb < min_nsb:
                    min_nsb = nsb
                if nsb > max_nsb:
                    max_nsb = nsb
        
        if chunk_processed > 0:
            print(f"  Processed {chunk_processed}/{num_chunks} chunks, NSB min={min_nsb} max={max_nsb}")
            
            # Reshape back to original shape
            processed_data = data_flat.reshape(original_shape)
            
            # Create new data array with same attributes
            result = data_array.copy()
            result.values = processed_data
            return result, True
        else:
            print("  No chunks processed successfully")
            return data_array, False

def main():
    """Main function"""
    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description="NetCDF Bit Rounding Tool - Applies bitrounding to float variables in NetCDF files.",
        prog="python netcdf_bit_rounding.py"
    )
    
    parser.add_argument(
        'inflevel',
        type=float,
        help='Information level threshold (0.0-1.0, typically 0.9999)'
    )
    
    parser.add_argument(
        'input_file',
        help='Input NetCDF file'
    )
    
    parser.add_argument(
        'output_file', 
        help='Output NetCDF file'
    )
    
    parser.add_argument(
        '--complevel',
        type=int,
        default=0,
        choices=range(1, 10),
        help='Optional compression level (1-9), enables shuffle filter'
    )
    parser.add_argument(
        '--monotonic',
        action='store_true',
        help='Use monotonic gradient-based method for bit rounding'
    )
    parser.add_argument(
        '--exp_lambda',
        type=float,
        default=0.5,
        help='Exponential lambda for moving average in monotonic method (default=0.5)'
    )
    
    try:
        args = parser.parse_args()
    except SystemExit:
        return 1
    
    inflevel = args.inflevel
    input_file = args.input_file
    output_file = args.output_file
    compression_level = args.complevel
    
    if inflevel < 0.0 or inflevel > 1.0:
        print("Error: inflevel must be between 0.0 and 1.0")
        return 1
    
    # Print processing info
    if compression_level > 0:
        print(f"Processing: {input_file} -> {output_file} (inflevel={inflevel:.6f}, compression={compression_level}, shuffle=enabled)")
    else:
        print(f"Processing: {input_file} -> {output_file} (inflevel={inflevel:.6f})")
    
    try:
        # Load input NetCDF file
        print("Loading input file...")
        ds = xr.open_dataset(input_file, engine="h5netcdf")
        
        processed_vars = 0
        bitrounded_vars = 0
        
        # Process each data variable in place
        for var_name in ds.data_vars:
            var = ds[var_name]
            processed_vars += 1
            
            # Only process float32 variables
            if var.dtype == np.float32:
                # Skip coordinate variables
                if is_coordinate_variable(var_name, ds):
                    print(f"Variable {var_name}: Skipping bitrounding (coordinate variable)")
                    continue
                
               
                # Process the variable
                processed_var, was_bitrounded = process_float32_variable(var_name, ds[var_name], inflevel, args.monotonic, args.exp_lambda)
                
                # Update the dataset with processed variable in place
                ds[var_name] = processed_var
                
                if was_bitrounded:
                    bitrounded_vars += 1
            else:
                print(f"Variable {var_name}: Skipping (dtype={var.dtype}, only processing float32)")
        
        # Prepare encoding for compression if requested
        encoding = {}
        if compression_level > 0:
            for var_name in ds.data_vars:
                var = ds[var_name]
                # Apply chunking for 3D+ variables before processing
                if len(var.dims) >= 3:
                    # Create chunking dict based on last two dimensions
                    chunks = {}
                    for i, dim in enumerate(var.dims):
                        if i < len(var.dims) - 2:
                            # Set chunk size to 1 for all dimensions except last two
                            chunks[dim] = 1
                        else:
                            # Use full dimension size for last two dimensions
                            chunks[dim] = var.sizes[dim]
                    
                    #print(f"Variable {var_name}: Applying chunking {chunks}")
                    #ds[var_name] = var.chunk(chunks)
                if var.dtype == np.float32 and not is_coordinate_variable(var_name, ds):
                    encoding[var_name] = {
                        'zlib': True,
                        'complevel': compression_level,
                        'shuffle': True,
                        'chunksizes': tuple(chunks[dim] for dim in var.dims)
                    }
        
        # Save output NetCDF file
        print("Saving output file...")
        if encoding:
            ds.to_netcdf(output_file, encoding=encoding, engine="h5netcdf")
        else:
            ds.to_netcdf(output_file, engine="h5netcdf")
        
        # Close the dataset
        ds.close()
        
        # Calculate file sizes and compression ratio
        input_size = get_file_size(input_file)
        output_size = get_file_size(output_file)
        
        print("\nBitrounding complete:")
        print(f"  Processed variables: {processed_vars}")
        print(f"  Bitrounded variables: {bitrounded_vars}")
        print(f"  Output file: {output_file}")
        
        if input_size > 0 and output_size > 0:
            print(f"  Input file size: {input_size / (1024.0 * 1024.0):.2f} MB")
            print(f"  Output file size: {output_size / (1024.0 * 1024.0):.2f} MB")
            print(f"  Compression ratio: {input_size / output_size:.2f}:1")
        
        return 0
        
    except FileNotFoundError:
        print(f"Error: Cannot open input file '{input_file}'")
        return 1
    except Exception as e:
        print(f"Error: {str(e)}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
