# NetCDF and HDF5 Bit Analysis Tools Makefile
# Requires NetCDF-C and HDF5 libraries (available through Spack)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99 -g
LDFLAGS = -static

# Spack integration
SPACK_ROOT = /capstor/scratch/cscs/lhuang/spack
SPACK_CONFIG = /capstor/scratch/cscs/lhuang/.spack
SPACK_ENV = source $(SPACK_ROOT)/share/spack/setup-env.sh && export SPACK_USER_CONFIG_PATH=$(SPACK_CONFIG)

# Try to get library flags from pkg-config if available
NETCDF_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists netcdf 2>/dev/null && pkg-config --cflags netcdf)
NETCDF_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists netcdf 2>/dev/null && pkg-config --libs netcdf)
HDF5_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists hdf5 2>/dev/null && pkg-config --cflags hdf5)
HDF5_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists hdf5 2>/dev/null && pkg-config --libs hdf5)
ZLIB_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists zlib 2>/dev/null && pkg-config --cflags zlib)
ZLIB_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists zlib 2>/dev/null && pkg-config --libs zlib)

# If pkg-config fails, use Spack
ifeq ($(NETCDF_CFLAGS),)
    NETCDF_CFLAGS := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --cflags netcdf 2>/dev/null || echo "-I$(shell $(SPACK_ENV) && spack location -i netcdf-c)/include")
endif

ifeq ($(NETCDF_LIBS),)
    NETCDF_LIBS := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --libs netcdf 2>/dev/null || echo "-L$(shell $(SPACK_ENV) && spack location -i netcdf-c)/lib -lnetcdf")
endif

ifeq ($(HDF5_CFLAGS),)
    HDF5_CFLAGS := $(shell $(SPACK_ENV) && spack load hdf5 && pkg-config --cflags hdf5 2>/dev/null || echo "-I$(shell $(SPACK_ENV) && spack location -i hdf5)/include")
endif

ifeq ($(HDF5_LIBS),)
    HDF5_LIBS := $(shell $(SPACK_ENV) && spack load hdf5 && pkg-config --libs hdf5 2>/dev/null || echo "-L$(shell $(SPACK_ENV) && spack location -i hdf5)/lib -lhdf5")
endif

ifeq ($(ZLIB_CFLAGS),)
    ZLIB_CFLAGS := $(shell $(SPACK_ENV) && spack load zlib && pkg-config --cflags zlib 2>/dev/null || echo "-I$(shell $(SPACK_ENV) && spack location -i zlib)/include")
endif

ifeq ($(ZLIB_LIBS),)
    ZLIB_LIBS := $(shell $(SPACK_ENV) && spack load zlib && pkg-config --libs zlib 2>/dev/null || echo "-L$(shell $(SPACK_ENV) && spack location -i zlib)/lib -lz")
endif

# Ensure ZLIB_LIBS always includes library path if it's missing
ifeq ($(findstring -L,$(ZLIB_LIBS)),)
    ZLIB_LIBS := -L$(shell $(SPACK_ENV) && spack location -i zlib)/lib $(ZLIB_LIBS)
endif

# Ensure HDF5_LIBS always includes -lhdf5 if it's missing
ifeq ($(findstring -lhdf5,$(HDF5_LIBS)),)
    HDF5_LIBS += -lhdf5_hl -lhdf5
endif

# Ensure HDF5_LIBS always includes -lhdf5_hl for NetCDF dimension scale support
ifeq ($(findstring -lhdf5_hl,$(HDF5_LIBS)),)
    HDF5_LIBS := $(subst -lhdf5,-lhdf5_hl -lhdf5,$(HDF5_LIBS))
endif

# Get library paths for runtime
NETCDF_LIBDIR := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --variable=libdir netcdf 2>/dev/null || echo "$(shell $(SPACK_ENV) && spack location -i netcdf-c)/lib")
HDF5_LIBDIR := $(shell $(SPACK_ENV) && spack load hdf5 && pkg-config --variable=libdir hdf5 2>/dev/null || echo "$(shell $(SPACK_ENV) && spack location -i hdf5)/lib")
ZLIB_LIBDIR := $(shell $(SPACK_ENV) && spack load zlib && pkg-config --variable=libdir zlib 2>/dev/null || echo "$(shell $(SPACK_ENV) && spack location -i zlib)/lib")

# Add RPATH to ensure runtime library location
LDFLAGS += -Wl,-rpath,$(NETCDF_LIBDIR) -Wl,-rpath,$(HDF5_LIBDIR) -Wl,-rpath,$(ZLIB_LIBDIR)

