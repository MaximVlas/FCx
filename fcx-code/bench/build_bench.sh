#!/bin/bash
# Build and run binary search benchmark

set -e

# Change to script directory
cd "$(dirname "$0")"

echo "=== Building Binary Search Benchmark ==="

# Compile C version to .o
echo "Compiling C to .o..."
gcc -Os -c binary_s.c -o binary_s_c.o

# Compile FCx version to .o (adjust path to your fcx compiler)
echo "Compiling FCx to .o..."
../../bin/fcx -c binary_s.fcx -o binary_s_fcx.o

# Rename _start to avoid conflict with C runtime
objcopy --redefine-sym _start=_fcx_start binary_s_fcx.o

# Link benchmark harness with both object files
echo "Linking benchmark..."
gcc -Os bench_bs.c binary_s_c.o binary_s_fcx.o -o bench_bs

# Run benchmark
echo ""
./bench_bs
