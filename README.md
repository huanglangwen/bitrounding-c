# NetCDF and HDF5 Analysis Tools

A collection of tools for analyzing NetCDF/HDF5 files, compression characteristics, bit usage patterns, and applying bitrounding compression. Designed for HPC environments with SLURM and Spack.

## Tools Overview

### 1. `netcdf_bit_rounding` (C Implementation)
High-performance C implementation of bitrounding algorithm for NetCDF files with compression support.

**Features:**
- Bitrounding algorithm ported from CDO with exact statistical functions
- Chunked processing for 3D+ variables using last two dimensions
- Missing value and NaN handling
- Optional compression with shuffle filter (`--complevel=1-9`)
- File size and compression ratio reporting

### 2. `netcdf_bit_analysis` (C Implementation)
Fast C implementation for analyzing bit usage patterns in NetCDF variables.

**Features:**
- Shows bit patterns for each variable (MSB to LSB)
- Analyzes 3D+ variables slice-by-slice (each 2D slice)

### 3. `hdf_bit_analysis` (C Implementation)
Fast C implementation for analyzing bit usage patterns in HDF5 datasets.

**Features:**
- Shows bit patterns for each dataset (MSB to LSB)
- Analyzes 3D+ datasets slice-by-slice (each 2D slice)
- Direct HDF5 library interface for maximum performance

### 4. `hdf_size_stat` (C Implementation)
Comprehensive HDF5 compression analysis tool for detailed size statistics.

**Features:**
- Shows compressed vs uncompressed sizes for each dataset
- Chunk-level compression analysis with min/max/mean statistics
- Categorizes variables by dimensionality (3D+, 2D, coordinates)
- Calculates compression ratios and file proportions
- Handles both chunked and non-chunked datasets

### 5. `analyze_hdf_compression.py`
Python-based HDF5 compression statistics analyzer using `h5ls`.

**Features:**
- Extracts storage information for each variable
- Categorizes variables by dimensionality (3D, 2D, coordinates)
- Shows disk space usage and compression ratios
- Formats output in a clean table

### 6. `analyze_bit_precision.py`
Python implementation for analyzing bit usage patterns in NetCDF variables.

**Features:**
- Shows bit patterns for each variable (MSB to LSB)
- Analyzes 3D+ variables slice-by-slice (each 2D slice)
- Uses vectorized operations for efficiency

## Prerequisites

- Python 3.6+
- xarray, numpy (available in `/code/venv/bin/activate`)
- NetCDF-C library (available via Spack)
- HDF5 tools (available via Spack)
- SLURM environment for compute jobs
- GCC compiler for building C tools

## Setup

### Building C Tools
```bash
# Source Spack and set config path
source /capstor/scratch/cscs/lhuang/cdo/spack/share/spack/setup-env.sh
export SPACK_USER_CONFIG_PATH=/capstor/scratch/cscs/lhuang/cdo/.spack

# Load required tools
spack load netcdf-c

# Build all tools
make all
```

### Spack Environment
```bash
# Source Spack and set config path
source /capstor/scratch/cscs/lhuang/cdo/spack/share/spack/setup-env.sh
export SPACK_USER_CONFIG_PATH=/capstor/scratch/cscs/lhuang/cdo/.spack

# Load required tools
spack load hdf5
spack load netcdf-c
```

### Python Environment
```bash
# Activate Python virtual environment (for compute nodes)
source /code/venv/bin/activate
```

## Usage

### NetCDF Bit Rounding (C Implementation)

```bash
# Basic usage
./netcdf_bit_rounding <inflevel> <input.nc> <output.nc>

# With compression
./netcdf_bit_rounding <inflevel> <input.nc> <output.nc> --complevel=9

# Examples
./netcdf_bit_rounding 0.99 input.nc output.nc
./netcdf_bit_rounding 0.9999 input.nc compressed_output.nc --complevel=6
```

**Parameters:**
- `inflevel`: Information level threshold (0.0-1.0, typically 0.99-0.9999)
- `--complevel`: Optional compression level (1-9), automatically enables shuffle filter

### NetCDF Bit Analysis (C Implementation)

```bash
# Analyze bit patterns
./netcdf_bit_analysis <netcdf_file>

# Example
./netcdf_bit_analysis data.nc
```

