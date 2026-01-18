// C binary search - compile to .o with: gcc -O3 -c binary_s.c -o binary_s_c.o
#include <stdint.h>

int64_t binary_search_c(int64_t target, int64_t size) {
    int64_t low = 0;
    int64_t high = size - 1;
    int64_t steps = 0;
    
    while (low <= high) {
        int64_t mid = low + (high - low) / 2;
        int64_t arr_mid = mid;  // Simulated: arr[mid] = mid
        steps++;
        
        if (arr_mid == target) return steps;
        if (arr_mid < target) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return steps;
}
