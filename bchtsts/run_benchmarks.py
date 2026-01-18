#!/usr/bin/env python3
"""
FCx vs C Benchmark Runner
Compiles and runs all benchmarks, comparing FCx against C

Usage:
    python run_benchmarks.py [OPTIONS]

Options:
    -O0, -O1, -O2, -O3, -Os    Optimization level (default: O2)
    -c, --category CATEGORY    Filter by category (computational, loop, arithmetic, bitwise, function, memory)
    -i, --iterations N         Number of iterations per benchmark (default: 5)
    -v, --verbose              Verbose output
    -h, --help                 Show this help

Examples:
    python run_benchmarks.py -O3                    # Run all with O3
    python run_benchmarks.py -O2 -c computational   # Run computational benchmarks with O2
    python run_benchmarks.py -O0 -O3 --compare      # Compare O0 vs O3
"""

import subprocess
import time
import os
import sys
import argparse
import csv
import resource
from pathlib import Path
from dataclasses import dataclass
from typing import List, Optional, Dict, Tuple
from statistics import mean, stdev

# ANSI colors
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    BOLD = '\033[1m'
    NC = '\033[0m'  # No Color

@dataclass
class BenchmarkResult:
    name: str
    category: str
    opt_level: str
    fcx_times: List[float]
    c_times: List[float]
    fcx_compiled: bool
    c_compiled: bool
    
    @property
    def fcx_avg(self) -> float:
        return mean(self.fcx_times) if self.fcx_times else 0.0
    
    @property
    def c_avg(self) -> float:
        return mean(self.c_times) if self.c_times else 0.0
    
    @property
    def fcx_min(self) -> float:
        return min(self.fcx_times) if self.fcx_times else 0.0
    
    @property
    def c_min(self) -> float:
        return min(self.c_times) if self.c_times else 0.0
    
    @property
    def ratio(self) -> float:
        # Use minimum times for more accurate comparison (less noise)
        if self.c_min > 0 and self.fcx_min > 0:
            return self.fcx_min / self.c_min
        return 0.0

# Benchmark definitions
BENCHMARKS = [
    # Computational
    ("01_fibonacci", "computational"),
    ("02_primes_sieve", "computational"),
    ("03_factorial", "computational"),
    ("04_gcd_lcm", "computational"),
    ("05_collatz", "computational"),
    ("06_ackermann", "computational"),
    
    # Loop Performance
    ("07_loop_sum", "loop"),
    ("08_nested_loops", "loop"),
    ("09_loop_unroll", "loop"),
    ("10_countdown", "loop"),
    
    # Arithmetic
    ("11_int_arithmetic", "arithmetic"),
    ("12_mixed_ops", "arithmetic"),
    ("13_division_heavy", "arithmetic"),
    ("14_multiply_chain", "arithmetic"),
    ("15_modulo_ops", "arithmetic"),
    
    # Bitwise
    ("16_bit_count", "bitwise"),
    ("17_bit_reverse", "bitwise"),
    ("18_bit_shift", "bitwise"),
    ("19_xor_swap", "bitwise"),
    ("20_power_of_two", "bitwise"),
    
    # Function Calls
    ("21_recursion_deep", "function"),
    ("22_tail_recursion", "function"),
    ("23_mutual_recursion", "function"),
    ("24_function_chain", "function"),
    
    # Array/Memory
    ("25_array_sum", "memory"),
    ("26_array_copy", "memory"),
    ("27_matrix_mult", "memory"),
    ("28_bubble_sort", "memory"),
    ("29_binary_search", "memory"),
    ("30_memory_throughput", "memory"),
]

# Optimization level configurations
OPT_CONFIGS = {
    "O0": {"fcx": "-O0", "c": "-O0", "desc": "No optimizations (debug)"},
    "O1": {"fcx": "-O1", "c": "-O1", "desc": "Basic optimizations"},
    "O2": {"fcx": "-O2", "c": "-O2", "desc": "Standard optimizations"},
    "O3": {"fcx": "-O3", "c": "-O3 -march=native -flto", "desc": "Aggressive optimizations"},
    "Os": {"fcx": "-Os", "c": "-Os", "desc": "Size optimizations"},
}

