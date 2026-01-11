// Benchmark: Decrement loop performance
// Tests: Subtraction in loops

#include <stdint.h>

static inline int64_t decrement_sum(int64_t start, int64_t count) {
    int64_t sum = 0;
    int64_t val = start;
    for (int64_t i = 0; i < count; i++) {
        sum += val;
        val--;
        if (val < 0) val = start;
    }
    return sum;
}

int main(void) {
    int64_t result = decrement_sum(1000, 10000000);
    return (result > 0) ? 0 : 1;
}
