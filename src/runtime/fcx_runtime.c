#include "fcx_runtime.h"
#include <string.h>

// ============================================================================
// Runtime Initialization and Utilities
// ============================================================================

// Initialize entire runtime system
int fcx_runtime_init(void) {
    // Initialize memory manager
    if (fcx_memory_init() != 0) {
        return -1;
    }
    
    // Detect CPU features
    CpuFeatures features = fcx_detect_cpu_features();
    
    // Store features for later use
    g_fcx_memory_manager.alignment = features.alignment_pref;
    
    return 0;
}

// Shutdown runtime system
void fcx_runtime_shutdown(void) {
    // Shutdown memory manager
    fcx_memory_shutdown();
}

// ============================================================================
// Error Handling
// ============================================================================

// Panic with message
void fcx_panic(const char* message) {
    // Write to stderr
    fcx_write_op(2, "FCx PANIC: ", 11);
    
    if (message) {
        size_t len = 0;
        while (message[len]) len++;
        fcx_write_op(2, message, len);
    }
    
    fcx_write_op(2, "\n", 1);
    
    // Exit with error code
    fcx_sys_exit(1);
}

// Assert with message
void fcx_assert(bool condition, const char* message) {
    if (!condition) {
        fcx_panic(message);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

// String length
size_t fcx_strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

// String compare
int fcx_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// String copy
char* fcx_strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

// Memory copy
void* fcx_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

// Memory set
void* fcx_memset(void* dest, int value, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    
    return dest;
}

// Memory compare
int fcx_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    
    return 0;
}

// ============================================================================
// Debug and Diagnostics
// ============================================================================

// Print integer to stderr
void fcx_print_int(int64_t value) {
    char buffer[32];
    int pos = 0;
    bool negative = false;
    
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        while (value > 0) {
            buffer[pos++] = '0' + (value % 10);
            value /= 10;
        }
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse buffer
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    fcx_write_op(2, buffer, pos);
}

// Print hex value to stderr
void fcx_print_hex(uint64_t value) {
    char buffer[18] = "0x";
    const char* hex_digits = "0123456789abcdef";
    int pos = 2;
    
    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        // Convert to hex
        uint64_t temp = value;
        int digits = 0;
        while (temp > 0) {
            temp >>= 4;
            digits++;
        }
        
        for (int i = digits - 1; i >= 0; i--) {
            buffer[pos++] = hex_digits[(value >> (i * 4)) & 0xF];
        }
    }
    
    fcx_write_op(2, buffer, pos);
}

// Print string to stderr
void fcx_print_str(const char* str) {
    if (str) {
        fcx_write_op(2, str, fcx_strlen(str));
    }
}

// Print newline
void fcx_print_newline(void) {
    fcx_write_op(2, "\n", 1);
}

// ============================================================================
// Memory Diagnostics
// ============================================================================

// Print memory statistics
void fcx_print_memory_stats(void) {
    FcxMemoryManager* mgr = &g_fcx_memory_manager;
    
    fcx_print_str("FCx Memory Statistics:\n");
    fcx_print_str("  Heap start: ");
    fcx_print_hex((uint64_t)mgr->heap_start);
    fcx_print_newline();
    
    fcx_print_str("  Heap end: ");
    fcx_print_hex((uint64_t)mgr->heap_end);
    fcx_print_newline();
    
    fcx_print_str("  Heap size: ");
    fcx_print_int(mgr->heap_end - mgr->heap_start);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Total allocated: ");
    fcx_print_int(mgr->total_allocated);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Total freed: ");
    fcx_print_int(mgr->total_freed);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Currently in use: ");
    fcx_print_int(mgr->total_allocated - mgr->total_freed);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Fragmentation: ");
    fcx_print_int(fcx_get_fragmentation());
    fcx_print_str("%\n");
}

