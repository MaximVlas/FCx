/**
 * FCx Preprocessor - C-style preprocessing for FCX language
 * 
 * Supports:
 *   #include "file.h"    - Include local header file
 *   #include <file.h>    - Include system header file
 *   #define NAME value   - Object-like macro
 *   #define NAME(x) expr - Function-like macro
 *   #undef NAME          - Undefine macro
 *   #ifdef NAME          - Conditional if defined
 *   #ifndef NAME         - Conditional if not defined
 *   #if expr             - Conditional expression
 *   #elif expr           - Else-if conditional
 *   #else                - Else branch
 *   #endif               - End conditional
 *   #pragma              - Compiler directive (ignored for now)
 *   #error "msg"         - Emit error
 *   #warning "msg"       - Emit warning
 */

#ifndef FCX_PREPROCESSOR_H
#define FCX_PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct Stmt;

// Maximum limits
#define PP_MAX_INCLUDE_DEPTH 64
#define PP_MAX_MACRO_PARAMS 32
#define PP_MAX_MACRO_LENGTH 4096
#define PP_MAX_CONDITION_DEPTH 64
#define PP_MAX_INCLUDE_PATHS 16
#define PP_MAX_MACROS 1024

// Macro types
typedef enum {
    MACRO_OBJECT,      // #define NAME value
    MACRO_FUNCTION,    // #define NAME(x, y) expr
    MACRO_BUILTIN      // __FILE__, __LINE__, etc.
} MacroType;

// Macro parameter
typedef struct {
    char *name;
    size_t index;
} MacroParam;

// Macro definition
typedef struct Macro {
    char *name;
    MacroType type;
    char *body;                          // Replacement text
    MacroParam *params;                  // For function-like macros
    size_t param_count;
    bool is_variadic;                    // Has ... parameter
    const char *defined_file;            // Where it was defined
    size_t defined_line;
    struct Macro *next;                  // Hash chain
} Macro;

// Conditional state
typedef enum {
    COND_IF,
    COND_ELIF,
    COND_ELSE
} ConditionType;

typedef struct {
    ConditionType type;
    bool condition_met;      // Was any branch taken?
    bool currently_active;   // Is current branch active?
    size_t line;
} ConditionState;

// Include file tracking (for #pragma once and cycle detection)
typedef struct IncludedFile {
    char *path;                    // Canonical path
    bool pragma_once;              // Has #pragma once
    struct IncludedFile *next;
} IncludedFile;

// Source location for error reporting
typedef struct {
    const char *filename;
    size_t line;
    size_t column;
} SourceLocation;

// Preprocessor context
typedef struct Preprocessor {
    // Include paths
    char *include_paths[PP_MAX_INCLUDE_PATHS];
    size_t include_path_count;
    
    // Macro table (hash table)
    Macro *macros[PP_MAX_MACROS];
    size_t macro_count;
    
    // Conditional stack
    ConditionState condition_stack[PP_MAX_CONDITION_DEPTH];
    size_t condition_depth;
    
    // Include stack (for nested includes)
    struct {
        const char *filename;
        const char *source;
        const char *current;
        size_t line;
    } include_stack[PP_MAX_INCLUDE_DEPTH];
    size_t include_depth;
    
    // Included files tracking
    IncludedFile *included_files;
    
    // Current state
    const char *current_file;
    const char *source;
    const char *current;
    size_t line;
    
    // Output buffer
    char *output;
    size_t output_size;
    size_t output_capacity;
    
    // Error handling
    bool had_error;
    char error_message[512];
    SourceLocation error_location;
    
    // Options
    bool keep_comments;
    bool emit_line_markers;    // #line directives in output
} Preprocessor;

// ============================================================================
// Preprocessor lifecycle
// ============================================================================

/**
 * Create a new preprocessor instance
 * @param std_path Path to standard library (can be NULL for default)
 * @return New preprocessor or NULL on failure
 */
Preprocessor *preprocessor_create(const char *std_path);

/**
 * Destroy preprocessor and free resources
 */
void preprocessor_destroy(Preprocessor *pp);

/**
 * Reset preprocessor state (keep macros and include paths)
 */
void preprocessor_reset(Preprocessor *pp);

// ============================================================================
// Include path management
// ============================================================================

/**
 * Add an include search path
 * @return true on success
 */
bool preprocessor_add_include_path(Preprocessor *pp, const char *path);

/**
 * Set the standard library path
 */
void preprocessor_set_std_path(Preprocessor *pp, const char *path);

// ============================================================================
// Macro management
// ============================================================================

/**
 * Define an object-like macro: #define NAME value
 */
bool preprocessor_define(Preprocessor *pp, const char *name, const char *value);

/**
 * Define a function-like macro: #define NAME(params) body
 */
bool preprocessor_define_function(Preprocessor *pp, const char *name,
                                   const char **params, size_t param_count,
                                   const char *body);

/**
 * Undefine a macro
 */
bool preprocessor_undef(Preprocessor *pp, const char *name);

/**
 * Check if a macro is defined
 */
bool preprocessor_is_defined(Preprocessor *pp, const char *name);

/**
 * Get macro definition (NULL if not defined)
 */
const Macro *preprocessor_get_macro(Preprocessor *pp, const char *name);

// ============================================================================
// Main preprocessing functions
// ============================================================================

/**
 * Preprocess a source string
 * @param pp Preprocessor instance
 * @param source Source code to preprocess
 * @param filename Filename for error reporting
 * @return Preprocessed source or NULL on error
 */
char *preprocessor_process(Preprocessor *pp, const char *source, 
                           const char *filename);

/**
 * Preprocess a file
 * @param pp Preprocessor instance
 * @param filename File to preprocess
 * @return Preprocessed source or NULL on error
 */
char *preprocessor_process_file_to_string(Preprocessor *pp, const char *filename);

/**
 * Process file and parse into statements (legacy interface)
 * @param pp Preprocessor instance
 * @param filename File to process
 * @param statements Output array of statements
 * @param stmt_count Output statement count
 * @return true on success
 */
bool preprocessor_process_file(Preprocessor *pp, const char *filename,
                               struct Stmt ***statements, size_t *stmt_count);

// ============================================================================
// Error handling
// ============================================================================

/**
 * Get last error message
 */
const char *preprocessor_get_error(Preprocessor *pp);

/**
 * Get error location
 */
SourceLocation preprocessor_get_error_location(Preprocessor *pp);

/**
 * Check if preprocessor had an error
 */
bool preprocessor_had_error(Preprocessor *pp);

// ============================================================================
// Utility functions
// ============================================================================

/**
 * Resolve include path (find file in include paths)
 * @param pp Preprocessor instance
 * @param include_name Name from #include directive
 * @param is_system true for <file.h>, false for "file.h"
 * @param current_file Current file (for relative includes)
 * @return Resolved path or NULL if not found (caller must free)
 */
char *preprocessor_resolve_include(Preprocessor *pp, const char *include_name,
                                    bool is_system, const char *current_file);

/**
 * Read file contents
 * @return File contents or NULL on error (caller must free)
 */
char *preprocessor_read_file(const char *filename);

#endif // FCX_PREPROCESSOR_H
