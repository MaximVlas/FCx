// Benchmark: Simple loop summation
// Tests: Loop overhead, increment operations

#include <stdint.h>

int main(void) {
    int64_t iterations = 100000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += i;
    }
    
    return (sum > 0) ? 0 : 1;
}