// Print CPU features
void fcx_print_cpu_features(void) {
    CpuFeatures features = fcx_detect_cpu_features();
    char vendor[13];
    char model[49];
    
    fcx_get_cpu_vendor(vendor);
    fcx_get_cpu_model(model);
    
    fcx_print_str("FCx CPU Features:\n");
    fcx_print_str("  Vendor: ");
    fcx_print_str(vendor);
    fcx_print_newline();
    
    fcx_print_str("  Model: ");
    fcx_print_str(model);
    fcx_print_newline();
    
    fcx_print_str("  Vector width: ");
    fcx_print_int(features.vector_width);
    fcx_print_str(" bits\n");
    
    fcx_print_str("  Cache line size: ");
    fcx_print_int(features.cache_line_size);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Red zone size: ");
    fcx_print_int(features.red_zone_size);
    fcx_print_str(" bytes\n");
    
    fcx_print_str("  Features: ");
    if (features.features & CPU_FEATURE_SSE2) fcx_print_str("SSE2 ");
    if (features.features & CPU_FEATURE_AVX2) fcx_print_str("AVX2 ");
    if (features.features & CPU_FEATURE_AVX512F) fcx_print_str("AVX512F ");
    if (features.features & CPU_FEATURE_BMI2) fcx_print_str("BMI2 ");
    fcx_print_newline();
}

// ============================================================================
// Benchmark Utilities
// ============================================================================

// Simple benchmark function
void fcx_benchmark(const char* name, BenchmarkFunc func, int iterations) {
    fcx_print_str("Benchmarking: ");
    fcx_print_str(name);
    fcx_print_newline();
    
    uint64_t start = fcx_rdtsc();
    
    for (int i = 0; i < iterations; i++) {
        func();
    }
    
    uint64_t end = fcx_rdtsc();
    uint64_t cycles = end - start;
    
    fcx_print_str("  Total cycles: ");
    fcx_print_int(cycles);
    fcx_print_newline();
    
    fcx_print_str("  Cycles per iteration: ");
    fcx_print_int(cycles / iterations);
    fcx_print_newline();
}

// ============================================================================
// Underscore-prefixed aliases for linker compatibility
// These are called by generated code via _fcx_* naming convention
// Note: _fcx_alloc, _fcx_free, _fcx_syscall, _fcx_write, _fcx_read are 
// already defined in bootstrap.c
// ============================================================================

void _fcx_print_str(const char* str) {
    fcx_print_str(str);
}

void _fcx_print_func(const char* str) {
    // Generic print function - prints string to stdout
    if (str) {
        fcx_write_op(1, str, fcx_strlen(str));  // stdout instead of stderr
    }
}

// Print integer to stdout
void _fcx_print_int(int64_t value) {
    char buffer[32];
    int pos = 0;
    
    if (value == 0) {
        fcx_write_op(1, "0", 1);
        return;
    }
    
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    // Convert to string (reverse order)
    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    fcx_write_op(1, buffer, pos);
}

// Print string to stdout with newline
void _fcx_println(const char* str) {
    _fcx_print_func(str);
    fcx_write_op(1, "\n", 1);
}

// Print integer to stdout with newline
void _fcx_println_int(int64_t value) {
    _fcx_print_int(value);
    fcx_write_op(1, "\n", 1);
}

