// Benchmark: Bit shift operations
// Tests: Left and right shift performance

#include <stdint.h>

int main(void) {
    int64_t iterations = 10000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        int64_t a = i;
        int64_t b = a << 1;
        int64_t c = b << 2;
        int64_t d = c >> 1;
        int64_t e = d >> 3;
        sum += e;
    }
    
    return (sum > 0) ? 0 : 1;
}
