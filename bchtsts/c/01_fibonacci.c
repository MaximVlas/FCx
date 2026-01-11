// Benchmark: Fibonacci sequence computation

#include <stdint.h>

static inline int64_t fib(int64_t n) {
    if (n <= 1) return n;
    int64_t a = 0, b = 1;
    for (int64_t i = 2; i <= n; i++) {
        int64_t temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

int main(void) {
    int64_t iterations = 1000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += fib(30);
    }
    
    // Prevent optimization
    return (sum > 0) ? 0 : 1;
}
