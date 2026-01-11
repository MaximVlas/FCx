// Benchmark: Prime number sieve (trial division)
// Tests: Nested loops, modulo operations, conditionals

#include <stdint.h>

static inline int is_prime(int64_t n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if ((n & 1) == 0) return 0;
    
    for (int64_t i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return 0;
    }
    return 1;
}

int main(void) {
    int64_t count = 0;
    int64_t limit = 100000;
    
    for (int64_t n = 2; n < limit; n++) {
        if (is_prime(n)) {
            count++;
        }
    }
    
    return (count > 0) ? 0 : 1;
}
