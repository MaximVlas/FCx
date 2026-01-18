#include <stdio.h>
#include <time.h>

long fibonacci(long n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    long result = fibonacci(35);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long ms = (end.tv_sec - start.tv_sec) * 1000 + 
              (end.tv_nsec - start.tv_nsec) / 1000000;
    
    printf("fib(35) = %ld\n", result);
    printf("Time: %ld ms\n", ms);
    return 0;
}
