#define _POSIX_C_SOURCE 200809L
#include "error_handler.h"
#include "../lexer/lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Forward declarations for operator registry access
extern const OperatorInfo* get_operator_registry(void);
extern size_t get_operator_registry_size(void);

// ANSI color codes for terminal output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// Initialize error handler
ErrorHandler* error_handler_create(void) {
    ErrorHandler* handler = malloc(sizeof(ErrorHandler));
    if (!handler) return NULL;
    
    handler->errors = NULL;
    handler->error_count = 0;
    handler->error_capacity = 0;
    handler->warning_count = 0;
    handler->fatal_error_count = 0;
    handler->continue_after_error = true;
    handler->max_errors = 100;  // Stop after 100 errors
    
    handler->string_pool = NULL;
    handler->string_pool_count = 0;
    handler->string_pool_capacity = 0;
    
    return handler;
}

// Cleanup error handler
void error_handler_destroy(ErrorHandler* handler) {
    if (!handler) return;
    
    // Free all errors
    for (size_t i = 0; i < handler->error_count; i++) {
        CompileError* error = handler->errors[i];
        if (!error) continue;
        
        free(error->message);
        free(error->context_line);
        
        if (error->has_details) {
            switch (error->type) {
                case ERROR_TYPE_SYNTAX:
                    free(error->details.syntax_error.expected);
                    free(error->details.syntax_error.found);
                    break;
                case ERROR_TYPE_TYPE_MISMATCH:
                case ERROR_TYPE_POINTER_TYPE_MISMATCH:
                    free(error->details.type_error.from_type);
                    free(error->details.type_error.to_type);
                    free(error->details.type_error.hint);
                    break;
                case ERROR_TYPE_UNKNOWN_OPERATOR:
                    free(error->details.unknown_operator.symbol);
                    free_operator_suggestions(error->details.unknown_operator.suggestions,
                                            error->details.unknown_operator.suggestion_count);
                    break;
                case ERROR_TYPE_OPERATOR_AMBIGUITY:
                    free(error->details.ambiguity_error.operator_symbol);
                    for (size_t j = 0; j < error->details.ambiguity_error.meaning_count; j++) {
                        free(error->details.ambiguity_error.possible_meanings[j]);
                    }
                    free(error->details.ambiguity_error.possible_meanings);
                    free(error->details.ambiguity_error.disambiguation_hint);
                    break;
                default:
                    break;
            }
        }
        
        free(error);
    }
    free(handler->errors);
    
    // Free string pool
    for (size_t i = 0; i < handler->string_pool_count; i++) {
        free(handler->string_pool[i]);
    }
    free(handler->string_pool);
    
    free(handler);
}

// String interning for memory efficiency
const char* error_handler_intern_string(ErrorHandler* handler, const char* str) {
    if (!str) return NULL;
    
    // Check if string already exists in pool
    for (size_t i = 0; i < handler->string_pool_count; i++) {
        if (strcmp(handler->string_pool[i], str) == 0) {
            return handler->string_pool[i];
        }
    }
    
    // Add new string to pool
    if (handler->string_pool_count >= handler->string_pool_capacity) {
        size_t new_capacity = handler->string_pool_capacity ? handler->string_pool_capacity * 2 : 16;
        char** new_pool = realloc(handler->string_pool, new_capacity * sizeof(char*));
        if (!new_pool) return NULL;
        handler->string_pool = new_pool;
        handler->string_pool_capacity = new_capacity;
    }
    
    char* interned = strdup(str);
    if (!interned) return NULL;
    
    handler->string_pool[handler->string_pool_count++] = interned;
    return interned;
}

// Helper to add error to handler
static CompileError* add_error_internal(ErrorHandler* handler, ErrorType type,
                                       ErrorSeverity severity, SourcePosition pos) {
    if (handler->error_count >= handler->max_errors) {
        return NULL;  // Too many errors
    }
    
    if (handler->error_count >= handler->error_capacity) {
        size_t new_capacity = handler->error_capacity ? handler->error_capacity * 2 : 16;
        CompileError** new_errors = realloc(handler->errors, new_capacity * sizeof(CompileError*));
        if (!new_errors) return NULL;
        handler->errors = new_errors;
        handler->error_capacity = new_capacity;
    }
    
    CompileError* error = calloc(1, sizeof(CompileError));
    if (!error) return NULL;
    
    error->type = type;
    error->severity = severity;
    error->position = pos;
    error->position.filename = error_handler_intern_string(handler, pos.filename);
    error->has_details = false;
    
    handler->errors[handler->error_count++] = error;
    
    if (severity == ERROR_SEVERITY_WARNING) {
        handler->warning_count++;
    } else if (severity == ERROR_SEVERITY_FATAL) {
        handler->fatal_error_count++;
    }
    
    return error;
}

