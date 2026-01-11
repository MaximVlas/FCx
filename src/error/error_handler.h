#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Error severity levels
typedef enum {
    ERROR_SEVERITY_WARNING = 0,
    ERROR_SEVERITY_ERROR = 1,
    ERROR_SEVERITY_FATAL = 2
} ErrorSeverity;

// Error types
typedef enum {
    ERROR_TYPE_LEXICAL = 0,
    ERROR_TYPE_SYNTAX,
    ERROR_TYPE_SEMANTIC,
    ERROR_TYPE_TYPE_MISMATCH,
    ERROR_TYPE_UNKNOWN_OPERATOR,
    ERROR_TYPE_OPERATOR_AMBIGUITY,
    ERROR_TYPE_POINTER_TYPE_MISMATCH,
    ERROR_TYPE_CODEGEN,
    ERROR_TYPE_LINK,
    ERROR_TYPE_INTERNAL
} ErrorType;

// Source position for error reporting
typedef struct {
    const char* filename;
    size_t line;
    size_t column;
    size_t length;  // Length of the problematic token/expression
} SourcePosition;

// Operator suggestion for unknown operators
typedef struct {
    const char* operator_symbol;
    int similarity_score;  // 0-100, higher is more similar
    const char* description;
} OperatorSuggestion;

// Compile error structure
typedef struct {
    ErrorType type;
    ErrorSeverity severity;
    SourcePosition position;
    char* message;
    char* context_line;  // The source line where the error occurred
    
    // Type-specific data
    union {
        struct {
            char* expected;
            char* found;
        } syntax_error;
        
        struct {
            char* from_type;
            char* to_type;
            char* hint;  // Suggestion for fixing
        } type_error;
        
        struct {
            char* symbol;
            OperatorSuggestion* suggestions;
            size_t suggestion_count;
        } unknown_operator;
        
        struct {
            char* operator_symbol;
            char** possible_meanings;
            size_t meaning_count;
            char* disambiguation_hint;
        } ambiguity_error;
        
        struct {
            char* pointer_type;  // ptr<T>, rawptr, or byteptr
            char* operation;
            char* reason;
        } pointer_error;
    } details;
    
    bool has_details;
} CompileError;

// Error handler context
typedef struct {
    CompileError** errors;
    size_t error_count;
    size_t error_capacity;
    
    size_t warning_count;
    size_t fatal_error_count;
    
    bool continue_after_error;  // Error recovery flag
    size_t max_errors;  // Maximum errors before stopping
    
    // String pool for memory efficiency
    char** string_pool;
    size_t string_pool_count;
    size_t string_pool_capacity;
} ErrorHandler;

// Initialize error handler
ErrorHandler* error_handler_create(void);

// Cleanup error handler
void error_handler_destroy(ErrorHandler* handler);

// Add a simple error
void error_handler_add_error(ErrorHandler* handler, ErrorType type, 
                            ErrorSeverity severity, SourcePosition pos,
                            const char* message);

// Add error with syntax details
void error_handler_add_syntax_error(ErrorHandler* handler, SourcePosition pos,
                                   const char* expected, const char* found,
                                   const char* message);

// Add error with type mismatch details
void error_handler_add_type_error(ErrorHandler* handler, SourcePosition pos,
                                 const char* from_type, const char* to_type,
                                 const char* hint, const char* message);

// Add error for unknown operator with suggestions
void error_handler_add_unknown_operator(ErrorHandler* handler, SourcePosition pos,
                                       const char* symbol,
                                       OperatorSuggestion* suggestions,
                                       size_t suggestion_count);

// Add error for operator ambiguity
void error_handler_add_ambiguity_error(ErrorHandler* handler, SourcePosition pos,
                                      const char* operator_symbol,
                                      const char** possible_meanings,
                                      size_t meaning_count,
                                      const char* disambiguation_hint);

// Add error for pointer type mismatch
void error_handler_add_pointer_error(ErrorHandler* handler, SourcePosition pos,
                                    const char* pointer_type,
                                    const char* operation,
                                    const char* reason);

// Set context line for the most recent error
void error_handler_set_context(ErrorHandler* handler, const char* context_line);

// Print all errors
void error_handler_print_errors(const ErrorHandler* handler);

// Print a single error with formatting
void error_handler_print_error(const CompileError* error);

// Check if there are any errors
bool error_handler_has_errors(const ErrorHandler* handler);

// Check if there are fatal errors
bool error_handler_has_fatal_errors(const ErrorHandler* handler);

// Get error count
size_t error_handler_get_error_count(const ErrorHandler* handler);

// Clear all errors
void error_handler_clear(ErrorHandler* handler);

// String interning for memory efficiency
const char* error_handler_intern_string(ErrorHandler* handler, const char* str);

// Operator similarity calculation (Levenshtein distance)
int calculate_operator_similarity(const char* op1, const char* op2);

// Generate operator suggestions
OperatorSuggestion* generate_operator_suggestions(const char* unknown_op,
                                                 size_t* out_count);

// Free operator suggestions
void free_operator_suggestions(OperatorSuggestion* suggestions, size_t count);

#endif // ERROR_HANDLER_H
