// Benchmark: Bubble sort
// Tests: Nested loops, comparisons, swaps

#include <stdint.h>
#include <stdlib.h>

int main(void) {
    int64_t n = 500;
    int64_t iterations = 10;
    int64_t *arr = malloc(n * sizeof(int64_t));
    
    int64_t total = 0;
    for (int64_t iter = 0; iter < iterations; iter++) {
        // Initialize array in reverse order
        for (int64_t i = 0; i < n; i++) {
            arr[i] = (n - i) * 17;
        }
        
        // Bubble sort
        int64_t swaps = 0;
        for (int64_t i = 0; i < n - 1; i++) {
            for (int64_t j = 0; j < n - i - 1; j++) {
                if (arr[j] > arr[j + 1]) {
                    int64_t temp = arr[j];
                    arr[j] = arr[j + 1];
                    arr[j + 1] = temp;
                    swaps++;
                }
            }
        }
        total += swaps;
    }
    
    free(arr);
    return (total > 0) ? 0 : 1;
}
