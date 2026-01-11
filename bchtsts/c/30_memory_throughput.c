// Benchmark: Memory throughput
// Tests: Sequential read/write patterns

#include <stdint.h>
#include <stdlib.h>

int main(void) {
    int64_t size = 100000;
    int64_t iterations = 100;
    int64_t *buffer = malloc(size * sizeof(int64_t));
    
    int64_t total = 0;
    for (int64_t iter = 0; iter < iterations; iter++) {
        // Write pass
        for (int64_t i = 0; i < size; i++) {
            buffer[i] = i * 3;
        }
        
        // Read pass
        int64_t sum = 0;
        for (int64_t i = 0; i < size; i++) {
            sum += buffer[i];
        }
        
        total += sum;
    }
    
    free(buffer);
    return (total > 0) ? 0 : 1;
}
