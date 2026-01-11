#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "fcx_error_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <execinfo.h>

// Thread-local error context stack
__thread ErrorContext* current_error_context = NULL;

// Thread-local last error
__thread RuntimeError last_error = {0};

// Initialize runtime error system
void fcx_runtime_error_init(void) {
    current_error_context = NULL;
    memset(&last_error, 0, sizeof(RuntimeError));
}

// Cleanup runtime error system
void fcx_runtime_error_cleanup(void) {
    current_error_context = NULL;
}

// Set runtime error
void fcx_set_runtime_error(RuntimeErrorType type, int error_code,
                          const char* message, const char* function,
                          const char* file, int line) {
    last_error.type = type;
    last_error.error_code = error_code;
    last_error.message = message;
    last_error.function = function;
    last_error.file = file;
    last_error.line = line;
    
    // Capture stack trace
    last_error.frame_count = fcx_capture_stack_trace(last_error.stack_frames, 16);
    
    // If we're in a try block, copy error to context
    if (current_error_context) {
        current_error_context->error = last_error;
        current_error_context->has_error = true;
    }
}

// Get last runtime error
RuntimeError* fcx_get_last_error(void) {
    return &last_error;
}

// Clear last error
void fcx_clear_error(void) {
    memset(&last_error, 0, sizeof(RuntimeError));
}

// Check if there's an error
bool fcx_has_error(void) {
    return last_error.type != RUNTIME_ERROR_NONE;
}

// Push error context (for try block)
void fcx_push_error_context(ErrorContext* ctx) {
    ctx->parent = current_error_context;
    ctx->has_error = false;
    memset(&ctx->error, 0, sizeof(RuntimeError));
    current_error_context = ctx;
}

// Pop error context (after try/catch)
void fcx_pop_error_context(void) {
    if (current_error_context) {
        current_error_context = current_error_context->parent;
    }
}

// Throw error (longjmp to catch block)
void fcx_throw_error(RuntimeErrorType type, int error_code, const char* message) {
    if (current_error_context) {
        fcx_set_runtime_error(type, error_code, message, "throw", "<unknown>", 0);
        longjmp(current_error_context->jump_buffer, 1);
    } else {
        // No catch block - print error and abort
        fprintf(stderr, "Uncaught runtime error: %s (code: %d)\n", 
                message, error_code);
        abort();
    }
}

// Helper functions for specific error types
void fcx_error_syscall_failed(int syscall_num, int result) {
    char message[256];
    snprintf(message, sizeof(message), 
             "Syscall %d failed with result %d (errno: %d)", 
             syscall_num, result, errno);
    fcx_set_runtime_error(RUNTIME_ERROR_SYSCALL_FAILED, result, 
                         strdup(message), __func__, __FILE__, __LINE__);
}

void fcx_error_allocation_failed(size_t size) {
    char message[256];
    snprintf(message, sizeof(message), 
             "Memory allocation failed for %zu bytes", size);
    fcx_set_runtime_error(RUNTIME_ERROR_ALLOCATION_FAILED, (int)size,
                         strdup(message), __func__, __FILE__, __LINE__);
}

void fcx_error_atomic_conflict(void* address) {
    char message[256];
    snprintf(message, sizeof(message), 
             "Atomic operation conflict at address %p", address);
    fcx_set_runtime_error(RUNTIME_ERROR_ATOMIC_CONFLICT, 0,
                         strdup(message), __func__, __FILE__, __LINE__);
}

void fcx_error_null_pointer(const char* var_name) {
    char message[256];
    snprintf(message, sizeof(message), 
             "Null pointer dereference: %s", var_name);
    fcx_set_runtime_error(RUNTIME_ERROR_NULL_POINTER, 0,
                         strdup(message), __func__, __FILE__, __LINE__);
}

void fcx_error_division_by_zero(void) {
    fcx_set_runtime_error(RUNTIME_ERROR_DIVISION_BY_ZERO, 0,
                         "Division by zero", __func__, __FILE__, __LINE__);
}

// Stack trace capture using backtrace
int fcx_capture_stack_trace(void** frames, int max_frames) {
#ifdef __linux__
    return backtrace(frames, max_frames);
#else
    // Fallback for systems without backtrace
    (void)frames;
    (void)max_frames;
    return 0;
#endif
}

// Print stack trace
void fcx_print_stack_trace(void** frames, int frame_count) {
#ifdef __linux__
    char** symbols = backtrace_symbols(frames, frame_count);
    if (symbols) {
        fprintf(stderr, "Stack trace:\n");
        for (int i = 0; i < frame_count; i++) {
            fprintf(stderr, "  [%d] %s\n", i, symbols[i]);
        }
        free(symbols);
    }
#else
    (void)frames;
    (void)frame_count;
    fprintf(stderr, "Stack trace not available on this platform\n");
#endif
}

// Error type to string
const char* fcx_error_type_string(RuntimeErrorType type) {
    switch (type) {
        case RUNTIME_ERROR_NONE:
            return "No error";
        case RUNTIME_ERROR_SYSCALL_FAILED:
            return "Syscall failed";
        case RUNTIME_ERROR_ALLOCATION_FAILED:
            return "Memory allocation failed";
        case RUNTIME_ERROR_ATOMIC_CONFLICT:
            return "Atomic operation conflict";
        case RUNTIME_ERROR_NULL_POINTER:
            return "Null pointer dereference";
        case RUNTIME_ERROR_DIVISION_BY_ZERO:
            return "Division by zero";
        case RUNTIME_ERROR_STACK_OVERFLOW:
            return "Stack overflow";
        case RUNTIME_ERROR_CUSTOM:
            return "Custom error";
        default:
            return "Unknown error";
    }
}

// Print runtime error with full details
void fcx_print_runtime_error(const RuntimeError* error) {
    if (!error || error->type == RUNTIME_ERROR_NONE) {
        return;
    }
    
    fprintf(stderr, "\n=== Runtime Error ===\n");
    fprintf(stderr, "Type: %s\n", fcx_error_type_string(error->type));
    fprintf(stderr, "Message: %s\n", error->message ? error->message : "<no message>");
    fprintf(stderr, "Error code: %d\n", error->error_code);
    
    if (error->function) {
        fprintf(stderr, "Function: %s\n", error->function);
    }
    if (error->file) {
        fprintf(stderr, "Location: %s:%d\n", error->file, error->line);
    }
    
    if (error->frame_count > 0) {
        fprintf(stderr, "\n");
        fcx_print_stack_trace((void**)error->stack_frames, error->frame_count);
    }
    
    fprintf(stderr, "====================\n\n");
}
