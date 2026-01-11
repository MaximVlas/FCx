// Benchmark: GCD and LCM computation
// Tests: Modulo operations, function calls

#include <stdint.h>

static inline int64_t gcd(int64_t a, int64_t b) {
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

static inline int64_t lcm(int64_t a, int64_t b) {
    return (a * b) / gcd(a, b);
}

int main(void) {
    int64_t iterations = 5000000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += gcd(48, 18);
        sum += lcm(12, 8);
    }
    
    return (sum > 0) ? 0 : 1;
}
