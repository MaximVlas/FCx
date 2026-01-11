// Benchmark: XOR operations
// Tests: XOR performance, variable swapping

#include <stdint.h>

int main(void) {
    int64_t iterations = 10000000;
    int64_t a = 12345678;
    int64_t b = 87654321;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        // XOR swap
        a ^= b;
        b ^= a;
        a ^= b;
        sum += a ^ b;
    }
    
    return (sum > 0) ? 0 : 1;
}
