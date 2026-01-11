#!/bin/bash
# Quick benchmark test - runs a subset of benchmarks for fast validation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FCX_COMPILER="${SCRIPT_DIR}/../bin/fcx"
C_COMPILER="clang"
C_FLAGS="-O3 -march=native"

echo "Quick Benchmark Test"
echo "===================="
echo ""

# Test a few representative benchmarks
TESTS=("01_fibonacci" "07_loop_sum" "11_int_arithmetic" "16_bit_count" "21_recursion_deep")

for test in "${TESTS[@]}"; do
    echo "Testing: $test"
    
    # Compile C
    $C_COMPILER $C_FLAGS "${SCRIPT_DIR}/c/${test}.c" -o "/tmp/${test}_c" 2>/dev/null
    
    # Compile FCx
    "$FCX_COMPILER" "${SCRIPT_DIR}/fcx/${test}.fcx" -o "/tmp/${test}_fcx" 2>/dev/null
    
    # Time C (using date for nanosecond precision)
    c_start=$(date +%s.%N)
    /tmp/${test}_c
    c_end=$(date +%s.%N)
    c_time=$(echo "scale=3; ($c_end - $c_start) * 1000" | bc)
    
    # Time FCx
    fcx_start=$(date +%s.%N)
    /tmp/${test}_fcx
    fcx_end=$(date +%s.%N)
    fcx_time=$(echo "scale=3; ($fcx_end - $fcx_start) * 1000" | bc)
    
    # Calculate ratio
    if [ -n "$c_time" ] && [ -n "$fcx_time" ]; then
        ratio=$(echo "scale=2; $fcx_time / $c_time" | bc 2>/dev/null || echo "N/A")
        printf "  C: %.1f ms  FCx: %.1f ms  Ratio: %sx\n" "$c_time" "$fcx_time" "$ratio"
    fi
    
    # Cleanup
    rm -f "/tmp/${test}_c" "/tmp/${test}_fcx"
done

echo ""
echo "Quick test completed!"
