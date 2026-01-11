// Benchmark: Matrix multiplication
// Tests: Triple nested loop, memory access patterns

#include <stdint.h>
#include <stdlib.h>

int main(void) {
    int64_t n = 50;
    int64_t iterations = 10;
    
    int64_t *A = malloc(n * n * sizeof(int64_t));
    int64_t *B = malloc(n * n * sizeof(int64_t));
    int64_t *C = malloc(n * n * sizeof(int64_t));
    
    // Initialize matrices
    for (int64_t i = 0; i < n * n; i++) {
        A[i] = i % n;
        B[i] = i / n;
    }
    
    int64_t total = 0;
    for (int64_t iter = 0; iter < iterations; iter++) {
        for (int64_t i = 0; i < n; i++) {
            for (int64_t j = 0; j < n; j++) {
                int64_t sum = 0;
                for (int64_t k = 0; k < n; k++) {
                    sum += A[i * n + k] * B[k * n + j];
                }
                C[i * n + j] = sum;
                total += sum;
            }
        }
    }
    
    free(A);
    free(B);
    free(C);
    return (total > 0) ? 0 : 1;
}
