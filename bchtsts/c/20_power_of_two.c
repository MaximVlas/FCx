// Benchmark: Power of two checks
// Tests: Bitwise AND for power-of-2 detection

#include <stdint.h>

static inline int is_power_of_two(int64_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

int main(void) {
    int64_t iterations = 10000000;
    int64_t count = 0;
    
    for (int64_t i = 1; i <= iterations; i++) {
        count += is_power_of_two(i);
    }
    
    return (count > 0) ? 0 : 1;
}