### HDF5 Size Analysis (C Implementation)

```bash
# Fast C implementation for detailed compression analysis
./hdf_size_stat <hdf5_or_netcdf_file>

# Examples
./hdf_size_stat data.nc
./hdf_size_stat data.h5
```

### HDF5 Bit Analysis (C Implementation)

```bash
# Analyze bit patterns in HDF5 datasets
./hdf_bit_analysis <hdf5_or_netcdf_file>

# Example
./hdf_bit_analysis data.nc
```

### Compression Analysis (Python)

```bash
# On login node (lightweight operation)
python3 analyze_hdf_compression.py <netcdf_file>

# Example
python3 analyze_hdf_compression.py /path/to/compressed_file.nc
```

### Bit Precision Analysis

```bash
# On compute node (memory and CPU intensive)
srun --environment=compbench -A a122 -t 4:00:00 --mem=460000 bash -c \
  "source /code/venv/bin/activate && python3 analyze_bit_precision.py <netcdf_file>"

# Example
srun --environment=compbench -A a122 -t 4:00:00 --mem=460000 bash -c \
  "source /code/venv/bin/activate && python3 analyze_bit_precision.py /path/to/file.nc"
```

## Examples and Expected Results

### HDF5 Size Analysis Example

**Command:**
```bash
./hdf_size_stat P2016-1015_19_br.nc
```

**Expected Output:**
```
HDF5 Size Analysis: P2016-1015_19_br.nc
================================================================================================================================================================
Variable                                        Compressed (MB) Original (MB)   Compression %   File %    
----------------------------------------------------------------------------------------------------------------------------------------------------------------
3D+ Variables:
  v_component_of_wind                           11.81           146.54            12.4x           26.9% (37 chunks: min=156.43 KB, max=2.03 MB, mean=326.96 KB)
  temperature                                   11.03           146.54            13.3x           25.2% (37 chunks: min=204.16 KB, max=1.22 MB, mean=305.24 KB)
  specific_humidity                             10.80           146.54            13.6x           24.6% (37 chunks: min=85.60 KB, max=1.94 MB, mean=298.94 KB)
  geopotential                                  8.60            146.54            17.0x           19.6% (37 chunks: min=114.55 KB, max=1.25 MB, mean=237.93 KB)

2D Variables:
  top_net_thermal_radiation                     0.32            3.96              12.3x            0.7%
  mean_surface_sensible_heat_flux               0.32            3.96              12.5x            0.7%
  mean_surface_latent_heat_flux                 0.26            3.96              15.0x            0.6%
  mean_surface_net_long_wave_radiation_flux     0.23            3.96              17.3x            0.5%
  top_net_solar_radiation                       0.18            3.96              22.0x            0.4%
  mean_surface_net_short_wave_radiation_flux    0.17            3.96              24.0x            0.4%
  toa_incident_solar_radiation                  0.12            3.96              33.3x            0.3%

Coordinate Variables:
  longitude                                     <0.01           <0.01              6.1x            0.0%
  latitude                                      <0.01           <0.01              3.6x            0.0%
  level                                         <0.01           <0.01              4.9x            0.0%
  time                                          <0.01           <0.01              0.0x            0.0%

----------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL COMPRESSED SIZE:                   43.84           MB
================================================================================================================================================================
```

### Bit Precision Analysis Example

**Command:**
```bash
srun --environment=compbench -A a122 -t 4:00:00 --mem=460000 bash -c \
  "source /code/venv/bin/activate && python3 analyze_bit_precision.py P2016-1020_09_chunked_cdotest_nccopy_compress.nc"
```

