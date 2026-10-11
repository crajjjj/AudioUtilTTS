#!/usr/bin/env python3
"""Run a training script with PyTorch's GPU memory capped.

Under WSL the NVIDIA driver lets an allocation past the card's memory succeed by
spilling into system RAM, which is many times slower and never raises the
out-of-memory error PyTorch relies on to release its cached blocks. Capping the
process below the card's size makes PyTorch reuse its cache, or fail loudly if
the batch truly does not fit.

Usage: run_capped.py <fraction> <script.py> [script args...]
"""
import os
import runpy
import sys

import torch

fraction = float(sys.argv[1])
script = sys.argv[2]
if not torch.cuda.is_available():
    sys.exit("No GPU is visible to PyTorch. Training needs an NVIDIA card.")
torch.cuda.set_per_process_memory_fraction(fraction)
total = torch.cuda.get_device_properties(0).total_memory / 2**30
print(f"GPU memory cap: {fraction:.2f} of {total:.1f} GB = {fraction * total:.1f} GB", flush=True)

# `python script.py` puts the script's folder first on the import path; do the same
sys.path.insert(0, os.path.dirname(os.path.abspath(script)))
sys.argv = [script] + sys.argv[3:]
runpy.run_path(script, run_name="__main__")
