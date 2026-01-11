// Benchmark: Manual loop unrolling effect
// Tests: Instruction-level parallelism

#include <stdint.h>

int main(void) {
    int64_t iterations = 25000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i += 4) {
        sum += i;
        sum += i + 1;
        sum += i + 2;
        sum += i + 3;
    }
    
    return (sum > 0) ? 0 : 1;
}
