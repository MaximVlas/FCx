// Benchmark: Collatz conjecture sequence length
// Tests: Conditionals, division, multiplication

#include <stdint.h>

static inline int64_t collatz_length(int64_t n) {
    int64_t steps = 0;
    while (n != 1) {
        if ((n & 1) == 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps++;
    }
    return steps;
}

int main(void) {
    int64_t sum = 0;
    int64_t limit = 100000;
    
    for (int64_t n = 1; n < limit; n++) {
        sum += collatz_length(n);
    }
    
    return (sum > 0) ? 0 : 1;
}