// Add a simple error
void error_handler_add_error(ErrorHandler* handler, ErrorType type,
                            ErrorSeverity severity, SourcePosition pos,
                            const char* message) {
    CompileError* error = add_error_internal(handler, type, severity, pos);
    if (!error) return;
    
    error->message = strdup(message);
}

// Add error with syntax details
void error_handler_add_syntax_error(ErrorHandler* handler, SourcePosition pos,
                                   const char* expected, const char* found,
                                   const char* message) {
    CompileError* error = add_error_internal(handler, ERROR_TYPE_SYNTAX,
                                            ERROR_SEVERITY_ERROR, pos);
    if (!error) return;
    
    error->message = strdup(message);
    error->has_details = true;
    error->details.syntax_error.expected = strdup(expected);
    error->details.syntax_error.found = strdup(found);
}

// Add error with type mismatch details
void error_handler_add_type_error(ErrorHandler* handler, SourcePosition pos,
                                 const char* from_type, const char* to_type,
                                 const char* hint, const char* message) {
    CompileError* error = add_error_internal(handler, ERROR_TYPE_TYPE_MISMATCH,
                                            ERROR_SEVERITY_ERROR, pos);
    if (!error) return;
    
    error->message = strdup(message);
    error->has_details = true;
    error->details.type_error.from_type = strdup(from_type);
    error->details.type_error.to_type = strdup(to_type);
    error->details.type_error.hint = hint ? strdup(hint) : NULL;
}

// Add error for unknown operator with suggestions
void error_handler_add_unknown_operator(ErrorHandler* handler, SourcePosition pos,
                                       const char* symbol,
                                       OperatorSuggestion* suggestions,
                                       size_t suggestion_count) {
    CompileError* error = add_error_internal(handler, ERROR_TYPE_UNKNOWN_OPERATOR,
                                            ERROR_SEVERITY_ERROR, pos);
    if (!error) return;
    
    char message[256];
    snprintf(message, sizeof(message), "Unknown operator: '%s'", symbol);
    error->message = strdup(message);
    error->has_details = true;
    error->details.unknown_operator.symbol = strdup(symbol);
    error->details.unknown_operator.suggestions = suggestions;
    error->details.unknown_operator.suggestion_count = suggestion_count;
}

// Add error for operator ambiguity
void error_handler_add_ambiguity_error(ErrorHandler* handler, SourcePosition pos,
                                      const char* operator_symbol,
                                      const char** possible_meanings,
                                      size_t meaning_count,
                                      const char* disambiguation_hint) {
    CompileError* error = add_error_internal(handler, ERROR_TYPE_OPERATOR_AMBIGUITY,
                                            ERROR_SEVERITY_ERROR, pos);
    if (!error) return;
    
    char message[256];
    snprintf(message, sizeof(message), "Ambiguous operator: '%s'", operator_symbol);
    error->message = strdup(message);
    error->has_details = true;
    error->details.ambiguity_error.operator_symbol = strdup(operator_symbol);
    
    // Copy possible meanings
    error->details.ambiguity_error.possible_meanings = malloc(meaning_count * sizeof(char*));
    for (size_t i = 0; i < meaning_count; i++) {
        error->details.ambiguity_error.possible_meanings[i] = strdup(possible_meanings[i]);
    }
    error->details.ambiguity_error.meaning_count = meaning_count;
    error->details.ambiguity_error.disambiguation_hint = disambiguation_hint ? strdup(disambiguation_hint) : NULL;
}

// Add error for pointer type mismatch
void error_handler_add_pointer_error(ErrorHandler* handler, SourcePosition pos,
                                    const char* pointer_type,
                                    const char* operation,
                                    const char* reason) {
    CompileError* error = add_error_internal(handler, ERROR_TYPE_POINTER_TYPE_MISMATCH,
                                            ERROR_SEVERITY_ERROR, pos);
    if (!error) return;
    
    char message[256];
    snprintf(message, sizeof(message), "Invalid pointer operation on %s", pointer_type);
    error->message = strdup(message);
    error->has_details = true;
    error->details.pointer_error.pointer_type = strdup(pointer_type);
    error->details.pointer_error.operation = strdup(operation);
    error->details.pointer_error.reason = strdup(reason);
}

// Set context line for the most recent error
void error_handler_set_context(ErrorHandler* handler, const char* context_line) {
    if (handler->error_count == 0) return;
    
    CompileError* error = handler->errors[handler->error_count - 1];
    if (error->context_line) {
        free(error->context_line);
    }
    error->context_line = strdup(context_line);
}