# Source files
SRCDIR = src
NETCDF_SOURCES = $(SRCDIR)/netcdf_bit_analysis.c $(SRCDIR)/bit_pattern.c
NETCDF_OBJECTS = $(NETCDF_SOURCES:.c=.o)
HDF5_SOURCES = $(SRCDIR)/hdf_bit_analysis.c $(SRCDIR)/bit_pattern.c
HDF5_OBJECTS = $(HDF5_SOURCES:.c=.o)
HDF5_SIZE_SOURCES = $(SRCDIR)/hdf_size_stat.c
HDF5_SIZE_OBJECTS = $(HDF5_SIZE_SOURCES:.c=.o)
HEADERS = $(SRCDIR)/bit_pattern.h

# Bitrounding source files
BITROUNDING_SOURCES = $(SRCDIR)/netcdf_bit_rounding.c $(SRCDIR)/bitrounding_stats.c $(SRCDIR)/bitrounding_bitinfo.c
BITROUNDING_OBJECTS = $(BITROUNDING_SOURCES:.c=.o)
BITROUNDING_HEADERS = $(SRCDIR)/bitrounding_stats.h $(SRCDIR)/bitrounding_bitinfo.h

# Target executables
NETCDF_TARGET = netcdf_bit_analysis
HDF5_TARGET = hdf_bit_analysis
HDF5_SIZE_TARGET = hdf_size_stat
BITROUNDING_TARGET = netcdf_bit_rounding

# Build rules
all: $(NETCDF_TARGET) $(HDF5_TARGET) $(HDF5_SIZE_TARGET) $(BITROUNDING_TARGET)

$(NETCDF_TARGET): $(NETCDF_OBJECTS)
	@echo "Linking $(NETCDF_TARGET)..."
	$(CC) $(NETCDF_OBJECTS) -o $@ $(LDFLAGS) $(NETCDF_LIBS) $(HDF5_LIBS) $(ZLIB_LIBS) -lm -ldl
	@echo "Build complete: $(NETCDF_TARGET)"

$(HDF5_TARGET): $(HDF5_OBJECTS)
	@echo "Linking $(HDF5_TARGET)..."
	$(CC) $(HDF5_OBJECTS) -o $@ $(LDFLAGS) $(HDF5_LIBS) $(ZLIB_LIBS) -lm -ldl
	@echo "Build complete: $(HDF5_TARGET)"

$(HDF5_SIZE_TARGET): $(HDF5_SIZE_OBJECTS)
	@echo "Linking $(HDF5_SIZE_TARGET)..."
	$(CC) $(HDF5_SIZE_OBJECTS) -o $@ $(LDFLAGS) $(HDF5_LIBS) $(ZLIB_LIBS) -lm -ldl
	@echo "Build complete: $(HDF5_SIZE_TARGET)"

$(BITROUNDING_TARGET): $(BITROUNDING_OBJECTS)
	@echo "Linking $(BITROUNDING_TARGET)..."
	$(CC) $(BITROUNDING_OBJECTS) -o $@ $(LDFLAGS) $(NETCDF_LIBS) $(HDF5_LIBS) $(ZLIB_LIBS) -lm -ldl
	@echo "Build complete: $(BITROUNDING_TARGET)"

# NetCDF objects
$(SRCDIR)/netcdf_bit_analysis.o: $(SRCDIR)/netcdf_bit_analysis.c $(HEADERS)
	@echo "Compiling $< (NetCDF)..."
	$(CC) $(CFLAGS) $(NETCDF_CFLAGS) -c $< -o $@

# HDF5 objects  
$(SRCDIR)/hdf_bit_analysis.o: $(SRCDIR)/hdf_bit_analysis.c $(HEADERS)
	@echo "Compiling $< (HDF5)..."
	$(CC) $(CFLAGS) $(HDF5_CFLAGS) -c $< -o $@

$(SRCDIR)/hdf_size_stat.o: $(SRCDIR)/hdf_size_stat.c
	@echo "Compiling $< (HDF5 Size Stats)..."
	$(CC) $(CFLAGS) $(HDF5_CFLAGS) -c $< -o $@

# Generic bit_pattern object (used by both)
$(SRCDIR)/bit_pattern.o: $(SRCDIR)/bit_pattern.c $(HEADERS)
	@echo "Compiling $< (generic)..."
	$(CC) $(CFLAGS) -c $< -o $@