**Expected Output (abbreviated):**
```
Loading NetCDF file: P2016-1020_09_chunked_cdotest_nccopy_compress.nc
Dataset contains 11 data variables
------------------------------------------------------------------------------------------------------------------------
Variable                                      Shape                Bit Pattern (MSB->LSB)                            
------------------------------------------------------------------------------------------------------------------------
temperature (3D+)
  Slice                                     37x721x1440          Bit Pattern (MSB->LSB)                            
  [0,:,:]                                     721x1440             (MSB) -1----11 11111111 11111111 11111111 (LSB)
  [1,:,:]                                     721x1440             (MSB) -1----11 11111111 11111111 11111111 (LSB)
  [6,:,:]                                     721x1440             (MSB) -1----11 -1111111 11111111 11111111 (LSB)
  ...

specific_humidity (3D+)
  Slice                                     37x721x1440          Bit Pattern (MSB->LSB)                            
  [0,:,:]                                     721x1440             (MSB) --11-11- 11111111 11111111 11111111 (LSB)
  [7,:,:]                                     721x1440             (MSB) --11-11- -1111111 11111111 11111111 (LSB)
  [12,:,:]                                    721x1440             (MSB) --111111 11111111 11111111 11111111 (LSB)
  ...

mean_surface_net_long_wave_radiation_flux     721x1440             (MSB) 11111111 11111111 11111111 11111111 (LSB)
top_net_solar_radiation                       721x1440             (MSB) -1--1111 11111111 11111111 11111111 (LSB)
------------------------------------------------------------------------------------------------------------------------
Analysis complete for 11 variables
  4 variables analyzed slice-by-slice (3D+)
  7 variables analyzed as whole (≤2D)

Summary:
  Bit patterns show which bit positions are used across all values
  '1' = at least one value uses this bit position
  '-' = no values use this bit position
  Pattern format: (MSB) xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx (LSB)
```

## Understanding the Output

### HDF5 Size Analysis
- **Compressed (MB)**: Actual disk space used after compression
- **Original (MB)**: Uncompressed size calculated from dimensions
- **Compression %**: `uncompressed/compressed` ratio - higher is better compression
- **File %**: Percentage of total compressed file size
- **Chunk Statistics**: For chunked variables, shows min/max/mean compressed chunk sizes in KB/MB

### Bit Precision Analysis
- **Bit Pattern**: Shows which bit positions are used in the data
  - `1`: At least one value uses this bit position
  - `-`: No values use this bit position
  - Format: `(MSB) xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx (LSB)`
- **3D+ Variables**: Each 2D slice analyzed separately to show vertical stratification
- **Slice Notation**: `[level,:,:]` for atmospheric data

## Key Insights

### From Compression Analysis
1. **3D variables dominate storage** (82% of total size)
2. **Wind components least compressible** due to high variability
3. **Radiation variables highly compressible** (288-372% compression ratios)

### From Bit Precision Analysis
1. **Vertical stratification** in bit usage patterns
2. **Variable-specific precision** needs by atmospheric level
3. **Surface vs upper-level** data characteristics differ
4. **Limited bit-shaving potential** - most positions used

## Performance Notes

- **Compression analysis**: Fast, can run on login node
- **Bit precision analysis**: Memory intensive, requires compute node
- **3D slice analysis**: Scales with number of vertical levels
- **Large files**: May require increased memory allocation (`--mem` parameter)

## File Descriptions

- `analyze_netcdf_compression.py`: HDF5 compression statistics analyzer
- `analyze_bit_precision.py`: Bit usage pattern analyzer with slice-by-slice support
- `CLAUDE.md`: SLURM and environment setup documentation
- `README.md`: This documentation file

## SLURM Job Examples

### Interactive Analysis
```bash
# Start interactive session
srun --environment=compbench -A a122 -t 2:00:00 --mem=230000 --pty bash

# Activate environment and run analysis
source /code/venv/bin/activate
python3 analyze_bit_precision.py data.nc
```

### Batch Job
```bash
#!/bin/bash
#SBATCH --job-name=netcdf_analysis
#SBATCH --environment=compbench
#SBATCH --account=a122
#SBATCH --time=4:00:00
#SBATCH --mem=460000
#SBATCH --output=analysis_%j.out

source /code/venv/bin/activate
python3 analyze_bit_precision.py /path/to/netcdf/file.nc
```

## Troubleshooting

### Common Issues
1. **Module not found errors**: Ensure virtual environment is activated
2. **Out of memory**: Increase `--mem` parameter for large files
3. **Permission errors**: Check file permissions and paths
4. **Spack not found**: Source the Spack environment setup script

### Memory Requirements
- Small files (<1GB): 8GB memory sufficient
- Medium files (1-10GB): 16-32GB recommended  
- Large files (>10GB): 64GB+ may be needed

## Contributing

When making changes:
1. Test on sample data first
2. Update documentation and examples
3. Commit with descriptive messages
4. Include performance impact notes