// Print 128-bit integer to stdout with newline
// Uses __int128 which is supported by GCC/Clang on x86_64
void _fcx_println_i128(__int128 value) {
    char buffer[64];  // 128-bit can have up to 39 decimal digits + sign
    int pos = 0;
    
    if (value == 0) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    bool negative = false;
    unsigned __int128 uval;
    if (value < 0) {
        negative = true;
        uval = (unsigned __int128)(-value);
    } else {
        uval = (unsigned __int128)value;
    }
    
    // Convert to string (reverse order)
    while (uval > 0) {
        buffer[pos++] = '0' + (uval % 10);
        uval /= 10;
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print unsigned 128-bit integer to stdout with newline
void _fcx_println_u128(unsigned __int128 value) {
    char buffer[64];
    int pos = 0;
    
    if (value == 0) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Helper: divide a big integer (stored as array of 64-bit limbs) by 10
// Returns the remainder (0-9), modifies the value in place
// limbs[0] is the least significant limb
static uint8_t bigint_div10(uint64_t* limbs, int num_limbs) {
    unsigned __int128 carry = 0;
    for (int i = num_limbs - 1; i >= 0; i--) {
        unsigned __int128 cur = carry * ((unsigned __int128)1 << 64) + limbs[i];
        limbs[i] = (uint64_t)(cur / 10);
        carry = cur % 10;
    }
    return (uint8_t)carry;
}

// Helper: check if big integer is zero
static bool bigint_is_zero(const uint64_t* limbs, int num_limbs) {
    for (int i = 0; i < num_limbs; i++) {
        if (limbs[i] != 0) return false;
    }
    return true;
}

// Helper: check if big integer is negative (signed, two's complement)
static bool bigint_is_negative(const uint64_t* limbs, int num_limbs) {
    return (limbs[num_limbs - 1] & ((uint64_t)1 << 63)) != 0;
}

// Helper: negate a big integer (two's complement)
static void bigint_negate(uint64_t* limbs, int num_limbs) {
    // Invert all bits
    for (int i = 0; i < num_limbs; i++) {
        limbs[i] = ~limbs[i];
    }
    // Add 1
    uint64_t carry = 1;
    for (int i = 0; i < num_limbs && carry; i++) {
        unsigned __int128 sum = (unsigned __int128)limbs[i] + carry;
        limbs[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
}

// Print signed 256-bit integer to stdout with newline
// Value is passed as pointer to 4 x 64-bit limbs (little-endian)
void _fcx_println_i256(const uint64_t* value) {
    char buffer[128];  // 256-bit can have up to 78 decimal digits + sign
    int pos = 0;
    
    // Copy value to work buffer
    uint64_t limbs[4];
    for (int i = 0; i < 4; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 4)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    bool negative = bigint_is_negative(limbs, 4);
    if (negative) {
        bigint_negate(limbs, 4);
    }
    
    // Convert to decimal
    while (!bigint_is_zero(limbs, 4)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 4);
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print unsigned 256-bit integer to stdout with newline
void _fcx_println_u256(const uint64_t* value) {
    char buffer[128];
    int pos = 0;
    
    uint64_t limbs[4];
    for (int i = 0; i < 4; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 4)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    while (!bigint_is_zero(limbs, 4)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 4);
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print signed 512-bit integer to stdout with newline
void _fcx_println_i512(const uint64_t* value) {
    char buffer[200];  // 512-bit can have up to 155 decimal digits + sign
    int pos = 0;
    
    uint64_t limbs[8];
    for (int i = 0; i < 8; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 8)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    bool negative = bigint_is_negative(limbs, 8);
    if (negative) {
        bigint_negate(limbs, 8);
    }
    
    while (!bigint_is_zero(limbs, 8)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 8);
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print unsigned 512-bit integer to stdout with newline
void _fcx_println_u512(const uint64_t* value) {
    char buffer[200];
    int pos = 0;
    
    uint64_t limbs[8];
    for (int i = 0; i < 8; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 8)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    while (!bigint_is_zero(limbs, 8)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 8);
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print signed 1024-bit integer to stdout with newline
void _fcx_println_i1024(const uint64_t* value) {
    char buffer[400];  // 1024-bit can have up to 309 decimal digits + sign
    int pos = 0;
    
    uint64_t limbs[16];
    for (int i = 0; i < 16; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 16)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    bool negative = bigint_is_negative(limbs, 16);
    if (negative) {
        bigint_negate(limbs, 16);
    }
    
    while (!bigint_is_zero(limbs, 16)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 16);
    }
    
    if (negative) {
        buffer[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print unsigned 1024-bit integer to stdout with newline
void _fcx_println_u1024(const uint64_t* value) {
    char buffer[400];
    int pos = 0;
    
    uint64_t limbs[16];
    for (int i = 0; i < 16; i++) limbs[i] = value[i];
    
    if (bigint_is_zero(limbs, 16)) {
        fcx_write_op(1, "0\n", 2);
        return;
    }
    
    while (!bigint_is_zero(limbs, 16)) {
        buffer[pos++] = '0' + bigint_div10(limbs, 16);
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print float (f32) to stdout with newline
void _fcx_println_f32(float value) {
    char buffer[64];
    int pos = 0;
    
    // Handle special cases
    if (value != value) {  // NaN check
        fcx_write_op(1, "NaN\n", 4);
        return;
    }
    
    // Handle infinity
    if (value > 3.4e38f) {
        fcx_write_op(1, "inf\n", 4);
        return;
    }
    if (value < -3.4e38f) {
        fcx_write_op(1, "-inf\n", 5);
        return;
    }
    
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    // Simple float to string conversion (6 decimal places)
    int64_t int_part = (int64_t)value;
    float frac_part = value - (float)int_part;
    
    // Convert integer part
    if (int_part == 0) {
        buffer[pos++] = '0';
    } else {
        char int_buf[32];
        int int_pos = 0;
        while (int_part > 0) {
            int_buf[int_pos++] = '0' + (int_part % 10);
            int_part /= 10;
        }
        // Reverse
        for (int i = int_pos - 1; i >= 0; i--) {
            buffer[pos++] = int_buf[i];
        }
    }
    
    // Add decimal point and fractional part
    buffer[pos++] = '.';
    for (int i = 0; i < 6; i++) {
        frac_part *= 10;
        int digit = (int)frac_part;
        buffer[pos++] = '0' + digit;
        frac_part -= digit;
    }
    
    // Remove trailing zeros
    while (pos > 2 && buffer[pos-1] == '0') pos--;
    if (buffer[pos-1] == '.') pos++;  // Keep at least one decimal
    
    // Add sign if negative
    if (negative) {
        // Shift everything right
        for (int i = pos; i > 0; i--) {
            buffer[i] = buffer[i-1];
        }
        buffer[0] = '-';
        pos++;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print double (f64) to stdout with newline
void _fcx_println_f64(double value) {
    char buffer[64];
    int pos = 0;
    
    // Handle special cases
    if (value != value) {  // NaN check
        fcx_write_op(1, "NaN\n", 4);
        return;
    }
    
    // Handle infinity
    if (value > 1.7e308) {
        fcx_write_op(1, "inf\n", 4);
        return;
    }
    if (value < -1.7e308) {
        fcx_write_op(1, "-inf\n", 5);
        return;
    }
    
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    // Simple double to string conversion (10 decimal places)
    int64_t int_part = (int64_t)value;
    double frac_part = value - (double)int_part;
    
    // Convert integer part
    if (int_part == 0) {
        buffer[pos++] = '0';
    } else {
        char int_buf[32];
        int int_pos = 0;
        while (int_part > 0) {
            int_buf[int_pos++] = '0' + (int_part % 10);
            int_part /= 10;
        }
        // Reverse
        for (int i = int_pos - 1; i >= 0; i--) {
            buffer[pos++] = int_buf[i];
        }
    }
    
    // Add decimal point and fractional part
    buffer[pos++] = '.';
    for (int i = 0; i < 10; i++) {
        frac_part *= 10;
        int digit = (int)frac_part;
        buffer[pos++] = '0' + digit;
        frac_part -= digit;
    }
    
    // Remove trailing zeros
    while (pos > 2 && buffer[pos-1] == '0') pos--;
    if (buffer[pos-1] == '.') pos++;  // Keep at least one decimal
    
    // Add sign if negative
    if (negative) {
        // Shift everything right
        for (int i = pos; i > 0; i--) {
            buffer[i] = buffer[i-1];
        }
        buffer[0] = '-';
        pos++;
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print boolean to stdout with newline
void _fcx_println_bool(int64_t value) {
    if (value) {
        fcx_write_op(1, "true\n", 5);
    } else {
        fcx_write_op(1, "false\n", 6);
    }
}

// Print pointer as hex to stdout with newline
void _fcx_println_ptr(void* ptr) {
    char buffer[32] = "0x";
    const char* hex_digits = "0123456789abcdef";
    int pos = 2;
    
    uint64_t value = (uint64_t)ptr;
    
    if (value == 0) {
        fcx_write_op(1, "0x0\n", 4);
        return;
    }
    
    // Convert to hex
    char hex_buf[16];
    int hex_pos = 0;
    while (value > 0) {
        hex_buf[hex_pos++] = hex_digits[value & 0xF];
        value >>= 4;
    }
    
    // Reverse
    for (int i = hex_pos - 1; i >= 0; i--) {
        buffer[pos++] = hex_buf[i];
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print single character to stdout with newline
void _fcx_println_char(int64_t value) {
    char buffer[2];
    buffer[0] = (char)value;
    buffer[1] = '\n';
    fcx_write_op(1, buffer, 2);
}

// Print unsigned byte as number to stdout with newline
void _fcx_println_u8(int64_t value) {
    _fcx_println_int(value & 0xFF);
}

// Print integer as hex to stdout with newline
void _fcx_println_hex(int64_t value) {
    char buffer[32] = "0x";
    const char* hex_digits = "0123456789abcdef";
    int pos = 2;
    
    uint64_t uval = (uint64_t)value;
    
    if (uval == 0) {
        fcx_write_op(1, "0x0\n", 4);
        return;
    }
    
    // Convert to hex
    char hex_buf[16];
    int hex_pos = 0;
    while (uval > 0) {
        hex_buf[hex_pos++] = hex_digits[uval & 0xF];
        uval >>= 4;
    }
    
    // Reverse
    for (int i = hex_pos - 1; i >= 0; i--) {
        buffer[pos++] = hex_buf[i];
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// Print integer as binary to stdout with newline
void _fcx_println_bin(int64_t value) {
    char buffer[80] = "0b";
    int pos = 2;
    
    uint64_t uval = (uint64_t)value;
    
    if (uval == 0) {
        fcx_write_op(1, "0b0\n", 4);
        return;
    }
    
    // Find highest set bit
    int highest = 63;
    while (highest > 0 && !((uval >> highest) & 1)) highest--;
    
    // Convert to binary
    for (int i = highest; i >= 0; i--) {
        buffer[pos++] = ((uval >> i) & 1) ? '1' : '0';
    }
    
    buffer[pos++] = '\n';
    fcx_write_op(1, buffer, pos);
}

// ============================================================================
// String Operations
// ============================================================================

// Get string length
size_t _fcx_strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

// Compare two strings, returns 0 if equal
int64_t _fcx_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int64_t)(*(unsigned char*)s1 - *(unsigned char*)s2);
}

// Copy string, returns dest
char* _fcx_strcpy(char* dest, const char* src) {
    if (!dest) return NULL;
    if (!src) {
        dest[0] = '\0';
        return dest;
    }
    
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

// Concatenate strings
char* _fcx_strcat(char* dest, const char* src) {
    if (!dest) return NULL;
    if (!src) return dest;
    
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

// Find character in string
char* _fcx_strchr(const char* str, int64_t c) {
    if (!str) return NULL;
    
    while (*str) {
        if (*str == (char)c) return (char*)str;
        str++;
    }
    return (c == 0) ? (char*)str : NULL;
}

// Find substring
char* _fcx_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

// ============================================================================
// Memory Operations
// ============================================================================

// Copy memory
void* _fcx_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

// Set memory
void* _fcx_memset(void* dest, int64_t value, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    
    return dest;
}

// Compare memory
int64_t _fcx_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int64_t)(p1[i] - p2[i]);
        }
    }
    
    return 0;
}

// Move memory (handles overlapping regions)
void* _fcx_memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    }
    
    return dest;
}

// ============================================================================
// Conversion Functions
// ============================================================================

// Convert string to integer
int64_t _fcx_atoi(const char* str) {
    if (!str) return 0;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n') str++;
    
    bool negative = false;
    if (*str == '-') {
        negative = true;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    int64_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return negative ? -result : result;
}

// Convert integer to string (returns length)
int64_t _fcx_itoa(int64_t value, char* buffer, int64_t base) {
    if (!buffer || base < 2 || base > 36) return 0;
    
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    char temp[65];
    int pos = 0;
    
    bool negative = false;
    uint64_t uval;
    
    if (value < 0 && base == 10) {
        negative = true;
        uval = (uint64_t)(-value);
    } else {
        uval = (uint64_t)value;
    }
    
    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return 1;
    }
    
    while (uval > 0) {
        temp[pos++] = digits[uval % base];
        uval /= base;
    }
    
    int out_pos = 0;
    if (negative) {
        buffer[out_pos++] = '-';
    }
    
    // Reverse
    for (int i = pos - 1; i >= 0; i--) {
        buffer[out_pos++] = temp[i];
    }
    buffer[out_pos] = '\0';
    
    return out_pos;
}

void* _fcx_arena_alloc(size_t size, size_t alignment, uint32_t scope_id) {
    return fcx_arena_alloc(size, alignment, scope_id);
}

void* _fcx_slab_alloc(size_t object_size, uint32_t type_hash) {
    return fcx_slab_alloc(object_size, type_hash);
}

void* _fcx_pool_alloc(size_t object_size, size_t capacity, bool overflow) {
    return fcx_pool_alloc(object_size, capacity, overflow);
}

bool _fcx_atomic_cas(volatile uint64_t* ptr, uint64_t expected, uint64_t new_val) {
    return fcx_atomic_cas(ptr, expected, new_val);
}

uint64_t _fcx_atomic_swap(volatile uint64_t* ptr, uint64_t val) {
    return fcx_atomic_swap(ptr, val);
}

void _fcx_memory_barrier(void) {
    fcx_barrier_full();
}
