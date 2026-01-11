// Benchmark: Ackermann function (limited)
// Tests: Deep recursion, function call overhead

#include <stdint.h>

int64_t ackermann(int64_t m, int64_t n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

int main(void) {
    int64_t iterations = 100;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        // Use small values to avoid stack overflow
        sum += ackermann(3, 6);
    }
    
    return (sum > 0) ? 0 : 1;
}