// Print a single error with formatting
void error_handler_print_error(const CompileError* error) {
    if (!error) return;
    
    // Print error header with color
    const char* severity_str;
    const char* color;
    
    switch (error->severity) {
        case ERROR_SEVERITY_WARNING:
            severity_str = "warning";
            color = COLOR_YELLOW;
            break;
        case ERROR_SEVERITY_FATAL:
            severity_str = "fatal error";
            color = COLOR_RED;
            break;
        default:
            severity_str = "error";
            color = COLOR_RED;
            break;
    }
    
    printf("%s%s%s: ", COLOR_BOLD, severity_str, COLOR_RESET);
    printf("%s\n", error->message);
    
    // Print location
    if (error->position.filename) {
        printf("  %s--> %s%s:%zu:%zu%s\n", COLOR_CYAN, COLOR_RESET,
               error->position.filename, error->position.line, error->position.column,
               COLOR_RESET);
    }
    
    // Print context line if available
    if (error->context_line) {
        printf("   %s|\n", COLOR_CYAN);
        printf("%3zu| %s%s\n", error->position.line, COLOR_RESET, error->context_line);
        printf("   %s|", COLOR_CYAN);
        
        // Print caret pointing to error location
        for (size_t i = 0; i < error->position.column - 1; i++) {
            printf(" ");
        }
        printf("%s", color);
        for (size_t i = 0; i < (error->position.length > 0 ? error->position.length : 1); i++) {
            printf("^");
        }
        printf("%s\n", COLOR_RESET);
    }
    
    // Print type-specific details
    if (error->has_details) {
        switch (error->type) {
            case ERROR_TYPE_SYNTAX:
                printf("   %s= note:%s expected '%s', found '%s'\n", COLOR_CYAN, COLOR_RESET,
                       error->details.syntax_error.expected,
                       error->details.syntax_error.found);
                break;
                
            case ERROR_TYPE_TYPE_MISMATCH:
            case ERROR_TYPE_POINTER_TYPE_MISMATCH:
                printf("   %s= note:%s cannot convert from '%s' to '%s'\n", COLOR_CYAN, COLOR_RESET,
                       error->details.type_error.from_type,
                       error->details.type_error.to_type);
                if (error->details.type_error.hint) {
                    printf("   %s= help:%s %s\n", COLOR_CYAN, COLOR_RESET,
                           error->details.type_error.hint);
                }
                break;
                
            case ERROR_TYPE_UNKNOWN_OPERATOR:
                if (error->details.unknown_operator.suggestion_count > 0) {
                    printf("   %s= help:%s did you mean one of these?\n", COLOR_CYAN, COLOR_RESET);
                    for (size_t i = 0; i < error->details.unknown_operator.suggestion_count && i < 5; i++) {
                        OperatorSuggestion* sug = &error->details.unknown_operator.suggestions[i];
                        printf("          '%s' - %s\n", sug->operator_symbol, sug->description);
                    }
                }
                break;
                
            case ERROR_TYPE_OPERATOR_AMBIGUITY:
                printf("   %s= note:%s operator '%s' could mean:\n", COLOR_CYAN, COLOR_RESET,
                       error->details.ambiguity_error.operator_symbol);
                for (size_t i = 0; i < error->details.ambiguity_error.meaning_count; i++) {
                    printf("          %zu. %s\n", i + 1,
                           error->details.ambiguity_error.possible_meanings[i]);
                }
                if (error->details.ambiguity_error.disambiguation_hint) {
                    printf("   %s= help:%s %s\n", COLOR_CYAN, COLOR_RESET,
                           error->details.ambiguity_error.disambiguation_hint);
                }
                break;
                
            default:
                break;
        }
    }
    
    printf("\n");
}

// Print all errors
void error_handler_print_errors(const ErrorHandler* handler) {
    if (!handler || handler->error_count == 0) return;
    
    for (size_t i = 0; i < handler->error_count; i++) {
        error_handler_print_error(handler->errors[i]);
    }
    
    // Print summary
    size_t error_count = handler->error_count - handler->warning_count;
    if (error_count > 0 || handler->warning_count > 0) {
        printf("%s%s", COLOR_BOLD, COLOR_RED);
        if (error_count > 0) {
            printf("%zu error%s", error_count, error_count == 1 ? "" : "s");
        }
        if (handler->warning_count > 0) {
            if (error_count > 0) printf(", ");
            printf("%s%zu warning%s", COLOR_YELLOW, handler->warning_count,
                   handler->warning_count == 1 ? "" : "s");
        }
        printf("%s generated\n", COLOR_RESET);
    }
}