class BenchmarkRunner:
    def __init__(self, script_dir: Path, iterations: int = 5, verbose: bool = False):
        self.script_dir = script_dir
        self.fcx_compiler = script_dir.parent / "bin" / "fcx"
        self.c_compiler = "clang"
        self.results_dir = script_dir / "results"
        self.bin_dir = script_dir / "bin"
        self.iterations = iterations
        self.verbose = verbose
        
        # Create directories
        self.results_dir.mkdir(exist_ok=True)
        self.bin_dir.mkdir(exist_ok=True)
    
    def compile_fcx(self, src: Path, out: Path, opt_flag: str) -> bool:
        """Compile FCx source file."""
        if not src.exists():
            return False
        
        cmd = [str(self.fcx_compiler), opt_flag, str(src), "-o", str(out)]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=30)
            return result.returncode == 0
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False
    
    def compile_c(self, src: Path, out: Path, opt_flags: str) -> bool:
        """Compile C source file."""
        if not src.exists():
            return False
        
        cmd = [self.c_compiler] + opt_flags.split() + [str(src), "-o", str(out)]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=30)
            return result.returncode == 0
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False
    
    def run_binary(self, binary: Path) -> Optional[float]:
        """Run a binary and return execution time in milliseconds using getrusage for accuracy."""
        if not binary.exists():
            return None
        
        try:
            # Warm up run (helps with cache effects)
            subprocess.run([str(binary)], capture_output=True, timeout=60)
            
            # Use clock_gettime via time.perf_counter_ns for highest precision
            # Also do a fork/exec to minimize Python overhead
            start = time.perf_counter_ns()
            proc = subprocess.run([str(binary)], capture_output=True, timeout=60)
            end = time.perf_counter_ns()
            
            if proc.returncode not in (0, 1):  # Allow 0 or 1 as valid exit codes
                return None
            
            return (end - start) / 1_000_000  # Convert ns to ms
        except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
            return None
    
    def run_benchmark(self, name: str, category: str, opt_level: str) -> BenchmarkResult:
        """Run a single benchmark."""
        config = OPT_CONFIGS[opt_level]
        
        fcx_src = self.script_dir / "fcx" / f"{name}.fcx"
        c_src = self.script_dir / "c" / f"{name}.c"
        fcx_bin = self.bin_dir / f"{name}_fcx_{opt_level.lower()}"
        c_bin = self.bin_dir / f"{name}_c_{opt_level.lower()}"
        
        # Compile
        fcx_compiled = self.compile_fcx(fcx_src, fcx_bin, config["fcx"])
        c_compiled = self.compile_c(c_src, c_bin, config["c"])
        
        # Run benchmarks
        fcx_times = []
        c_times = []
        
        # Run more iterations and take the minimum (most representative)
        actual_iterations = self.iterations + 2  # Extra runs for warmup effect
        
        if fcx_compiled:
            for i in range(actual_iterations):
                t = self.run_binary(fcx_bin)
                if t is not None:
                    # Skip first run (cold cache)
                    if i > 0:
                        fcx_times.append(t)
        
        if c_compiled:
            for i in range(actual_iterations):
                t = self.run_binary(c_bin)
                if t is not None:
                    # Skip first run (cold cache)
                    if i > 0:
                        c_times.append(t)
        
        return BenchmarkResult(
            name=name,
            category=category,
            opt_level=opt_level,
            fcx_times=fcx_times,
            c_times=c_times,
            fcx_compiled=fcx_compiled,
            c_compiled=c_compiled
        )
    
    def print_header(self, opt_level: str, category: Optional[str]):
        """Print benchmark header."""
        config = OPT_CONFIGS[opt_level]
        
        print(f"{Colors.CYAN}╔════════════════════════════════════════════════════════════════╗{Colors.NC}")
        print(f"{Colors.CYAN}║         FCx vs C Performance Benchmark Suite                   ║{Colors.NC}")
        print(f"{Colors.CYAN}╠════════════════════════════════════════════════════════════════╣{Colors.NC}")
        print(f"{Colors.CYAN}║  Optimization: {opt_level:<6} ({config['desc']:<36})║{Colors.NC}")
        print(f"{Colors.CYAN}║  Category: {(category or 'all'):<52}║{Colors.NC}")
        print(f"{Colors.CYAN}║  Iterations: {self.iterations:<50}║{Colors.NC}")
        print(f"{Colors.CYAN}║  FCx Flags: {config['fcx']:<51}║{Colors.NC}")
        print(f"{Colors.CYAN}║  C Flags: {config['c']:<53}║{Colors.NC}")
        print(f"{Colors.CYAN}╚════════════════════════════════════════════════════════════════╝{Colors.NC}")
        print()
    
    def print_result(self, result: BenchmarkResult):
        """Print a single benchmark result."""
        print(f"{Colors.BLUE}{'━' * 64}{Colors.NC}")
        print(f"{Colors.YELLOW}Benchmark: {result.name}{Colors.NC} ({result.category})")
        print(f"{Colors.BLUE}{'━' * 64}{Colors.NC}")
        
        # FCx result - show min time (most representative)
        if result.fcx_compiled and result.fcx_times:
            print(f"  FCx: {result.fcx_min:10.3f} ms (min of {len(result.fcx_times)} runs)")
        else:
            print(f"  {Colors.RED}FCx: compilation failed or not found{Colors.NC}")
        
        # C result - show min time
        if result.c_compiled and result.c_times:
            print(f"  C:   {result.c_min:10.3f} ms (min of {len(result.c_times)} runs)")
        else:
            print(f"  {Colors.RED}C: compilation failed or not found{Colors.NC}")
        
        # Ratio (using min times)
        if result.ratio > 0:
            if result.ratio < 1.1:
                color = Colors.GREEN
                status = "FCx is competitive"
            elif result.ratio < 2.0:
                color = Colors.YELLOW
                status = "FCx is slower"
            else:
                color = Colors.RED
                status = "FCx needs optimization"
            print(f"  Ratio: {color}{result.ratio:.2f}x{Colors.NC} ({status})")
        print()
    
    def run_all(self, opt_level: str, category: Optional[str] = None) -> List[BenchmarkResult]:
        """Run all benchmarks."""
        self.print_header(opt_level, category)
        
        results = []
        
        # Filter benchmarks
        benchmarks = [(n, c) for n, c in BENCHMARKS if category is None or c == category]
        
        print(f"{Colors.GREEN}Compiling and running {len(benchmarks)} benchmarks...{Colors.NC}")
        print()
        
        for name, cat in benchmarks:
            result = self.run_benchmark(name, cat, opt_level)
            results.append(result)
            self.print_result(result)
        
        return results
    
    def print_summary(self, results: List[BenchmarkResult], opt_level: str):
        """Print summary statistics."""
        print(f"{Colors.CYAN}╔════════════════════════════════════════════════════════════════╗{Colors.NC}")
        print(f"{Colors.CYAN}║                    BENCHMARK SUMMARY ({opt_level})                      ║{Colors.NC}")
        print(f"{Colors.CYAN}╚════════════════════════════════════════════════════════════════╝{Colors.NC}")
        print()
        
        # Group by category
        categories: Dict[str, List[BenchmarkResult]] = {}
        for r in results:
            if r.category not in categories:
                categories[r.category] = []
            categories[r.category].append(r)
        
        print(f"{Colors.GREEN}Category Averages (using min times):{Colors.NC}")
        for cat, cat_results in sorted(categories.items()):
            fcx_times = [r.fcx_min for r in cat_results if r.fcx_min > 0]
            c_times = [r.c_min for r in cat_results if r.c_min > 0]
            
            fcx_avg = mean(fcx_times) if fcx_times else 0
            c_avg = mean(c_times) if c_times else 0
            ratio = fcx_avg / c_avg if c_avg > 0 else 0
            
            print(f"  {cat:<15} FCx: {fcx_avg:8.2f} ms  C: {c_avg:8.2f} ms  Ratio: {ratio:.2f}x")
        
        print()
        
        # Save to CSV
        csv_path = self.results_dir / f"benchmark_results_{opt_level.lower()}.csv"
        with open(csv_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['opt_level', 'name', 'category', 'fcx_min_ms', 'c_min_ms', 'ratio'])
            for r in results:
                writer.writerow([r.opt_level, r.name, r.category, f"{r.fcx_min:.3f}", f"{r.c_min:.3f}", f"{r.ratio:.2f}"])
        
        print(f"Results saved to: {csv_path}")
        print()

