// Benchmark: Division-heavy workload
// Tests: Division performance (typically slow)

#include <stdint.h>

int main(void) {
    int64_t iterations = 5000000;
    int64_t sum = 0;
    
    for (int64_t i = 1; i <= iterations; i++) {
        int64_t a = 1000000 / i;
        int64_t b = 999999 / i;
        int64_t c = 888888 / i;
        sum += a + b + c;
    }
    
    return (sum > 0) ? 0 : 1;
}