// Check if there are any errors
bool error_handler_has_errors(const ErrorHandler* handler) {
    return handler && (handler->error_count - handler->warning_count) > 0;
}

// Check if there are fatal errors
bool error_handler_has_fatal_errors(const ErrorHandler* handler) {
    return handler && handler->fatal_error_count > 0;
}

// Get error count
size_t error_handler_get_error_count(const ErrorHandler* handler) {
    return handler ? handler->error_count : 0;
}

// Clear all errors
void error_handler_clear(ErrorHandler* handler) {
    if (!handler) return;
    
    for (size_t i = 0; i < handler->error_count; i++) {
        CompileError* error = handler->errors[i];
        free(error->message);
        free(error->context_line);
        free(error);
    }
    
    handler->error_count = 0;
    handler->warning_count = 0;
    handler->fatal_error_count = 0;
}

// Levenshtein distance for string similarity
int calculate_operator_similarity(const char* op1, const char* op2) {
    if (!op1 || !op2) return 0;
    
    size_t len1 = strlen(op1);
    size_t len2 = strlen(op2);
    
    if (len1 == 0) return (int)len2 * 10;
    if (len2 == 0) return (int)len1 * 10;
    
    // Use dynamic programming for Levenshtein distance
    int matrix[len1 + 1][len2 + 1];
    
    for (size_t i = 0; i <= len1; i++) {
        matrix[i][0] = (int)i;
    }
    for (size_t j = 0; j <= len2; j++) {
        matrix[0][j] = (int)j;
    }
    
    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (op1[i - 1] == op2[j - 1]) ? 0 : 1;
            
            int deletion = matrix[i - 1][j] + 1;
            int insertion = matrix[i][j - 1] + 1;
            int substitution = matrix[i - 1][j - 1] + cost;
            
            matrix[i][j] = deletion < insertion ? deletion : insertion;
            matrix[i][j] = matrix[i][j] < substitution ? matrix[i][j] : substitution;
        }
    }
    
    int distance = matrix[len1][len2];
    
    // Convert to similarity score (0-100)
    int max_len = (int)(len1 > len2 ? len1 : len2);
    int similarity = 100 - (distance * 100 / max_len);
    
    return similarity > 0 ? similarity : 0;
}

// Generate operator suggestions
OperatorSuggestion* generate_operator_suggestions(const char* unknown_op, size_t* out_count) {
    if (!unknown_op || !out_count) return NULL;
    
    // Get all operators from registry
    const OperatorInfo* registry = get_operator_registry();
    size_t registry_size = get_operator_registry_size();
    
    // Calculate similarity for all operators
    typedef struct {
        const OperatorInfo* op_info;
        int similarity;
    } ScoredOperator;
    
    ScoredOperator* scored = malloc(registry_size * sizeof(ScoredOperator));
    if (!scored) return NULL;
    
    size_t scored_count = 0;
    for (size_t i = 0; i < registry_size; i++) {
        int similarity = calculate_operator_similarity(unknown_op, registry[i].symbol);
        
        // Only include operators with similarity > 40
        if (similarity > 40) {
            scored[scored_count].op_info = &registry[i];
            scored[scored_count].similarity = similarity;
            scored_count++;
        }
    }
    
    // Sort by similarity (bubble sort for small arrays)
    for (size_t i = 0; i < scored_count; i++) {
        for (size_t j = i + 1; j < scored_count; j++) {
            if (scored[j].similarity > scored[i].similarity) {
                ScoredOperator temp = scored[i];
                scored[i] = scored[j];
                scored[j] = temp;
            }
        }
    }
    
    // Create suggestions (top 5)
    size_t suggestion_count = scored_count < 5 ? scored_count : 5;
    OperatorSuggestion* suggestions = malloc(suggestion_count * sizeof(OperatorSuggestion));
    if (!suggestions) {
        free(scored);
        return NULL;
    }
    
    for (size_t i = 0; i < suggestion_count; i++) {
        suggestions[i].operator_symbol = strdup(scored[i].op_info->symbol);
        suggestions[i].similarity_score = scored[i].similarity;
        suggestions[i].description = strdup(scored[i].op_info->semantics);
    }
    
    free(scored);
    *out_count = suggestion_count;
    return suggestions;
}

// Free operator suggestions
void free_operator_suggestions(OperatorSuggestion* suggestions, size_t count) {
    if (!suggestions) return;
    
    for (size_t i = 0; i < count; i++) {
        free((char*)suggestions[i].operator_symbol);
        free((char*)suggestions[i].description);
    }
    free(suggestions);
}
