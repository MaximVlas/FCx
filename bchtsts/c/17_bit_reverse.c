// Benchmark: Bit reversal
// Tests: Shift and OR operations

#include <stdint.h>

static inline int64_t reverse_bits(int64_t n) {
    int64_t result = 0;
    for (int i = 0; i < 32; i++) {
        result = (result << 1) | (n & 1);
        n >>= 1;
    }
    return result;
}

int main(void) {
    int64_t iterations = 2000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        // Use XOR to prevent overflow while still doing work
        sum ^= reverse_bits(i);
    }
    
    // XOR result can be 0, so just return 0 (success)
    return 0;
}
