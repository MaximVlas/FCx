#ifndef FCX_ERROR_RUNTIME_H
#define FCX_ERROR_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>

// Runtime error types
typedef enum {
    RUNTIME_ERROR_NONE = 0,
    RUNTIME_ERROR_SYSCALL_FAILED,
    RUNTIME_ERROR_ALLOCATION_FAILED,
    RUNTIME_ERROR_ATOMIC_CONFLICT,
    RUNTIME_ERROR_NULL_POINTER,
    RUNTIME_ERROR_DIVISION_BY_ZERO,
    RUNTIME_ERROR_STACK_OVERFLOW,
    RUNTIME_ERROR_CUSTOM
} RuntimeErrorType;

// Runtime error structure
typedef struct {
    RuntimeErrorType type;
    int error_code;           // errno or syscall return value
    const char* message;
    const char* function;
    const char* file;
    int line;
    
    // Stack trace (simplified)
    void* stack_frames[16];
    int frame_count;
} RuntimeError;

// Error context for try/catch blocks
typedef struct ErrorContext {
    jmp_buf jump_buffer;
    RuntimeError error;
    struct ErrorContext* parent;
    bool has_error;
} ErrorContext;

// Global error context stack
extern __thread ErrorContext* current_error_context;

// Initialize runtime error system
void fcx_runtime_error_init(void);

// Cleanup runtime error system
void fcx_runtime_error_cleanup(void);

// Set runtime error
void fcx_set_runtime_error(RuntimeErrorType type, int error_code, 
                          const char* message, const char* function,
                          const char* file, int line);

// Get last runtime error
RuntimeError* fcx_get_last_error(void);

// Clear last error
void fcx_clear_error(void);

// Check if there's an error
bool fcx_has_error(void);

// Push error context (for try block)
void fcx_push_error_context(ErrorContext* ctx);

// Pop error context (after try/catch)
void fcx_pop_error_context(void);

// Throw error (longjmp to catch block)
void fcx_throw_error(RuntimeErrorType type, int error_code, const char* message);

// Compact error handling macros for ?! operator
#define FCX_CHECK_ERROR(expr, error_block) \
    do { \
        int __result = (expr); \
        if (__result < 0) { \
            fcx_set_runtime_error(RUNTIME_ERROR_SYSCALL_FAILED, __result, \
                                 #expr, __func__, __FILE__, __LINE__); \
            error_block \
        } \
    } while(0)

// Traditional try/catch support
#define FCX_TRY \
    do { \
        ErrorContext __error_ctx = {0}; \
        fcx_push_error_context(&__error_ctx); \
        if (setjmp(__error_ctx.jump_buffer) == 0) {

#define FCX_CATCH \
        fcx_pop_error_context(); \
        } else { \
            RuntimeError* __err __attribute__((unused)) = &__error_ctx.error;

#define FCX_END_TRY \
            fcx_pop_error_context(); \
        } \
    } while(0)

// Helper functions for specific error types
void fcx_error_syscall_failed(int syscall_num, int result);
void fcx_error_allocation_failed(size_t size);
void fcx_error_atomic_conflict(void* address);
void fcx_error_null_pointer(const char* var_name);
void fcx_error_division_by_zero(void);

// Stack trace capture (simplified)
int fcx_capture_stack_trace(void** frames, int max_frames);
void fcx_print_stack_trace(void** frames, int frame_count);

// Error message formatting
const char* fcx_error_type_string(RuntimeErrorType type);
void fcx_print_runtime_error(const RuntimeError* error);

#endif // FCX_ERROR_RUNTIME_H
