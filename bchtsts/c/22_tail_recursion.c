// Benchmark: Tail recursion (potential optimization)
// Tests: Tail call optimization

#include <stdint.h>

int64_t sum_tail(int64_t n, int64_t acc) {
    if (n == 0) return acc;
    return sum_tail(n - 1, acc + n);
}

int main(void) {
    int64_t iterations = 100000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        sum += sum_tail(100, 0);
    }
    
    return (sum > 0) ? 0 : 1;
}
