// Benchmark: Nested loop performance
// Tests: Loop nesting overhead, multiplication

#include <stdint.h>

int main(void) {
    int64_t outer = 1000;
    int64_t inner = 1000;
    int64_t sum = 0;
    
    for (int64_t i = 0; i < outer; i++) {
        for (int64_t j = 0; j < inner; j++) {
            sum += i * j;
        }
    }
    
    return (sum > 0) ? 0 : 1;
}
