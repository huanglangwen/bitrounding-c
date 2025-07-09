cmake_minimum_required(VERSION 3.10)
project(NetCDF_HDF5_BitAnalysis C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compilation flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O3 -g")

# Find required packages
find_package(PkgConfig REQUIRED)

# Find NetCDF
pkg_check_modules(NETCDF REQUIRED netcdf)
if(NOT NETCDF_FOUND)
    message(FATAL_ERROR "NetCDF library not found")
endif()

# Find HDF5
find_package(HDF5 REQUIRED COMPONENTS C HL)
if(NOT HDF5_FOUND)
    message(FATAL_ERROR "HDF5 library not found")
endif()

# Find HDF5 static libraries for hdf_concat
find_library(HDF5_STATIC_LIB NAMES libhdf5.a
    PATHS /usr/lib64 /usr/lib /usr/local/lib /lib64
    NO_DEFAULT_PATH)
find_library(HDF5_HL_STATIC_LIB NAMES libhdf5_hl.a
    PATHS /usr/lib64 /usr/lib /usr/local/lib /lib64
    NO_DEFAULT_PATH)

# Find static zlib for static build
find_library(ZLIB_STATIC_LIB NAMES libz.a
    PATHS /usr/lib64 /usr/lib /usr/local/lib /lib64
    NO_DEFAULT_PATH)

# Find szip library (required by HDF5)
find_library(SZIP_LIB NAMES libsz.so sz
    PATHS /usr/lib64 /usr/lib /usr/local/lib /lib64
    NO_DEFAULT_PATH)

# Find ZLIB
find_package(ZLIB REQUIRED)
if(NOT ZLIB_FOUND)
    message(FATAL_ERROR "ZLIB library not found")
endif()

# Include directories
include_directories(${NETCDF_INCLUDE_DIRS})
include_directories(${HDF5_INCLUDE_DIRS})
include_directories(${ZLIB_INCLUDE_DIRS})

# Source files
set(SRCDIR src)

# Headers
set(HEADERS
    ${SRCDIR}/bit_pattern.h
    ${SRCDIR}/bitrounding_stats.h
    ${SRCDIR}/bitrounding_bitinfo.h
)

# NetCDF bit analysis executable
add_executable(netcdf_bit_analysis
    ${SRCDIR}/netcdf_bit_analysis.c
    ${SRCDIR}/bit_pattern.c
)
target_link_libraries(netcdf_bit_analysis
    ${NETCDF_LIBRARIES}
    ${HDF5_LIBRARIES}
    ${ZLIB_LIBRARIES}
    m
    dl
)
target_compile_options(netcdf_bit_analysis PRIVATE ${NETCDF_CFLAGS_OTHER})
target_link_directories(netcdf_bit_analysis PRIVATE ${NETCDF_LIBRARY_DIRS})

# HDF5 bit analysis executable
add_executable(hdf_bit_analysis
    ${SRCDIR}/hdf_bit_analysis.c
    ${SRCDIR}/bit_pattern.c
)
target_link_libraries(hdf_bit_analysis
    ${HDF5_LIBRARIES}
    ${ZLIB_LIBRARIES}
    m
    dl
)

# HDF5 size statistics executable
add_executable(hdf_size_stat
    ${SRCDIR}/hdf_size_stat.c
)
target_link_libraries(hdf_size_stat
    ${HDF5_LIBRARIES}
    ${ZLIB_LIBRARIES}
    m
    dl
)

# NetCDF bit rounding executable
add_executable(netcdf_bit_rounding
    ${SRCDIR}/netcdf_bit_rounding.c
    ${SRCDIR}/bitrounding_stats.c
    ${SRCDIR}/bitrounding_bitinfo.c
)
target_link_libraries(netcdf_bit_rounding
    ${NETCDF_LIBRARIES}
    ${HDF5_LIBRARIES}
    ${ZLIB_LIBRARIES}
    m
    dl
)
target_compile_options(netcdf_bit_rounding PRIVATE ${NETCDF_CFLAGS_OTHER})
target_link_directories(netcdf_bit_rounding PRIVATE ${NETCDF_LIBRARY_DIRS})

# HDF5 concatenation executable
add_executable(hdf_concat
    ${SRCDIR}/hdf_concat.c
)

# Link HDF5 statically for hdf_concat
if(HDF5_STATIC_LIB AND HDF5_HL_STATIC_LIB)
    message(STATUS "Using static HDF5 libraries for hdf_concat")
    if(ZLIB_STATIC_LIB)
        message(STATUS "Using static zlib for hdf_concat")
        target_link_libraries(hdf_concat
            ${HDF5_STATIC_LIB}
            ${HDF5_HL_STATIC_LIB}
            ${ZLIB_STATIC_LIB}
            ${SZIP_LIB}
            m
            dl
            pthread
        )
    else()
        message(STATUS "Using shared zlib for hdf_concat")
        target_link_libraries(hdf_concat
            ${HDF5_STATIC_LIB}
            ${HDF5_HL_STATIC_LIB}
            ${ZLIB_LIBRARIES}
            ${SZIP_LIB}
            m
            dl
            pthread
        )
    endif()
else()
    message(STATUS "Static HDF5 libraries not found, using shared libraries for hdf_concat")
    target_link_libraries(hdf_concat
        ${HDF5_LIBRARIES}
        ${ZLIB_LIBRARIES}
        m
        dl
    )
endif()

# Set RPATH for runtime library location
set_target_properties(netcdf_bit_analysis PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
set_target_properties(hdf_bit_analysis PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
set_target_properties(hdf_size_stat PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
set_target_properties(netcdf_bit_rounding PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
set_target_properties(hdf_concat PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# Debug and Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
else()
    add_definitions(-DNDEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
endif()

# Installation rules
install(TARGETS netcdf_bit_analysis hdf_bit_analysis hdf_size_stat netcdf_bit_rounding hdf_concat
    RUNTIME DESTINATION bin
)

# Print configuration information
message(STATUS "NetCDF include dirs: ${NETCDF_INCLUDE_DIRS}")
message(STATUS "NetCDF libraries: ${NETCDF_LIBRARIES}")
message(STATUS "HDF5 include dirs: ${HDF5_INCLUDE_DIRS}")
message(STATUS "HDF5 libraries: ${HDF5_LIBRARIES}")
message(STATUS "ZLIB include dirs: ${ZLIB_INCLUDE_DIRS}")
message(STATUS "ZLIB libraries: ${ZLIB_LIBRARIES}")