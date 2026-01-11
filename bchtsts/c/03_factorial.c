// Benchmark: Factorial computation
// Tests: Multiplication chains, loop performance

#include <stdint.h>

static inline int64_t factorial(int64_t n) {
    int64_t result = 1;
    for (int64_t i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

int main(void) {
    int64_t iterations = 10000000;
    int64_t sum = 0;
    
    // Use varying input to prevent constant folding
    for (int64_t i = 0; i < iterations; i++) {
        sum += factorial((i % 12) + 1);
    }
    
    return (sum > 0) ? 0 : 1;
}
