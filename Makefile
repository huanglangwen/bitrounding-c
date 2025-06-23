# NetCDF Bit Analysis Tool Makefile
# Requires NetCDF-C library (available through Spack)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99 -g
LDFLAGS = -lnetcdf -lm

# Spack integration
SPACK_ROOT = /capstor/scratch/cscs/lhuang/cdo/spack
SPACK_CONFIG = /capstor/scratch/cscs/lhuang/cdo/.spack
SPACK_ENV = source $(SPACK_ROOT)/share/spack/setup-env.sh && export SPACK_USER_CONFIG_PATH=$(SPACK_CONFIG)

# Try to get NetCDF flags from pkg-config if available
NETCDF_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists netcdf 2>/dev/null && pkg-config --cflags netcdf)
NETCDF_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists netcdf 2>/dev/null && pkg-config --libs netcdf)

# If pkg-config fails, use Spack
ifeq ($(NETCDF_CFLAGS),)
    NETCDF_CFLAGS := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --cflags netcdf 2>/dev/null || echo "-I$(SPACK_ROOT)/opt/spack/linux-neoverse_v2/netcdf-c/include")
endif

ifeq ($(NETCDF_LIBS),)
    NETCDF_LIBS := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --libs netcdf 2>/dev/null || echo "-L$(SPACK_ROOT)/opt/spack/linux-neoverse_v2/netcdf-c/lib -lnetcdf")
endif

# Get NetCDF library path for runtime
NETCDF_LIBDIR := $(shell $(SPACK_ENV) && spack load netcdf-c && pkg-config --variable=libdir netcdf 2>/dev/null || echo "$(SPACK_ROOT)/opt/spack/linux-neoverse_v2/netcdf-c/lib")

# Add RPATH to ensure runtime library location
LDFLAGS += -Wl,-rpath,$(NETCDF_LIBDIR)

# Source files
SRCDIR = src
SOURCES = $(SRCDIR)/netcdf_bit_analysis.c $(SRCDIR)/bit_pattern.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(SRCDIR)/bit_pattern.h

# Bitrounding source files
BITROUNDING_SOURCES = $(SRCDIR)/bitrounding.c $(SRCDIR)/bitrounding_stats.c $(SRCDIR)/bitrounding_bitinfo.c
BITROUNDING_OBJECTS = $(BITROUNDING_SOURCES:.c=.o)
BITROUNDING_HEADERS = $(SRCDIR)/bitrounding_stats.h $(SRCDIR)/bitrounding_bitinfo.h

# Target executables
TARGET = netcdf_bit_analysis
BITROUNDING_TARGET = bitrounding

# Build rules
all: $(TARGET) $(BITROUNDING_TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(NETCDF_LIBS)
	@echo "Build complete: $(TARGET)"

$(BITROUNDING_TARGET): $(BITROUNDING_OBJECTS)
	@echo "Linking $(BITROUNDING_TARGET)..."
	$(CC) $(BITROUNDING_OBJECTS) -o $@ $(LDFLAGS) $(NETCDF_LIBS)
	@echo "Build complete: $(BITROUNDING_TARGET)"

%.o: %.c $(HEADERS) $(BITROUNDING_HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(NETCDF_CFLAGS) -c $< -o $@

# Spack-specific build target
build-spack: setup-env all

setup-env:
	@echo "Setting up Spack environment..."
	@$(SPACK_ENV) && spack load netcdf-c && echo "NetCDF loaded successfully"

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
	rm -f $(OBJECTS) $(BITROUNDING_OBJECTS) $(TARGET) $(BITROUNDING_TARGET)
	@echo "Clean complete"

# Install to local bin (optional)
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Installation complete"

# Create example output
example: $(TARGET)
	@echo "Running example analysis..."
	./$(TARGET) /capstor/scratch/cscs/lhuang/CompressionBenchmark_data/energy_flux/P2016-1020_09_chunked_cdotest_nccopy_compress.nc > examples/sample_output.txt
	@echo "Example output saved to examples/sample_output.txt"

# Check NetCDF installation
check-deps:
	@echo "Checking NetCDF installation..."
	@$(SPACK_ENV) && spack load netcdf-c && nc-config --version 2>/dev/null || echo "NetCDF not found via Spack"
	@pkg-config --exists netcdf && echo "NetCDF found via pkg-config" || echo "NetCDF not found via pkg-config"
	@echo "NetCDF CFLAGS: $(NETCDF_CFLAGS)"
	@echo "NetCDF LIBS: $(NETCDF_LIBS)"

# Print make variables for debugging
print-vars:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "NETCDF_CFLAGS: $(NETCDF_CFLAGS)"
	@echo "NETCDF_LIBS: $(NETCDF_LIBS)"
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"
	@echo "TARGET: $(TARGET)"

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build the netcdf_bit_analysis tool (default)"
	@echo "  build-spack  - Build with explicit Spack environment setup"
	@echo "  debug        - Build with debug flags"
	@echo "  release      - Build optimized release version"
	@echo "  test-build   - Build with verbose debugging"
	@echo "  clean        - Remove build artifacts"
	@echo "  install      - Install to /usr/local/bin (requires sudo)"
	@echo "  example      - Run example analysis and save output"
	@echo "  check-deps   - Check NetCDF library installation"
	@echo "  print-vars   - Print make variables for debugging"
	@echo "  help         - Show this help message"

# SLURM integration targets
srun-build:
	srun -A a122 -t 1:00:00 --mem=8000 make build-spack

srun-test:
	srun -A a122 -t 2:00:00 --mem=16000 make example

# Declare phony targets
.PHONY: all build-spack setup-env test-build debug release clean install example check-deps print-vars help srun-build srun-test

# Default target
.DEFAULT_GOAL := all