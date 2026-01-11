// Benchmark: Integer arithmetic operations
// Tests: Add, sub, mul, div performance

#include <stdint.h>

int main(void) {
    int64_t iterations = 10000000;
    int64_t a = 12345;
    int64_t b = 67;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        int64_t r1 = a + b;
        int64_t r2 = a - b;
        int64_t r3 = a * b;
        int64_t r4 = a / b;
        sum += r1 + r2 + r3 + r4;
    }
    
    return (sum > 0) ? 0 : 1;
}
