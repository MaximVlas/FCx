// Benchmark: Multiplication chain
// Tests: Multiplication throughput

#include <stdint.h>

int main(void) {
    int64_t iterations = 10000000;
    int64_t sum = 0;
    
    // Use loop variable to prevent constant folding
    for (int64_t i = 1; i <= iterations; i++) {
        int64_t a = i;
        int64_t b = a * 5;
        int64_t c = b * 7;
        int64_t d = c * 11;
        int64_t e = d * 13;
        sum += e / 1000000;
    }
    
    return (sum > 0) ? 0 : 1;
}
