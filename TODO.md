# TODO List

## Planned Improvements and Features

### 1. Statistical Functions Enhancement
- [x] Replace normal_inv implementation with Acklam approach
  - ~~Current implementation uses Newton-Raphson method~~
  - ~~Acklam approach may provide better numerical stability and performance~~
  - **COMPLETED**: Acklam implementation now active, original preserved as `normal_inv_original()`

### 2. Bitrounding Tool Enhancements
- [ ] Add `--chunksize` option to bitrounding
  - Allow user-specified chunk sizes instead of automatic last-two-dimensions
  - Format: `--chunksize=dim1,dim2,dim3` or similar
  - Validate chunk sizes against variable dimensions

### 3. NetCDF Bit Analysis Improvements
- [ ] Collect variable-wise storage statistics in netcdf_bit_analysis.c
  - Occupied size (actual storage)
  - Original size (uncompressed)
  - Compression ratio per variable
  - Proportion of total file size
  - Output in tabular format for easy analysis

### 4. Code Organization
- [x] Rename bitrounding.c to netcdf_bit_rounding.c
  - ~~Better alignment with naming conventions~~
  - ~~More descriptive filename~~
  - ~~Update Makefile accordingly~~
  - **COMPLETED**: Tool renamed to `netcdf_bit_rounding`, Makefile and README updated

### 5. New Tool Development
- [ ] Add netcdf_bit_info.c 
  - Print bit information sequence for each variable
  - Support 2D slice analysis for multidimensional variables
  - Optional CLI curve/graph output for bit patterns
  - Complement existing bit analysis tools with detailed bit-level statistics

## Implementation Notes

### Priority Order
1. Code organization (rename files)
2. Storage statistics collection
3. Chunk size options
4. Bit info tool development
5. Statistical function improvements

### Technical Considerations
- Maintain backward compatibility where possible
- Follow existing code style and patterns
- Add comprehensive documentation for new features
- Include example usage in README updates