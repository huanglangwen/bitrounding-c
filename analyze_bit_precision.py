#!/usr/bin/env python3
"""
NetCDF Bit Precision Analysis Script

This script loads a NetCDF file with xarray, iterates through all data variables,
and finds the largest non-zero bit position in each variable's values.
This helps determine the effective precision needed for compression.

Usage:
    python analyze_bit_precision.py <netcdf_file>
"""

import xarray as xr
import numpy as np
import sys
from pathlib import Path

def find_bit_pattern(data_array):
    """
    Find the bit pattern showing which bit positions are used in a data array.
    
    Args:
        data_array: xarray DataArray with numerical data
        
    Returns:
        str: Bit pattern string with '1' for used bits, '-' for unused bits
    """
    # Convert to numpy array and handle NaN/inf values
    data = data_array.values
    
    # Remove NaN and infinite values
    valid_data = data[np.isfinite(data)]
    
    if len(valid_data) == 0:
        return "(MSB) -------- -------- -------- -------- (LSB)"
    
    # Convert to integer representation for bit analysis
    # For float32, we'll look at the raw bits
    if data.dtype == np.float32:
        # Convert to 32-bit integer view
        int_data = valid_data.view(np.uint32)
    elif data.dtype == np.float64:
        # Convert to 64-bit integer view
        int_data = valid_data.view(np.uint64)
    else:
        # For integer types, use directly
        int_data = valid_data.astype(np.uint64)
    
    # Find the maximum value to determine highest bit position
    max_val = np.max(int_data)
    
    if max_val == 0:
        return "(MSB) -------- -------- -------- -------- (LSB)"
    
    # Determine bit width based on data type
    if data.dtype == np.float32:
        bit_width = 32
    elif data.dtype == np.float64:
        bit_width = 64
    else:
        bit_width = 64  # Default for integer types
    
    # Create bit pattern array - 1 means bit is used, 0 means not used
    bit_pattern = np.zeros(bit_width, dtype=bool)
    
    # Check each bit position using vectorized operations
    for bit_pos in range(bit_width):
        # Create mask for this bit position
        bit_mask = 1 << bit_pos
        # Check if any value has this bit set
        if np.any((int_data & bit_mask) != 0):
            bit_pattern[bit_pos] = True
    
    # Convert to string representation (MSB first)
    pattern_chars = []
    for i in range(bit_width-1, -1, -1):  # Start from MSB
        if bit_pattern[i]:
            pattern_chars.append('1')
        else:
            pattern_chars.append('-')
        
        # Add space every 8 bits (but not at the end)
        if i > 0 and (bit_width - i) % 8 == 0:
            pattern_chars.append(' ')
    
    pattern_str = ''.join(pattern_chars)
    return f"(MSB) {pattern_str} (LSB)"

def analyze_netcdf_precision(filepath):
    """
    Analyze bit precision requirements for all data variables in NetCDF file.
    
    Args:
        filepath: Path to NetCDF file
    """
    print(f"Loading NetCDF file: {filepath}")
    
    try:
        # Load the dataset
        ds = xr.open_dataset(filepath)
        
        print(f"Dataset contains {len(ds.data_vars)} data variables")
        print("-" * 120)
        print(f"{'Variable':<45} {'Shape':<20} {'Bit Pattern (MSB->LSB)':<50}")
        print("-" * 120)
        
        results = []
        
        # Iterate through all data variables
        for var_name in ds.data_vars:
            data_var = ds[var_name]
            
            # Get shape information
            shape_str = "x".join(map(str, data_var.shape))
            
            # Check if variable has more than 2 dimensions
            if len(data_var.shape) > 2:
                # For 3D+ variables, analyze each 2D slice
                print(f"{var_name} (3D+)")
                print(f"{'  Slice':<43} {shape_str:<20} {'Bit Pattern (MSB->LSB)':<50}")
                
                # Get the shape of the leading dimensions (all except last 2)
                leading_dims = data_var.shape[:-2]
                slice_shape = data_var.shape[-2:]  # Last 2 dimensions
                slice_shape_str = "x".join(map(str, slice_shape))
                
                # Generate all combinations of indices for leading dimensions
                import itertools
                for indices in itertools.product(*[range(dim) for dim in leading_dims]):
                    # Create slice for this combination
                    slice_data = data_var[indices]
                    
                    # Find bit pattern for this slice
                    bit_pattern = find_bit_pattern(slice_data)
                    
                    # Create slice identifier
                    slice_id = "[" + ",".join(map(str, indices)) + ",:,:]"
                    
                    print(f"  {slice_id:<43} {slice_shape_str:<20} {bit_pattern}")
                
                # Store overall variable info
                results.append({
                    'variable': var_name,
                    'shape': data_var.shape,
                    'dtype': str(data_var.dtype),
                    'is_multidimensional': True,
                    'total_elements': data_var.size
                })
                
            else:
                # For 2D or 1D variables, analyze the whole variable
                bit_pattern = find_bit_pattern(data_var)
                
                print(f"{var_name:<45} {shape_str:<20} {bit_pattern}")
                
                results.append({
                    'variable': var_name,
                    'shape': data_var.shape,
                    'dtype': str(data_var.dtype),
                    'bit_pattern': bit_pattern,
                    'is_multidimensional': False,
                    'total_elements': data_var.size
                })
        
        print("-" * 120)
        print(f"Analysis complete for {len(results)} variables")
        
        # Count multidimensional vs regular variables
        multi_vars = sum(1 for r in results if r.get('is_multidimensional', False))
        regular_vars = len(results) - multi_vars
        print(f"  {multi_vars} variables analyzed slice-by-slice (3D+)")
        print(f"  {regular_vars} variables analyzed as whole (≤2D)")
        
        # Summary statistics
        print(f"\nSummary:")
        print(f"  Bit patterns show which bit positions are used across all values")
        print(f"  '1' = at least one value uses this bit position")
        print(f"  '-' = no values use this bit position")
        print(f"  Pattern format: (MSB) xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx (LSB)")
        
        # Close the dataset
        ds.close()
        
        return results
        
    except Exception as e:
        print(f"Error processing NetCDF file: {e}")
        sys.exit(1)

def main():
    if len(sys.argv) != 2:
        print("Usage: python analyze_bit_precision.py <netcdf_file>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    if not Path(filepath).exists():
        print(f"Error: File {filepath} does not exist")
        sys.exit(1)
    
    results = analyze_netcdf_precision(filepath)
    
    print(f"\nBit precision analysis saved for {len(results)} variables")

if __name__ == "__main__":
    main()
