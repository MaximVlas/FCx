// Benchmark: Mixed arithmetic operations
// Tests: Operation mixing, register pressure

#include <stdint.h>

int main(void) {
    int64_t iterations = 5000000;
    int64_t a = 100;
    int64_t b = 7;
    int64_t c = 13;
    int64_t d = 3;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        int64_t r = ((a + b) * c - d) / b + (a * d);
        sum += r;
        a++;
        if (a > 200) a = 100;
    }
    
    return (sum > 0) ? 0 : 1;
}
