// Benchmark: Deep recursion
// Tests: Function call overhead, stack usage

#include <stdint.h>

int64_t recurse(int64_t n, int64_t acc) {
    if (n == 0) return acc;
    return recurse(n - 1, acc + n);
}

int main(void) {
    int64_t iterations = 10000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += recurse(100, 0);
    }
    
    return (sum > 0) ? 0 : 1;
}
