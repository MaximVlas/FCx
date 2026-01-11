// Test harness comparing FCx JSON library with cJSON
// Compile: gcc -O2 test_fcx_json.c -o test_fcx_json -L/tmp -lfcx_json -lcjson -Wl,-rpath,/tmp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

// FCx JSON library functions (from libfcx_json.so)
extern int64_t fcx_is_whitespace(int64_t c);
extern int64_t fcx_is_digit(int64_t c);
extern int64_t fcx_digit_value(int64_t c);
extern int64_t fcx_json_token_type(int64_t c);
extern int64_t fcx_json_check_balance(int64_t obj_open, int64_t obj_close, int64_t arr_open, int64_t arr_close);
extern int64_t fcx_json_parse_digits(int64_t d0, int64_t d1, int64_t d2, int64_t d3, int64_t d4, int64_t d5, int64_t d6, int64_t d7, int64_t d8);
extern int64_t fcx_json_hash8(int64_t c0, int64_t c1, int64_t c2, int64_t c3, int64_t c4, int64_t c5, int64_t c6, int64_t c7);
extern int64_t fcx_json_valid_escape(int64_t c);
extern int64_t fcx_json_valid_string_char(int64_t c);
extern int64_t fcx_json_value_type_from_token(int64_t token_type);
extern int64_t fcx_json_max_depth(void);

// Simple timing helper
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Test whitespace detection
void test_whitespace(void) {
    printf("\n=== Testing Whitespace Detection ===\n");
    
    int passed = 0, total = 0;
    
    // Test whitespace characters
    int ws_chars[] = {' ', '\t', '\n', '\r'};
    for (int i = 0; i < 4; i++) {
        total++;
        if (fcx_is_whitespace(ws_chars[i]) == 1) {
            passed++;
        } else {
            printf("FAIL: '%c' (0x%02x) should be whitespace\n", ws_chars[i], ws_chars[i]);
        }
    }
    
    // Test non-whitespace characters
    int non_ws[] = {'a', '0', '{', '"', 0};
    for (int i = 0; i < 5; i++) {
        total++;
        if (fcx_is_whitespace(non_ws[i]) == 0) {
            passed++;
        } else {
            printf("FAIL: '%c' (0x%02x) should NOT be whitespace\n", non_ws[i], non_ws[i]);
        }
    }
    
    printf("Whitespace: %d/%d tests passed\n", passed, total);
}

// Test digit detection
void test_digits(void) {
    printf("\n=== Testing Digit Detection ===\n");
    
    int passed = 0, total = 0;
    
    // Test digit characters
    for (char c = '0'; c <= '9'; c++) {
        total++;
        if (fcx_is_digit(c) == 1) {
            passed++;
        } else {
            printf("FAIL: '%c' should be a digit\n", c);
        }
    }
    
    // Test non-digit characters
    char non_digits[] = {'a', 'Z', ' ', '-', '.', 0};
    for (int i = 0; non_digits[i]; i++) {
        total++;
        if (fcx_is_digit(non_digits[i]) == 0) {
            passed++;
        } else {
            printf("FAIL: '%c' should NOT be a digit\n", non_digits[i]);
        }
    }
    
    printf("Digits: %d/%d tests passed\n", passed, total);
}

// Test token type detection
void test_token_types(void) {
    printf("\n=== Testing Token Type Detection ===\n");
    
    struct { char c; int64_t expected; const char* name; } tests[] = {
        {0, 0, "EOF"},
        {'{', 1, "object start"},
        {'}', 2, "object end"},
        {'[', 3, "array start"},
        {']', 4, "array end"},
        {':', 5, "colon"},
        {',', 6, "comma"},
        {'"', 7, "string"},
        {'-', 8, "number (negative)"},
        {'0', 8, "number"},
        {'5', 8, "number"},
        {'t', 9, "true"},
        {'f', 10, "false"},
        {'n', 11, "null"},
    };
    
    int passed = 0, total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int64_t result = fcx_json_token_type(tests[i].c);
        if (result == tests[i].expected) {
            passed++;
        } else {
            printf("FAIL: '%c' -> got %ld, expected %ld (%s)\n", 
                   tests[i].c, result, tests[i].expected, tests[i].name);
        }
    }
    
    printf("Token types: %d/%d tests passed\n", passed, total);
}

// Test number parsing
void test_number_parsing(void) {
    printf("\n=== Testing Number Parsing ===\n");
    
    int passed = 0, total = 0;
    
    // Test: "123" -> 123
    total++;
    int64_t result = fcx_json_parse_digits('1', '2', '3', 0, 0, 0, 0, 0, 0);
    if (result == 123) {
        passed++;
    } else {
        printf("FAIL: '123' -> got %ld, expected 123\n", result);
    }
    
    // Test: "42" -> 42
    total++;
    result = fcx_json_parse_digits('4', '2', 0, 0, 0, 0, 0, 0, 0);
    if (result == 42) {
        passed++;
    } else {
        printf("FAIL: '42' -> got %ld, expected 42\n", result);
    }
    
    // Test: "999999999" -> 999999999
    total++;
    result = fcx_json_parse_digits('9', '9', '9', '9', '9', '9', '9', '9', '9');
    if (result == 999999999) {
        passed++;
    } else {
        printf("FAIL: '999999999' -> got %ld, expected 999999999\n", result);
    }
    
    // Test: "0" -> 0
    total++;
    result = fcx_json_parse_digits('0', 0, 0, 0, 0, 0, 0, 0, 0);
    if (result == 0) {
        passed++;
    } else {
        printf("FAIL: '0' -> got %ld, expected 0\n", result);
    }
    
    printf("Number parsing: %d/%d tests passed\n", passed, total);
}

