// Benchmark: Mutual recursion
// Tests: Indirect function calls

#include <stdint.h>

int64_t is_odd(int64_t n);

int64_t is_even(int64_t n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}

int64_t is_odd(int64_t n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}

int main(void) {
    int64_t iterations = 100000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        // Use varying input to prevent constant folding
        int64_t n = (i % 20) + 40;
        sum += is_even(n);
        sum += is_odd(n + 1);
    }
    
    return (sum > 0) ? 0 : 1;
}
