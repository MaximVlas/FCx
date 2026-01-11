// Benchmark: Modulo operations
// Tests: Modulo performance

#include <stdint.h>

int main(void) {
    int64_t iterations = 5000000;
    int64_t sum = 0;
    
    for (int64_t i = 1; i <= iterations; i++) {
        int64_t mod1 = i % 17;
        int64_t mod2 = i % 23;
        sum += mod1 + mod2;
    }
    
    return (sum > 0) ? 0 : 1;
}