def main():
    parser = argparse.ArgumentParser(description="FCx vs C Benchmark Runner")
    parser.add_argument('-O0', action='store_true', help='No optimizations')
    parser.add_argument('-O1', action='store_true', help='Basic optimizations')
    parser.add_argument('-O2', action='store_true', help='Standard optimizations (default)')
    parser.add_argument('-O3', action='store_true', help='Aggressive optimizations')
    parser.add_argument('-Os', action='store_true', help='Size optimizations')
    parser.add_argument('-c', '--category', choices=['computational', 'loop', 'arithmetic', 'bitwise', 'function', 'memory'],
                        help='Filter by category')
    parser.add_argument('-i', '--iterations', type=int, default=5, help='Iterations per benchmark')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    # Determine optimization level
    opt_level = "O2"  # Default
    if args.O0:
        opt_level = "O0"
    elif args.O1:
        opt_level = "O1"
    elif args.O3:
        opt_level = "O3"
    elif args.Os:
        opt_level = "Os"
    
    script_dir = Path(__file__).parent.resolve()
    runner = BenchmarkRunner(script_dir, iterations=args.iterations, verbose=args.verbose)
    
    results = runner.run_all(opt_level, args.category)
    runner.print_summary(results, opt_level)
    
    print(f"{Colors.GREEN}Benchmark suite completed!{Colors.NC}")

if __name__ == "__main__":
    main()
