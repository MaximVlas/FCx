// Benchmark: Binary search
// Tests: Divide and conquer, comparisons

#include <stdint.h>

static inline int64_t binary_search(int64_t target, int64_t size) {
    int64_t low = 0;
    int64_t high = size - 1;
    int64_t steps = 0;
    
    while (low <= high) {
        int64_t mid = (low + high) / 2;
        steps++;
        
        if (mid == target) return steps;
        if (mid < target) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return steps;
}

int main(void) {
    int64_t size = 1000000;
    int64_t iterations = 1000000;
    int64_t total = 0;
    
    for (int64_t i = 0; i < iterations; i++) {
        int64_t target = (i * 7) % size;
        total += binary_search(target, size);
    }
    
    return (total > 0) ? 0 : 1;
}
