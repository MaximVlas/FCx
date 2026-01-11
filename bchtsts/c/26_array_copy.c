// Benchmark: Array copy
// Tests: Memory copy throughput

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    int64_t size = 10000;
    int64_t iterations = 500;
    int64_t *src = malloc(size * sizeof(int64_t));
    int64_t *dst = malloc(size * sizeof(int64_t));
    
    // Initialize source
    for (int64_t i = 0; i < size; i++) {
        src[i] = i * 2 + 1;
    }
    
    int64_t total = 0;
    for (int64_t iter = 0; iter < iterations; iter++) {
        memcpy(dst, src, size * sizeof(int64_t));
        int64_t sum = 0;
        for (int64_t i = 0; i < size; i++) {
            sum += dst[i];
        }
        total += sum;
    }
    
    free(src);
    free(dst);
    return (total > 0) ? 0 : 1;
}
