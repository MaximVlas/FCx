// Benchmark: Function call chain
// Tests: Multiple function call overhead

#include <stdint.h>

static inline int64_t f1(int64_t x) { return x + 1; }
static inline int64_t f2(int64_t x) { return f1(x) * 2; }
static inline int64_t f3(int64_t x) { return f2(x) + 3; }
static inline int64_t f4(int64_t x) { return f3(x) - 1; }
static inline int64_t f5(int64_t x) { return f4(x) * 2; }

int main(void) {
    int64_t iterations = 5000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += f5(i);
    }
    
    return (sum > 0) ? 0 : 1;
}
