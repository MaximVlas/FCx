// Benchmark: Array summation
// Tests: Sequential memory access

#include <stdint.h>
#include <stdlib.h>

int main(void) {
    int64_t size = 10000;
    int64_t iterations = 1000;
    int64_t *arr = malloc(size * sizeof(int64_t));
    
    // Initialize array
    for (int64_t i = 0; i < size; i++) {
        arr[i] = i;
    }
    
    int64_t total = 0;
    for (int64_t iter = 0; iter < iterations; iter++) {
        int64_t sum = 0;
        for (int64_t i = 0; i < size; i++) {
            sum += arr[i];
        }
        total += sum;
    }
    
    free(arr);
    return (total > 0) ? 0 : 1;
}
