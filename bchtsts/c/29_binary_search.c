#include <stdint.h>
#include <stdlib.h>

static inline int64_t binary_search(const int64_t* arr, int64_t target, int64_t size) {
    int64_t low = 0;
    int64_t high = size - 1;
    int64_t steps = 0;
    
    while (low <= high) {
        int64_t mid = low + (high - low) / 2;  // Avoid overflow
        steps++;
        
        if (arr[mid] == target) return steps;
        if (arr[mid] < target) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return steps;  // Not found
}

int main(void) {
    int64_t size = 1000000;
    int64_t iterations = 1000000;
    
    // Create sorted array
    int64_t* arr = malloc(size * sizeof(int64_t));
    for (int64_t i = 0; i < size; i++) {
        arr[i] = i;
    }
    
    int64_t total = 0;
    for (int64_t i = 0; i < iterations; i++) {
        int64_t target = (i * 7) % size;
        total += binary_search(arr, target, size);
    }
    
    free(arr);
    return (total > 0) ? 0 : 1;
}