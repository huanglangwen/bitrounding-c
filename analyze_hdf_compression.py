#!/usr/bin/env python3
"""
NetCDF Compression Analysis Script

This script analyzes NetCDF files compressed with HDF5 filters using h5ls,
extracts storage information, and generates a summary table of disk space
usage by variable.

Usage:
    python analyze_netcdf_compression.py <netcdf_file>
"""

import subprocess
import sys
import re
from pathlib import Path

def run_h5ls(filepath):
    """Run h5ls -rv on the NetCDF file and return output."""
    try:
        # Need to source Spack environment first
        cmd = [
            'bash', '-c', 
            f'source /capstor/scratch/cscs/lhuang/cdo/spack/share/spack/setup-env.sh && '
            f'export SPACK_USER_CONFIG_PATH=/capstor/scratch/cscs/lhuang/cdo/.spack && '
            f'spack load hdf5 && h5ls -rv "{filepath}"'
        ]
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        return result.stdout.decode('utf-8')
    except subprocess.CalledProcessError as e:
        print(f"Error running h5ls: {e}")
        print(f"stderr: {e.stderr}")
        sys.exit(1)

def parse_h5ls_output(output):
    """Parse h5ls output to extract variable storage information."""
    variables = []
    current_var = None
    
    lines = output.split('\n')
    
    for line in lines:
        line = line.strip()
        
        # Look for dataset declarations
        if line.startswith('/') and 'Dataset' in line:
            # Extract variable name and dimensions
            parts = line.split()
            var_name = parts[0][1:]  # Remove leading '/'
            if '{' in line:
                dims_str = line.split('{')[1].split('}')[0]
                dimensions = dims_str.replace('/', '')
            else:
                dimensions = ""
            
            current_var = {
                'name': var_name,
                'dimensions': dimensions,
                'logical_bytes': 0,
                'allocated_bytes': 0,
                'utilization': 0
            }
        
        # Look for storage information
        elif line.startswith('Storage:') and current_var:
            # Parse: "Storage: 153659520 logical bytes, 63643236 allocated bytes, 241.44% utilization"
            storage_match = re.search(r'(\d+) logical bytes, (\d+) allocated bytes, ([\d.]+)% utilization', line)
            if storage_match:
                current_var['logical_bytes'] = int(storage_match.group(1))
                current_var['allocated_bytes'] = int(storage_match.group(2))
                current_var['utilization'] = float(storage_match.group(3))
                variables.append(current_var)
                current_var = None
    
    return variables

def format_bytes_to_mb(bytes_val):
    """Convert bytes to MB with appropriate formatting."""
    mb = bytes_val / (1024 * 1024)
    if mb < 0.01:
        return "<0.01"
    else:
        return f"{mb:.2f}"

def categorize_variables(variables):
    """Categorize variables by type for better organization."""
    coord_vars = []
    data_2d = []
    data_3d = []
    other = []
    
    for var in variables:
        name = var['name']
        dims = var['dimensions']
        
        if name in ['latitude', 'longitude', 'level', 'time']:
            coord_vars.append(var)
        elif ',' in dims and dims.count(',') == 1:  # 2D data
            data_2d.append(var)
        elif ',' in dims and dims.count(',') == 2:  # 3D data
            data_3d.append(var)
        else:
            other.append(var)
    
    return coord_vars, data_2d, data_3d, other

def print_summary_table(variables, filepath):
    """Print a formatted summary table."""
    coord_vars, data_2d, data_3d, other = categorize_variables(variables)
    
    # Sort by allocated bytes (descending)
    data_3d.sort(key=lambda x: x['allocated_bytes'], reverse=True)
    data_2d.sort(key=lambda x: x['allocated_bytes'], reverse=True)
    
    print(f"\nNetCDF Compression Analysis: {Path(filepath).name}")
    print("=" * 80)
    print(f"{'Variable':<40} {'Allocated (MB)':<15} {'Logical (MB)':<15} {'Compression %':<15}")
    print("-" * 80)
    
    total_allocated = 0
    
    if data_3d:
        print("3D Variables:")
        for var in data_3d:
            allocated_mb = format_bytes_to_mb(var['allocated_bytes'])
            logical_mb = format_bytes_to_mb(var['logical_bytes'])
            print(f"  {var['name']:<38} {allocated_mb:<15} {logical_mb:<15} {var['utilization']:<15.1f}")
            total_allocated += var['allocated_bytes']
        print()
    
    if data_2d:
        print("2D Variables:")
        for var in data_2d:
            allocated_mb = format_bytes_to_mb(var['allocated_bytes'])
            logical_mb = format_bytes_to_mb(var['logical_bytes'])
            print(f"  {var['name']:<38} {allocated_mb:<15} {logical_mb:<15} {var['utilization']:<15.1f}")
            total_allocated += var['allocated_bytes']
        print()
    
    if coord_vars:
        print("Coordinate Variables:")
        for var in coord_vars:
            allocated_mb = format_bytes_to_mb(var['allocated_bytes'])
            logical_mb = format_bytes_to_mb(var['logical_bytes'])
            print(f"  {var['name']:<38} {allocated_mb:<15} {logical_mb:<15} {var['utilization']:<15.1f}")
            total_allocated += var['allocated_bytes']
        print()
    
    if other:
        print("Other Variables:")
        for var in other:
            allocated_mb = format_bytes_to_mb(var['allocated_bytes'])
            logical_mb = format_bytes_to_mb(var['logical_bytes'])
            print(f"  {var['name']:<38} {allocated_mb:<15} {logical_mb:<15} {var['utilization']:<15.1f}")
            total_allocated += var['allocated_bytes']
        print()
    
    print("-" * 80)
    print(f"{'TOTAL COMPRESSED SIZE:':<40} {format_bytes_to_mb(total_allocated):<15} MB")
    print("=" * 80)

def main():
    if len(sys.argv) != 2:
        print("Usage: python analyze_netcdf_compression.py <netcdf_file>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    if not Path(filepath).exists():
        print(f"Error: File {filepath} does not exist")
        sys.exit(1)
    
    print(f"Analyzing NetCDF file: {filepath}")
    print("Running h5ls -rv...")
    
    output = run_h5ls(filepath)
    variables = parse_h5ls_output(output)
    
    if not variables:
        print("No variables with storage information found")
        sys.exit(1)
    
    print_summary_table(variables, filepath)

if __name__ == "__main__":
    main()