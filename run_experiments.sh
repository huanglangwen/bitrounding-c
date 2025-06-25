#!/bin/bash
srun --environment=compbench -A a122 -t 4:00:00 --mem=460000 bash -c "source /code/venv/bin/activate && python3 /capstor/scratch/cscs/lhuang/cdo/analyze_bit_precision.py /capstor/scratch/cscs/lhuang/CompressionBenchmark_data/energy_flux/P2016-1020_09_chunked_cdotest.nc"
