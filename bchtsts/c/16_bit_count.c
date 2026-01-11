// Benchmark: Population count (bit counting)
// Tests: Bitwise AND, shift operations

#include <stdint.h>

static inline int64_t popcount(int64_t n) {
    int64_t count = 0;
    while (n != 0) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

int main(void) {
    int64_t iterations = 5000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += popcount(i);
    }
    
    return (sum > 0) ? 0 : 1;
}
