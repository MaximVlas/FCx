// Benchmark harness for binary search comparison
// Links against both C and FCx compiled object files

#include <stdio.h>
#include <stdint.h>
#include <time.h>

// External functions from object files
extern int64_t binary_search_c(int64_t target, int64_t size);
extern int64_t binary_search_fcx(int64_t target, int64_t size);

#define ITERATIONS 1000000
#define SIZE 1000000
#define WARMUP_RUNS 2
#define BENCH_RUNS 6

static double time_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + 
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static double benchmark_c(void) {
    struct timespec start, end;
    int64_t total = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int64_t i = 0; i < ITERATIONS; i++) {
        int64_t target = (i * 7) % SIZE;
        total += binary_search_c(target, SIZE);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Prevent optimization
    if (total == 0) printf("!");
    
    return time_ms(start, end);
}

static double benchmark_fcx(void) {
    struct timespec start, end;
    int64_t total = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int64_t i = 0; i < ITERATIONS; i++) {
        int64_t target = (i * 7) % SIZE;
        total += binary_search_fcx(target, SIZE);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Prevent optimization
    if (total == 0) printf("!");
    
    return time_ms(start, end);
}

int main(void) {
    double c_times[BENCH_RUNS];
    double fcx_times[BENCH_RUNS];
    
    printf("Binary Search Benchmark (.o linking comparison)\n");
    printf("================================================\n");
    printf("Iterations: %d, Array size: %d\n\n", ITERATIONS, SIZE);
    
    // Warmup
    printf("Warming up...\n");
    for (int i = 0; i < WARMUP_RUNS; i++) {
        benchmark_c();
        benchmark_fcx();
    }
    
    // Benchmark runs
    printf("Running benchmarks...\n\n");
    for (int i = 0; i < BENCH_RUNS; i++) {
        c_times[i] = benchmark_c();
        fcx_times[i] = benchmark_fcx();
    }
    
    // Find minimums
    double c_min = c_times[0], fcx_min = fcx_times[0];
    for (int i = 1; i < BENCH_RUNS; i++) {
        if (c_times[i] < c_min) c_min = c_times[i];
        if (fcx_times[i] < fcx_min) fcx_min = fcx_times[i];
    }
    
    // Results
    printf("Results (min of %d runs):\n", BENCH_RUNS);
    printf("  C:   %8.3f ms\n", c_min);
    printf("  FCx: %8.3f ms\n", fcx_min);
    printf("  Ratio: %.2fx\n", fcx_min / c_min);
    
    return 0;
}