# Bitrounding objects
%.o: %.c $(HEADERS) $(BITROUNDING_HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(NETCDF_CFLAGS) -c $< -o $@

# Spack-specific build target
build-spack: setup-env all

setup-env:
	@echo "Setting up Spack environment..."
	@$(SPACK_ENV) && spack load netcdf-c && spack load hdf5 && echo "NetCDF and HDF5 loaded successfully"

# Test build with verbose output
test-build: CFLAGS += -DDEBUG -v
test-build: clean all

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Release build (default)
release: CFLAGS += -DNDEBUG -O3
release: clean all

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(NETCDF_OBJECTS) $(HDF5_OBJECTS) $(HDF5_SIZE_OBJECTS) $(BITROUNDING_OBJECTS) $(NETCDF_TARGET) $(HDF5_TARGET) $(HDF5_SIZE_TARGET) $(BITROUNDING_TARGET)
	@echo "Clean complete"

# Install to local bin (optional)
install: $(NETCDF_TARGET) $(HDF5_TARGET) $(HDF5_SIZE_TARGET)
	@echo "Installing tools to /usr/local/bin..."
	sudo cp $(NETCDF_TARGET) $(HDF5_TARGET) $(HDF5_SIZE_TARGET) /usr/local/bin/
	@echo "Installation complete"

# Create example output
example: $(NETCDF_TARGET)
	@echo "Running example analysis..."
	./$(NETCDF_TARGET) /capstor/scratch/cscs/lhuang/CompressionBenchmark_data/energy_flux/P2016-1020_09_chunked_cdotest_nccopy_compress.nc > examples/sample_output.txt
	@echo "Example output saved to examples/sample_output.txt"

# Check library installations
check-deps:
	@echo "Checking library installations..."
	@$(SPACK_ENV) && spack load netcdf-c && nc-config --version 2>/dev/null || echo "NetCDF not found via Spack"
	@$(SPACK_ENV) && spack load hdf5 && h5cc --version 2>/dev/null || echo "HDF5 not found via Spack"
	@pkg-config --exists netcdf && echo "NetCDF found via pkg-config" || echo "NetCDF not found via pkg-config"
	@pkg-config --exists hdf5 && echo "HDF5 found via pkg-config" || echo "HDF5 not found via pkg-config"
	@echo "NetCDF CFLAGS: $(NETCDF_CFLAGS)"
	@echo "NetCDF LIBS: $(NETCDF_LIBS)"
	@echo "HDF5 CFLAGS: $(HDF5_CFLAGS)"
	@echo "HDF5 LIBS: $(HDF5_LIBS)"

# Print make variables for debugging
print-vars:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "NETCDF_CFLAGS: $(NETCDF_CFLAGS)"
	@echo "NETCDF_LIBS: $(NETCDF_LIBS)"
	@echo "HDF5_CFLAGS: $(HDF5_CFLAGS)"
	@echo "HDF5_LIBS: $(HDF5_LIBS)"
	@echo "ZLIB_CFLAGS: $(ZLIB_CFLAGS)"
	@echo "ZLIB_LIBS: $(ZLIB_LIBS)"
	@echo "NETCDF_SOURCES: $(NETCDF_SOURCES)"
	@echo "HDF5_SOURCES: $(HDF5_SOURCES)"
	@echo "NETCDF_OBJECTS: $(NETCDF_OBJECTS)"
	@echo "HDF5_OBJECTS: $(HDF5_OBJECTS)"
	@echo "NETCDF_TARGET: $(NETCDF_TARGET)"
	@echo "HDF5_TARGET: $(HDF5_TARGET)"

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build both NetCDF and HDF5 bit analysis tools (default)"
	@echo "  build-spack  - Build with explicit Spack environment setup"
	@echo "  debug        - Build with debug flags"
	@echo "  release      - Build optimized release version"
	@echo "  test-build   - Build with verbose debugging"
	@echo "  clean        - Remove build artifacts"
	@echo "  install      - Install tools to /usr/local/bin (requires sudo)"
	@echo "  example      - Run example analysis and save output"
	@echo "  check-deps   - Check NetCDF and HDF5 library installations"
	@echo "  print-vars   - Print make variables for debugging"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Individual targets:"
	@echo "  $(NETCDF_TARGET) - Build NetCDF bit analysis tool only"
	@echo "  $(HDF5_TARGET)   - Build HDF5 bit analysis tool only"
	@echo "  $(HDF5_SIZE_TARGET) - Build HDF5 size statistics tool only"

# SLURM integration targets
srun-build:
	srun -A a122 -t 1:00:00 --mem=8000 make build-spack

srun-test:
	srun -A a122 -t 2:00:00 --mem=16000 make example

# Declare phony targets
.PHONY: all build-spack setup-env test-build debug release clean install example check-deps print-vars help srun-build srun-test

# Default target
.DEFAULT_GOAL := all
