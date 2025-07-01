using NCDatasets
using BitInformation
using Printf

function calculate_and_print_bitinfo(data::Vector{Float32}, prefix::String="  ")
    """
    Calculate and pretty print bit information for a data array
    """
    try
        bit_info = bitinformation(data)
        
        # Pretty print bit information array
        if isa(bit_info, Number)
            println("$(prefix)Bit Information: $(Printf.@sprintf("%.2f", bit_info)) bits")
        else
            print("$(prefix)Bit Information: [")
            for (i, val) in enumerate(bit_info)
                if val < 0.01 && val != 0
                    print(Printf.@sprintf("%.2e", val))
                else
                    print(Printf.@sprintf("%.2f", val))
                end
                if i < length(bit_info)
                    print(", ")
                end
            end
            println("] bits")
        end
        
    catch e
        println("$(prefix)Error calculating bit information: $e")
    end
end

function analyze_netcdf_bit_information(filename::String)
    """
    Load a NetCDF file and calculate bit information for all Float32 data variables
    """
    println("Opening NetCDF file: $filename")
    
    NCDataset(filename, "r") do ds
        println("Dataset dimensions:")
        for (name, dim) in ds.dim
            println("  $name: $(dim)")
        end
        
        println("\nAnalyzing Float32 variables:")
        
        for (varname, var) in ds
                
	    if Float32 in Base.uniontypes(eltype(var))
                println("\nProcessing variable: $varname")
                println("  Shape: $(size(var))")
                println("  Type: $(eltype(var))")
                
                var_size = size(var)
                
                if length(var_size) <= 2
                    # 1D or 2D variable - process as before
                    data = var[:]
                    
                    # Remove missing values if present
                    if any(ismissing, data)
                        println("  Removing missing values...")
                        data = filter(!ismissing, data)
                    end
                    
                    # Convert to Vector{Float32} if needed
                    if !(data isa Vector{Float32})
                        data = Float32.(vec(data))
                    end
                    
                    if length(data) > 0
                        println("  Calculating bit information for $(length(data)) values...")
                        calculate_and_print_bitinfo(data)
                    else
                        println("  No valid data found")
                    end
                else
                    # Multi-dimensional variable - process 2D slices
                    println("  Processing 2D slices of first two dimensions...")
                    
                    # Get dimension names for the variable
                    var_dims = dimnames(var)
                    
                    # Get indices for all dimensions beyond the first two
                    remaining_dims = var_size[3:end]
                    
                    # Generate all combinations of indices for the remaining dimensions
                    indices_ranges = [1:dim_size for dim_size in remaining_dims]
                    
                    for indices in Iterators.product(indices_ranges...)
                        # Create slice indices: [:, :, indices...]
                        slice_indices = (:, :, indices...)
                        
                        try
                            # Extract 2D slice
                            slice_data = var[slice_indices...]
                            
                            # Remove missing values if present
                            if any(ismissing, slice_data)
                                slice_data = filter(!ismissing, vec(slice_data))
                            else
                                slice_data = vec(slice_data)
                            end
                            
                            # Convert to Vector{Float32} if needed
                            if !(slice_data isa Vector{Float32})
                                slice_data = Float32.(slice_data)
                            end
                            
                            # Build coordinate description
                            coord_parts = String[]
                            for (i, idx) in enumerate(indices)
                                dim_name = var_dims[i+2]  # +2 because first two dims are spatial
                                try
                                    # Try to get coordinate values from the dataset
                                    if haskey(ds, dim_name)
                                        coord_var = ds[dim_name]
                                        coord_value = coord_var[idx]
                                        push!(coord_parts, "$(dim_name)=$(coord_value)")
                                    else
                                        push!(coord_parts, "$(dim_name)=$(idx)")
                                    end
                                catch
                                    push!(coord_parts, "$(dim_name)=$(idx)")
                                end
                            end
                            coord_str = join(coord_parts, ", ")
                            
                            if length(slice_data) > 0
                                println("    $coord_str: $(length(slice_data)) values")
                                calculate_and_print_bitinfo(slice_data, "      ")
                            else
                                println("    $coord_str: No valid data")
                            end
                        catch e
                            indices_str = join(indices, ",")
                            println("    Slice [:,:,$indices_str]: Error - $e")
                        end
                    end
                end
            end
        end
    end
end

# Main execution
if length(ARGS) < 1
    println("Usage: julia bit_information.jl <netcdf_file>")
    println("Example: julia bit_information.jl data.nc")
    exit(1)
end

filename = ARGS[1]

if !isfile(filename)
    println("Error: File '$filename' not found")
    exit(1)
end

try
    analyze_netcdf_bit_information(filename)
catch e
    println("Error processing file: $e")
    exit(1)
end
