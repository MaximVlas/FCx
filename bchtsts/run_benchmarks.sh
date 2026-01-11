#!/bin/bash
# FCx vs C Benchmark Runner
# Compiles and runs all benchmarks, comparing FCx against C

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FCX_COMPILER="${SCRIPT_DIR}/../bin/fcx"
C_COMPILER="clang"
C_FLAGS="-O3 -march=native -flto"
RESULTS_DIR="${SCRIPT_DIR}/results"
ITERATIONS=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

mkdir -p "${RESULTS_DIR}"
mkdir -p "${SCRIPT_DIR}/bin"

echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║         FCx vs C Performance Benchmark Suite                   ║${NC}"
echo -e "${CYAN}╠════════════════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║  Iterations per test: ${ITERATIONS}                                        ║${NC}"
echo -e "${CYAN}║  C Compiler: ${C_COMPILER} ${C_FLAGS}                        ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Function to get time in milliseconds
get_time_ms() {
    local start=$1
    local end=$2
    echo "scale=3; ($end - $start) * 1000" | bc
}

# Function to run benchmark and collect metrics
run_benchmark() {
    local name=$1
    local fcx_bin=$2
    local c_bin=$3
    local category=$4
    
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${YELLOW}Benchmark: ${name}${NC} (${category})"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    local fcx_times=()
    local c_times=()
    local fcx_mem=0
    local c_mem=0
    
    # Run FCx benchmark
    if [ -f "$fcx_bin" ]; then
        echo -n "  FCx: "
        for i in $(seq 1 $ITERATIONS); do
            local start=$(date +%s.%N)
            "$fcx_bin" > /dev/null 2>&1
            local end=$(date +%s.%N)
            local elapsed=$(echo "scale=6; ($end - $start) * 1000" | bc)
            fcx_times+=($elapsed)
            echo -n "."
        done
        # Get memory usage on last run
        fcx_mem=$(/usr/bin/time -v "$fcx_bin" 2>&1 | grep "Maximum resident set size" | awk '{print $6}' || echo "0")
        echo ""
    else
        echo -e "  ${RED}FCx binary not found${NC}"
    fi
    
    # Run C benchmark
    if [ -f "$c_bin" ]; then
        echo -n "  C:   "
        for i in $(seq 1 $ITERATIONS); do
            local start=$(date +%s.%N)
            "$c_bin" > /dev/null 2>&1
            local end=$(date +%s.%N)
            local elapsed=$(echo "scale=6; ($end - $start) * 1000" | bc)
            c_times+=($elapsed)
            echo -n "."
        done
        # Get memory usage on last run
        c_mem=$(/usr/bin/time -v "$c_bin" 2>&1 | grep "Maximum resident set size" | awk '{print $6}' || echo "0")
        echo ""
    else
        echo -e "  ${RED}C binary not found${NC}"
    fi
    
    # Calculate averages
    local fcx_avg=0
    local c_avg=0
    
    if [ ${#fcx_times[@]} -gt 0 ]; then
        for t in "${fcx_times[@]}"; do
            fcx_avg=$(echo "scale=6; $fcx_avg + $t" | bc)
        done
        fcx_avg=$(echo "scale=3; $fcx_avg / ${#fcx_times[@]}" | bc)
    fi
    
    if [ ${#c_times[@]} -gt 0 ]; then
        for t in "${c_times[@]}"; do
            c_avg=$(echo "scale=6; $c_avg + $t" | bc)
        done
        c_avg=$(echo "scale=3; $c_avg / ${#c_times[@]}" | bc)
    fi
    
    # Calculate ratio
    local ratio="N/A"
    if [ "$c_avg" != "0" ] && [ "$fcx_avg" != "0" ]; then
        ratio=$(echo "scale=2; $fcx_avg / $c_avg" | bc)
    fi
    
    # Print results
    echo ""
    echo -e "  ${GREEN}Results:${NC}"
    printf "    %-12s %10s ms  (mem: %s KB)\n" "FCx:" "$fcx_avg" "$fcx_mem"
    printf "    %-12s %10s ms  (mem: %s KB)\n" "C:" "$c_avg" "$c_mem"
    
    if [ "$ratio" != "N/A" ]; then
        if (( $(echo "$ratio < 1.1" | bc -l) )); then
            echo -e "    Ratio:       ${GREEN}${ratio}x${NC} (FCx is competitive)"
        elif (( $(echo "$ratio < 2.0" | bc -l) )); then
            echo -e "    Ratio:       ${YELLOW}${ratio}x${NC} (FCx is slower)"
        else
            echo -e "    Ratio:       ${RED}${ratio}x${NC} (FCx needs optimization)"
        fi
    fi
    
    # Save to results file
    echo "${name},${category},${fcx_avg},${c_avg},${ratio},${fcx_mem},${c_mem}" >> "${RESULTS_DIR}/benchmark_results.csv"
    echo ""
}

# Compile a benchmark
compile_benchmark() {
    local name=$1
    local fcx_src="${SCRIPT_DIR}/fcx/${name}.fcx"
    local c_src="${SCRIPT_DIR}/c/${name}.c"
    local fcx_bin="${SCRIPT_DIR}/bin/${name}_fcx"
    local c_bin="${SCRIPT_DIR}/bin/${name}_c"
    
    # Compile FCx
    if [ -f "$fcx_src" ]; then
        echo -e "  Compiling FCx: ${name}.fcx"
        "$FCX_COMPILER" "$fcx_src" -o "$fcx_bin" 2>/dev/null || echo -e "    ${RED}FCx compilation failed${NC}"
    fi
    
    # Compile C
    if [ -f "$c_src" ]; then
        echo -e "  Compiling C:   ${name}.c"
        $C_COMPILER $C_FLAGS "$c_src" -o "$c_bin" 2>/dev/null || echo -e "    ${RED}C compilation failed${NC}"
    fi
}

# Initialize results file
echo "name,category,fcx_ms,c_ms,ratio,fcx_mem_kb,c_mem_kb" > "${RESULTS_DIR}/benchmark_results.csv"

# Category filter
CATEGORY_FILTER="${1:-all}"

echo -e "${GREEN}Compiling benchmarks...${NC}"
echo ""

# List of all benchmarks
BENCHMARKS=(
    # Computational
    "01_fibonacci:computational"
    "02_primes_sieve:computational"
    "03_factorial:computational"
    "04_gcd_lcm:computational"
    "05_collatz:computational"
    "06_ackermann:computational"
    
    # Loop Performance
    "07_loop_sum:loop"
    "08_nested_loops:loop"
    "09_loop_unroll:loop"
    "10_countdown:loop"
    
    # Arithmetic
    "11_int_arithmetic:arithmetic"
    "12_mixed_ops:arithmetic"
    "13_division_heavy:arithmetic"
    "14_multiply_chain:arithmetic"
    "15_modulo_ops:arithmetic"
    
    # Bitwise
    "16_bit_count:bitwise"
    "17_bit_reverse:bitwise"
    "18_bit_shift:bitwise"
    "19_xor_swap:bitwise"
    "20_power_of_two:bitwise"
    
    # Function Calls
    "21_recursion_deep:function"
    "22_tail_recursion:function"
    "23_mutual_recursion:function"
    "24_function_chain:function"
    
    # Array/Memory
    "25_array_sum:memory"
    "26_array_copy:memory"
    "27_matrix_mult:memory"
    "28_bubble_sort:memory"
    "29_binary_search:memory"
    "30_memory_throughput:memory"
)

# Compile all benchmarks
for bench in "${BENCHMARKS[@]}"; do
    name="${bench%%:*}"
    category="${bench##*:}"
    
    if [ "$CATEGORY_FILTER" = "all" ] || [ "$CATEGORY_FILTER" = "$category" ]; then
        compile_benchmark "$name"
    fi
done

echo ""
echo -e "${GREEN}Running benchmarks...${NC}"
echo ""

# Run all benchmarks
for bench in "${BENCHMARKS[@]}"; do
    name="${bench%%:*}"
    category="${bench##*:}"
    
    if [ "$CATEGORY_FILTER" = "all" ] || [ "$CATEGORY_FILTER" = "$category" ]; then
        fcx_bin="${SCRIPT_DIR}/bin/${name}_fcx"
        c_bin="${SCRIPT_DIR}/bin/${name}_c"
        run_benchmark "$name" "$fcx_bin" "$c_bin" "$category"
    fi
done

# Print summary
echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║                      BENCHMARK SUMMARY                         ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Results saved to: ${RESULTS_DIR}/benchmark_results.csv"
echo ""

# Calculate overall statistics
if [ -f "${RESULTS_DIR}/benchmark_results.csv" ]; then
    echo -e "${GREEN}Category Averages:${NC}"
    tail -n +2 "${RESULTS_DIR}/benchmark_results.csv" | awk -F',' '
    {
        category[$2]++
        fcx_total[$2] += $3
        c_total[$2] += $4
    }
    END {
        for (cat in category) {
            fcx_avg = fcx_total[cat] / category[cat]
            c_avg = c_total[cat] / category[cat]
            ratio = (c_avg > 0) ? fcx_avg / c_avg : 0
            printf "  %-15s FCx: %8.2f ms  C: %8.2f ms  Ratio: %.2fx\n", cat, fcx_avg, c_avg, ratio
        }
    }'
fi

echo ""
echo -e "${GREEN}Benchmark suite completed!${NC}"