// Test bracket balance checking
void test_bracket_balance(void) {
    printf("\n=== Testing Bracket Balance ===\n");
    
    int passed = 0, total = 0;
    
    // Balanced: 2 open, 2 close for both
    total++;
    if (fcx_json_check_balance(2, 2, 1, 1) == 0) {
        passed++;
    } else {
        printf("FAIL: balanced brackets should return 0\n");
    }
    
    // Unbalanced objects: 3 open, 2 close
    total++;
    if (fcx_json_check_balance(3, 2, 0, 0) != 0) {
        passed++;
    } else {
        printf("FAIL: unbalanced objects should return non-zero\n");
    }
    
    // Unbalanced arrays: 1 open, 2 close
    total++;
    if (fcx_json_check_balance(0, 0, 1, 2) != 0) {
        passed++;
    } else {
        printf("FAIL: unbalanced arrays should return non-zero\n");
    }
    
    printf("Bracket balance: %d/%d tests passed\n", passed, total);
}

// Test string hashing
void test_hashing(void) {
    printf("\n=== Testing String Hashing ===\n");
    
    // Hash "name" and "name" should be equal
    int64_t hash1 = fcx_json_hash8('n', 'a', 'm', 'e', 0, 0, 0, 0);
    int64_t hash2 = fcx_json_hash8('n', 'a', 'm', 'e', 0, 0, 0, 0);
    
    if (hash1 == hash2) {
        printf("PASS: Same strings have same hash (%ld)\n", hash1);
    } else {
        printf("FAIL: Same strings have different hashes (%ld vs %ld)\n", hash1, hash2);
    }
    
    // Hash "name" and "type" should be different
    int64_t hash3 = fcx_json_hash8('t', 'y', 'p', 'e', 0, 0, 0, 0);
    
    if (hash1 != hash3) {
        printf("PASS: Different strings have different hashes (%ld vs %ld)\n", hash1, hash3);
    } else {
        printf("FAIL: Different strings have same hash\n");
    }
}

// Test escape sequence validation
void test_escapes(void) {
    printf("\n=== Testing Escape Sequences ===\n");
    
    int passed = 0, total = 0;
    
    // Valid escapes
    char valid[] = {'"', '\\', '/', 'b', 'f', 'n', 'r', 't', 'u'};
    for (int i = 0; i < 9; i++) {
        total++;
        if (fcx_json_valid_escape(valid[i]) == 1) {
            passed++;
        } else {
            printf("FAIL: '\\%c' should be valid escape\n", valid[i]);
        }
    }
    
    // Invalid escapes
    char invalid[] = {'a', 'x', '0', ' '};
    for (int i = 0; i < 4; i++) {
        total++;
        if (fcx_json_valid_escape(invalid[i]) == 0) {
            passed++;
        } else {
            printf("FAIL: '\\%c' should NOT be valid escape\n", invalid[i]);
        }
    }
    
    printf("Escape sequences: %d/%d tests passed\n", passed, total);
}

// Benchmark: digit parsing
void benchmark_digit_parsing(void) {
    printf("\n=== Benchmark: Digit Parsing ===\n");
    
    const int iterations = 10000000;
    
    // FCx version
    uint64_t start = get_time_ns();
    volatile int64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
        sum += fcx_json_parse_digits('1', '2', '3', '4', '5', '6', '7', '8', '9');
    }
    uint64_t fcx_time = get_time_ns() - start;
    
    // C version (inline)
    start = get_time_ns();
    volatile int64_t sum2 = 0;
    for (int i = 0; i < iterations; i++) {
        int64_t val = 0;
        val = val * 10 + 1;
        val = val * 10 + 2;
        val = val * 10 + 3;
        val = val * 10 + 4;
        val = val * 10 + 5;
        val = val * 10 + 6;
        val = val * 10 + 7;
        val = val * 10 + 8;
        val = val * 10 + 9;
        sum2 += val;
    }
    uint64_t c_time = get_time_ns() - start;
    
    printf("FCx: %lu ns (%lu ns/iter)\n", fcx_time, fcx_time / iterations);
    printf("C:   %lu ns (%lu ns/iter)\n", c_time, c_time / iterations);
    printf("Ratio: %.2fx\n", (double)fcx_time / c_time);
}

// Benchmark: token type detection
void benchmark_token_type(void) {
    printf("\n=== Benchmark: Token Type Detection ===\n");
    
    const int iterations = 10000000;
    const char test_chars[] = "{\"name\":123,[true,false,null]}";
    const int num_chars = sizeof(test_chars) - 1;
    
    // FCx version
    uint64_t start = get_time_ns();
    volatile int64_t sum = 0;
    for (int i = 0; i < iterations; i++) {
        for (int j = 0; j < num_chars; j++) {
            sum += fcx_json_token_type(test_chars[j]);
        }
    }
    uint64_t fcx_time = get_time_ns() - start;
    
    printf("FCx: %lu ns total, %lu ns/char\n", fcx_time, fcx_time / (iterations * num_chars));
}

int main(void) {
    printf("FCx JSON Library Test Suite\n");
    printf("===========================\n");
    
    // Run correctness tests
    test_whitespace();
    test_digits();
    test_token_types();
    test_number_parsing();
    test_bracket_balance();
    test_hashing();
    test_escapes();
    
    // Run benchmarks
    benchmark_digit_parsing();
    benchmark_token_type();
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
