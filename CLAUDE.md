# Slurm and Environment Setup

## Slurm Job Submission
- To run a job in Slurm, use `sbatch` command followed by a job script
- Slurm account `a122`, use as `srun -A a122` or `sbatch -A a122` or `#SBATCH -A a122`
- Basic Slurm job script template:
  ```bash
  #!/bin/bash
  #SBATCH --job-name=myjob
  #SBATCH --output=output_%j.txt
  #SBATCH --error=error_%j.txt
  #SBATCH --nodes=1
  #SBATCH --ntasks=1
  #SBATCH --time=01:00:00

  # Your job commands here
  ```

## Python Job Submission
- Run Python scripts in Slurm using:
  ```bash
  #!/bin/bash
  #SBATCH --environment=compbench
  #SBATCH --job-name=pythonscript
  #SBATCH --output=python_output_%j.txt
  source /code/venv/bin/activate
  python your_script.py
  ```

## Environment Setup
- Enable Spack environment:
  ```bash
  source /path/to/spack/share/spack/setup-env.sh
  spack env activate your_environment_name
  ```

- Load HDF5 and NetCDF tools:
  ```bash
  spack load hdf5
  spack load netcdf-c
  spack load netcdf-fortran
  ```

- Spack Location and User Configuration:
  ```bash
  source /capstor/scratch/cscs/lhuang/bitrounding/spack/share/spack/setup-env.sh && export SPACK_USER_CONFIG_PATH=/capstor/scratch/cscs/lhuang/bitrounding/.spack
  ```

## Testing Scripts
- Record the testing script

## Python Environments
- You can run python under uv environment with `uv run ...`