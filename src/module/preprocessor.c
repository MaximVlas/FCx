/**
 * FCx Preprocessor - C-style preprocessing implementation
 */

#include "preprocessor.h"
#include "c_import_zig.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Global C import context (shared across preprocessing)
static CImportContext *g_c_import_ctx = NULL;
static CImportContext *g_cpp_import_ctx = NULL;

// ============================================================================
// Internal helpers
// ============================================================================

// Hash function for macro names
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % PP_MAX_MACROS;
}

// Set error message
static void pp_error(Preprocessor *pp, const char *fmt, ...) {
    pp->had_error = true;
    pp->error_location.filename = pp->current_file;
    pp->error_location.line = pp->line;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(pp->error_message, sizeof(pp->error_message), fmt, args);
    va_end(args);
}

// Append to output buffer
static bool pp_output(Preprocessor *pp, const char *str, size_t len) {
    if (len == 0) len = strlen(str);
    
    // Grow buffer if needed
    while (pp->output_size + len + 1 > pp->output_capacity) {
        size_t new_cap = pp->output_capacity * 2;
        if (new_cap == 0) new_cap = 4096;
        char *new_buf = realloc(pp->output, new_cap);
        if (!new_buf) {
            pp_error(pp, "Out of memory");
            return false;
        }
        pp->output = new_buf;
        pp->output_capacity = new_cap;
    }
    
    memcpy(pp->output + pp->output_size, str, len);
    pp->output_size += len;
    pp->output[pp->output_size] = '\0';
    return true;
}

static bool pp_output_char(Preprocessor *pp, char c) {
    return pp_output(pp, &c, 1);
}

// Check if currently in an active conditional branch
static bool pp_is_active(Preprocessor *pp) {
    if (pp->condition_depth == 0) return true;
    return pp->condition_stack[pp->condition_depth - 1].currently_active;
}

// Skip whitespace (not newlines)
static void pp_skip_hspace(Preprocessor *pp) {
    while (*pp->current == ' ' || *pp->current == '\t') {
        pp->current++;
    }
}

// Skip to end of line
static void pp_skip_to_eol(Preprocessor *pp) {
    while (*pp->current && *pp->current != '\n') {
        pp->current++;
    }
}

// Read identifier
static char *pp_read_identifier(Preprocessor *pp) {
    const char *start = pp->current;
    
    if (!isalpha(*pp->current) && *pp->current != '_') {
        return NULL;
    }
    
    while (isalnum(*pp->current) || *pp->current == '_') {
        pp->current++;
    }
    
    size_t len = pp->current - start;
    char *id = malloc(len + 1);
    if (id) {
        memcpy(id, start, len);
        id[len] = '\0';
    }
    return id;
}

// Read string literal (for #include "file" or #error "msg")
static char *pp_read_string(Preprocessor *pp, char delimiter) {
    if (*pp->current != delimiter) return NULL;
    pp->current++; // skip opening delimiter
    
    const char *start = pp->current;
    while (*pp->current && *pp->current != delimiter && *pp->current != '\n') {
        if (*pp->current == '\\' && pp->current[1]) {
            pp->current += 2; // skip escape sequence
        } else {
            pp->current++;
        }
    }
    
    size_t len = pp->current - start;
    char *str = malloc(len + 1);
    if (str) {
        memcpy(str, start, len);
        str[len] = '\0';
    }
    
    if (*pp->current == delimiter) {
        pp->current++; // skip closing delimiter
    }
    
    return str;
}

// Read rest of line (for macro body)
static char *pp_read_line(Preprocessor *pp) {
    const char *start = pp->current;
    
    // Handle line continuation with backslash
    while (*pp->current) {
        if (*pp->current == '\n') {
            // Check for line continuation
            if (pp->current > start && pp->current[-1] == '\\') {
                pp->current++;
                pp->line++;
                continue;
            }
            break;
        }
        pp->current++;
    }
    
    // Trim trailing whitespace and backslash
    const char *end = pp->current;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\\')) {
        end--;
    }
    
    size_t len = end - start;
    char *line = malloc(len + 1);
    if (line) {
        memcpy(line, start, len);
        line[len] = '\0';
    }
    return line;
}

// ============================================================================
// Macro management
// ============================================================================

static Macro *macro_create(const char *name, MacroType type) {
    Macro *m = calloc(1, sizeof(Macro));
    if (m) {
        m->name = strdup(name);
        m->type = type;
    }
    return m;
}

static void macro_destroy(Macro *m) {
    if (!m) return;
    free(m->name);
    free(m->body);
    for (size_t i = 0; i < m->param_count; i++) {
        free(m->params[i].name);
    }
    free(m->params);
    free(m);
}

bool preprocessor_define(Preprocessor *pp, const char *name, const char *value) {
    uint32_t hash = hash_string(name);
    
    // Check if already defined
    Macro *m = pp->macros[hash];
    while (m) {
        if (strcmp(m->name, name) == 0) {
            // Redefine
            free(m->body);
            m->body = value ? strdup(value) : strdup("");
            return true;
        }
        m = m->next;
    }
    
    // Create new macro
    m = macro_create(name, MACRO_OBJECT);
    if (!m) {
        pp_error(pp, "Out of memory defining macro '%s'", name);
        return false;
    }
    m->body = value ? strdup(value) : strdup("");
    m->defined_file = pp->current_file;
    m->defined_line = pp->line;
    
    // Insert into hash table
    m->next = pp->macros[hash];
    pp->macros[hash] = m;
    pp->macro_count++;
    
    return true;
}

bool preprocessor_define_function(Preprocessor *pp, const char *name,
                                   const char **params, size_t param_count,
                                   const char *body) {
    uint32_t hash = hash_string(name);
    
    Macro *m = macro_create(name, MACRO_FUNCTION);
    if (!m) {
        pp_error(pp, "Out of memory defining macro '%s'", name);
        return false;
    }
    
    m->body = strdup(body);
    m->param_count = param_count;
    if (param_count > 0) {
        m->params = calloc(param_count, sizeof(MacroParam));
        for (size_t i = 0; i < param_count; i++) {
            m->params[i].name = strdup(params[i]);
            m->params[i].index = i;
        }
    }
    m->defined_file = pp->current_file;
    m->defined_line = pp->line;
    
    // Insert into hash table
    m->next = pp->macros[hash];
    pp->macros[hash] = m;
    pp->macro_count++;
    
    return true;
}

bool preprocessor_undef(Preprocessor *pp, const char *name) {
    uint32_t hash = hash_string(name);
    
    Macro **prev = &pp->macros[hash];
    Macro *m = *prev;
    
    while (m) {
        if (strcmp(m->name, name) == 0) {
            *prev = m->next;
            macro_destroy(m);
            pp->macro_count--;
            return true;
        }
        prev = &m->next;
        m = m->next;
    }
    
    return false; // Not found (not an error)
}

bool preprocessor_is_defined(Preprocessor *pp, const char *name) {
    return preprocessor_get_macro(pp, name) != NULL;
}

const Macro *preprocessor_get_macro(Preprocessor *pp, const char *name) {
    uint32_t hash = hash_string(name);
    Macro *m = pp->macros[hash];
    
    while (m) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
        m = m->next;
    }
    
    return NULL;
}

// ============================================================================
// Include path resolution
// ============================================================================

char *preprocessor_read_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);
    
    return content;
}

static char *get_directory(const char *filepath) {
    char *path = strdup(filepath);
    if (!path) return NULL;
    
    char *dir = dirname(path);
    char *result = strdup(dir);
    free(path);
    return result;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char *preprocessor_resolve_include(Preprocessor *pp, const char *include_name,
                                    bool is_system, const char *current_file) {
    char path_buf[PATH_MAX];
    
    // For quoted includes, first try relative to current file
    if (!is_system && current_file) {
        char *dir = get_directory(current_file);
        if (dir) {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", dir, include_name);
            free(dir);
            if (file_exists(path_buf)) {
                char *resolved = realpath(path_buf, NULL);
                return resolved ? resolved : strdup(path_buf);
            }
        }
    }
    
    // Search include paths
    for (size_t i = 0; i < pp->include_path_count; i++) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", 
                 pp->include_paths[i], include_name);
        if (file_exists(path_buf)) {
            char *resolved = realpath(path_buf, NULL);
            return resolved ? resolved : strdup(path_buf);
        }
    }
    
    // Try current directory
    if (file_exists(include_name)) {
        char *resolved = realpath(include_name, NULL);
        return resolved ? resolved : strdup(include_name);
    }
    
    return NULL;
}

bool preprocessor_add_include_path(Preprocessor *pp, const char *path) {
    if (pp->include_path_count >= PP_MAX_INCLUDE_PATHS) {
        pp_error(pp, "Too many include paths");
        return false;
    }
    
    pp->include_paths[pp->include_path_count++] = strdup(path);
    return true;
}

void preprocessor_set_std_path(Preprocessor *pp, const char *path) {
    // Add std path as first include path
    if (pp->include_path_count > 0) {
        // Shift existing paths
        for (size_t i = pp->include_path_count; i > 0; i--) {
            pp->include_paths[i] = pp->include_paths[i-1];
        }
    }
    pp->include_paths[0] = strdup(path);
    pp->include_path_count++;
}

// ============================================================================
// Directive handlers
// ============================================================================

static bool pp_handle_include(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    bool is_system = false;
    char *filename = NULL;
    
    if (*pp->current == '"') {
        filename = pp_read_string(pp, '"');
        is_system = false;
    } else if (*pp->current == '<') {
        pp->current++; // skip <
        const char *start = pp->current;
        while (*pp->current && *pp->current != '>' && *pp->current != '\n') {
            pp->current++;
        }
        size_t len = pp->current - start;
        filename = malloc(len + 1);
        if (filename) {
            memcpy(filename, start, len);
            filename[len] = '\0';
        }
        if (*pp->current == '>') pp->current++;
        is_system = true;
    } else {
        pp_error(pp, "Expected filename after #include");
        return false;
    }
    
    if (!filename) {
        pp_error(pp, "Invalid #include filename");
        return false;
    }
    
    // Resolve the include path
    char *resolved = preprocessor_resolve_include(pp, filename, is_system, 
                                                   pp->current_file);
    
    if (!resolved) {
        pp_error(pp, "Cannot find include file '%s'", filename);
        free(filename);
        return false;
    }
    
    free(filename);
    
    // Check for include cycle
    for (size_t i = 0; i < pp->include_depth; i++) {
        if (strcmp(pp->include_stack[i].filename, resolved) == 0) {
            pp_error(pp, "Circular include detected: '%s'", resolved);
            free(resolved);
            return false;
        }
    }
    
    // Check include depth
    if (pp->include_depth >= PP_MAX_INCLUDE_DEPTH) {
        pp_error(pp, "Include depth exceeded (max %d)", PP_MAX_INCLUDE_DEPTH);
        free(resolved);
        return false;
    }
    
    // Check for #pragma once
    IncludedFile *inc = pp->included_files;
    while (inc) {
        if (strcmp(inc->path, resolved) == 0 && inc->pragma_once) {
            free(resolved);
            return true; // Skip this include
        }
        inc = inc->next;
    }
    
    // Read the file
    char *content = preprocessor_read_file(resolved);
    if (!content) {
        pp_error(pp, "Cannot read include file '%s': %s", resolved, strerror(errno));
        free(resolved);
        return false;
    }
    
    // Push current state
    pp->include_stack[pp->include_depth].filename = pp->current_file;
    pp->include_stack[pp->include_depth].source = pp->source;
    pp->include_stack[pp->include_depth].current = pp->current;
    pp->include_stack[pp->include_depth].line = pp->line;
    pp->include_depth++;
    
    // Track included file
    inc = calloc(1, sizeof(IncludedFile));
    inc->path = strdup(resolved);
    inc->next = pp->included_files;
    pp->included_files = inc;
    
    // Emit line marker if enabled
    if (pp->emit_line_markers) {
        char marker[256];
        snprintf(marker, sizeof(marker), "\n#line 1 \"%s\"\n", resolved);
        pp_output(pp, marker, 0);
    }
    
    // Switch to new file
    pp->current_file = resolved;
    pp->source = content;
    pp->current = content;
    pp->line = 1;
    
    return true;
}

static bool pp_handle_define(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    char *name = pp_read_identifier(pp);
    if (!name) {
        pp_error(pp, "Expected identifier after #define");
        return false;
    }
    
    // Check for function-like macro
    if (*pp->current == '(') {
        pp->current++; // skip (
        
        const char *params[PP_MAX_MACRO_PARAMS];
        size_t param_count = 0;
        
        pp_skip_hspace(pp);
        
        // Parse parameters
        while (*pp->current && *pp->current != ')') {
            pp_skip_hspace(pp);
            
            if (*pp->current == '.') {
                // Variadic: ...
                if (pp->current[1] == '.' && pp->current[2] == '.') {
                    pp->current += 3;
                    // TODO: handle variadic
                    break;
                }
            }
            
            char *param = pp_read_identifier(pp);
            if (!param) {
                pp_error(pp, "Expected parameter name in macro definition");
                free(name);
                return false;
            }
            
            if (param_count >= PP_MAX_MACRO_PARAMS) {
                pp_error(pp, "Too many macro parameters");
                free(param);
                free(name);
                return false;
            }
            
            params[param_count++] = param;
            
            pp_skip_hspace(pp);
            if (*pp->current == ',') {
                pp->current++;
            }
        }
        
        if (*pp->current != ')') {
            pp_error(pp, "Expected ')' in macro definition");
            free(name);
            return false;
        }
        pp->current++; // skip )
        
        pp_skip_hspace(pp);
        char *body = pp_read_line(pp);
        
        bool result = preprocessor_define_function(pp, name, params, param_count, 
                                                    body ? body : "");
        
        // Free params
        for (size_t i = 0; i < param_count; i++) {
            free((char*)params[i]);
        }
        free(body);
        free(name);
        return result;
    }
    
    // Object-like macro
    pp_skip_hspace(pp);
    char *value = pp_read_line(pp);
    
    bool result = preprocessor_define(pp, name, value ? value : "");
    free(value);
    free(name);
    return result;
}

static bool pp_handle_undef(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    char *name = pp_read_identifier(pp);
    if (!name) {
        pp_error(pp, "Expected identifier after #undef");
        return false;
    }
    
    preprocessor_undef(pp, name);
    free(name);
    return true;
}

// Simple expression evaluator for #if
static int64_t pp_eval_expr(Preprocessor *pp);

static int64_t pp_eval_primary(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    // defined(NAME) or defined NAME
    if (strncmp(pp->current, "defined", 7) == 0 && 
        !isalnum(pp->current[7]) && pp->current[7] != '_') {
        pp->current += 7;
        pp_skip_hspace(pp);
        
        bool has_paren = (*pp->current == '(');
        if (has_paren) pp->current++;
        pp_skip_hspace(pp);
        
        char *name = pp_read_identifier(pp);
        if (!name) {
            pp_error(pp, "Expected identifier after 'defined'");
            return 0;
        }
        
        int64_t result = preprocessor_is_defined(pp, name) ? 1 : 0;
        free(name);
        
        if (has_paren) {
            pp_skip_hspace(pp);
            if (*pp->current == ')') pp->current++;
        }
        
        return result;
    }
    
    // Parenthesized expression
    if (*pp->current == '(') {
        pp->current++;
        int64_t result = pp_eval_expr(pp);
        pp_skip_hspace(pp);
        if (*pp->current == ')') pp->current++;
        return result;
    }
    
    // Number
    if (isdigit(*pp->current)) {
        char *end;
        int64_t val = strtoll(pp->current, &end, 0);
        pp->current = end;
        return val;
    }
    
    // Character literal
    if (*pp->current == '\'') {
        pp->current++;
        int64_t val = *pp->current++;
        if (*pp->current == '\'') pp->current++;
        return val;
    }
    
    // Identifier (undefined = 0)
    if (isalpha(*pp->current) || *pp->current == '_') {
        char *name = pp_read_identifier(pp);
        if (name) {
            const Macro *m = preprocessor_get_macro(pp, name);
            free(name);
            if (m && m->body) {
                // Try to parse macro value as number
                char *end;
                int64_t val = strtoll(m->body, &end, 0);
                if (*end == '\0' || isspace(*end)) {
                    return val;
                }
            }
        }
        return 0; // Undefined identifiers are 0
    }
    
    return 0;
}

static int64_t pp_eval_unary(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    if (*pp->current == '!') {
        pp->current++;
        return !pp_eval_unary(pp);
    }
    if (*pp->current == '~') {
        pp->current++;
        return ~pp_eval_unary(pp);
    }
    if (*pp->current == '-') {
        pp->current++;
        return -pp_eval_unary(pp);
    }
    if (*pp->current == '+') {
        pp->current++;
        return pp_eval_unary(pp);
    }
    
    return pp_eval_primary(pp);
}

static int64_t pp_eval_multiplicative(Preprocessor *pp) {
    int64_t left = pp_eval_unary(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (*pp->current == '*') {
            pp->current++;
            left *= pp_eval_unary(pp);
        } else if (*pp->current == '/') {
            pp->current++;
            int64_t right = pp_eval_unary(pp);
            left = right ? left / right : 0;
        } else if (*pp->current == '%') {
            pp->current++;
            int64_t right = pp_eval_unary(pp);
            left = right ? left % right : 0;
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_additive(Preprocessor *pp) {
    int64_t left = pp_eval_multiplicative(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (*pp->current == '+') {
            pp->current++;
            left += pp_eval_multiplicative(pp);
        } else if (*pp->current == '-') {
            pp->current++;
            left -= pp_eval_multiplicative(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_shift(Preprocessor *pp) {
    int64_t left = pp_eval_additive(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (pp->current[0] == '<' && pp->current[1] == '<') {
            pp->current += 2;
            left <<= pp_eval_additive(pp);
        } else if (pp->current[0] == '>' && pp->current[1] == '>') {
            pp->current += 2;
            left >>= pp_eval_additive(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_relational(Preprocessor *pp) {
    int64_t left = pp_eval_shift(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (pp->current[0] == '<' && pp->current[1] == '=') {
            pp->current += 2;
            left = left <= pp_eval_shift(pp);
        } else if (pp->current[0] == '>' && pp->current[1] == '=') {
            pp->current += 2;
            left = left >= pp_eval_shift(pp);
        } else if (*pp->current == '<') {
            pp->current++;
            left = left < pp_eval_shift(pp);
        } else if (*pp->current == '>') {
            pp->current++;
            left = left > pp_eval_shift(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_equality(Preprocessor *pp) {
    int64_t left = pp_eval_relational(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (pp->current[0] == '=' && pp->current[1] == '=') {
            pp->current += 2;
            left = left == pp_eval_relational(pp);
        } else if (pp->current[0] == '!' && pp->current[1] == '=') {
            pp->current += 2;
            left = left != pp_eval_relational(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_bitand(Preprocessor *pp) {
    int64_t left = pp_eval_equality(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (*pp->current == '&' && pp->current[1] != '&') {
            pp->current++;
            left &= pp_eval_equality(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_bitxor(Preprocessor *pp) {
    int64_t left = pp_eval_bitand(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (*pp->current == '^') {
            pp->current++;
            left ^= pp_eval_bitand(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_bitor(Preprocessor *pp) {
    int64_t left = pp_eval_bitxor(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (*pp->current == '|' && pp->current[1] != '|') {
            pp->current++;
            left |= pp_eval_bitxor(pp);
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_logand(Preprocessor *pp) {
    int64_t left = pp_eval_bitor(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (pp->current[0] == '&' && pp->current[1] == '&') {
            pp->current += 2;
            int64_t right = pp_eval_bitor(pp);
            left = left && right;
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_logor(Preprocessor *pp) {
    int64_t left = pp_eval_logand(pp);
    
    for (;;) {
        pp_skip_hspace(pp);
        if (pp->current[0] == '|' && pp->current[1] == '|') {
            pp->current += 2;
            int64_t right = pp_eval_logand(pp);
            left = left || right;
        } else {
            break;
        }
    }
    return left;
}

static int64_t pp_eval_ternary(Preprocessor *pp) {
    int64_t cond = pp_eval_logor(pp);
    
    pp_skip_hspace(pp);
    if (*pp->current == '?') {
        pp->current++;
        int64_t then_val = pp_eval_expr(pp);
        pp_skip_hspace(pp);
        if (*pp->current == ':') pp->current++;
        int64_t else_val = pp_eval_ternary(pp);
        return cond ? then_val : else_val;
    }
    
    return cond;
}

static int64_t pp_eval_expr(Preprocessor *pp) {
    return pp_eval_ternary(pp);
}

// Conditional directive handlers
static bool pp_handle_ifdef(Preprocessor *pp, bool is_ifndef) {
    pp_skip_hspace(pp);
    
    char *name = pp_read_identifier(pp);
    if (!name) {
        pp_error(pp, "Expected identifier after #ifdef/#ifndef");
        return false;
    }
    
    bool defined = preprocessor_is_defined(pp, name);
    bool condition = is_ifndef ? !defined : defined;
    free(name);
    
    if (pp->condition_depth >= PP_MAX_CONDITION_DEPTH) {
        pp_error(pp, "Conditional nesting too deep");
        return false;
    }
    
    // If parent is inactive, this is also inactive
    bool parent_active = pp_is_active(pp);
    
    ConditionState *state = &pp->condition_stack[pp->condition_depth++];
    state->type = COND_IF;
    state->condition_met = condition && parent_active;
    state->currently_active = condition && parent_active;
    state->line = pp->line;
    
    return true;
}

static bool pp_handle_if(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    int64_t result = pp_eval_expr(pp);
    bool condition = result != 0;
    
    if (pp->condition_depth >= PP_MAX_CONDITION_DEPTH) {
        pp_error(pp, "Conditional nesting too deep");
        return false;
    }
    
    bool parent_active = pp_is_active(pp);
    
    ConditionState *state = &pp->condition_stack[pp->condition_depth++];
    state->type = COND_IF;
    state->condition_met = condition && parent_active;
    state->currently_active = condition && parent_active;
    state->line = pp->line;
    
    return true;
}

static bool pp_handle_elif(Preprocessor *pp) {
    if (pp->condition_depth == 0) {
        pp_error(pp, "#elif without #if");
        return false;
    }
    
    ConditionState *state = &pp->condition_stack[pp->condition_depth - 1];
    
    if (state->type == COND_ELSE) {
        pp_error(pp, "#elif after #else");
        return false;
    }
    
    pp_skip_hspace(pp);
    int64_t result = pp_eval_expr(pp);
    bool condition = result != 0;
    
    // Check if parent is active
    bool parent_active = true;
    if (pp->condition_depth > 1) {
        parent_active = pp->condition_stack[pp->condition_depth - 2].currently_active;
    }
    
    state->type = COND_ELIF;
    // Only active if no previous branch was taken and condition is true
    state->currently_active = !state->condition_met && condition && parent_active;
    if (state->currently_active) {
        state->condition_met = true;
    }
    
    return true;
}

static bool pp_handle_else(Preprocessor *pp) {
    if (pp->condition_depth == 0) {
        pp_error(pp, "#else without #if");
        return false;
    }
    
    ConditionState *state = &pp->condition_stack[pp->condition_depth - 1];
    
    if (state->type == COND_ELSE) {
        pp_error(pp, "Duplicate #else");
        return false;
    }
    
    // Check if parent is active
    bool parent_active = true;
    if (pp->condition_depth > 1) {
        parent_active = pp->condition_stack[pp->condition_depth - 2].currently_active;
    }
    
    state->type = COND_ELSE;
    // Only active if no previous branch was taken
    state->currently_active = !state->condition_met && parent_active;
    
    return true;
}

static bool pp_handle_endif(Preprocessor *pp) {
    if (pp->condition_depth == 0) {
        pp_error(pp, "#endif without #if");
        return false;
    }
    
    pp->condition_depth--;
    return true;
}

static bool pp_handle_error(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    char *msg = NULL;
    if (*pp->current == '"') {
        msg = pp_read_string(pp, '"');
    } else {
        msg = pp_read_line(pp);
    }
    
    pp_error(pp, "#error: %s", msg ? msg : "(no message)");
    free(msg);
    return false;
}

static bool pp_handle_warning(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    char *msg = NULL;
    if (*pp->current == '"') {
        msg = pp_read_string(pp, '"');
    } else {
        msg = pp_read_line(pp);
    }
    
    fprintf(stderr, "%s:%zu: warning: %s\n", 
            pp->current_file ? pp->current_file : "<input>",
            pp->line, msg ? msg : "(no message)");
    free(msg);
    return true;
}

static bool pp_handle_pragma(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    // Handle #pragma once
    if (strncmp(pp->current, "once", 4) == 0 && 
        !isalnum(pp->current[4]) && pp->current[4] != '_') {
        // Mark current file as pragma once
        IncludedFile *inc = pp->included_files;
        while (inc) {
            if (pp->current_file && strcmp(inc->path, pp->current_file) == 0) {
                inc->pragma_once = true;
                break;
            }
            inc = inc->next;
        }
        pp->current += 4;
        return true;
    }
    
    // Ignore other pragmas
    pp_skip_to_eol(pp);
    return true;
}

static bool pp_handle_line(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    // Parse line number
    if (!isdigit(*pp->current)) {
        pp_error(pp, "Expected line number after #line");
        return false;
    }
    
    char *end;
    long line = strtol(pp->current, &end, 10);
    pp->current = end;
    pp->line = line;
    
    pp_skip_hspace(pp);
    
    // Optional filename
    if (*pp->current == '"') {
        char *filename = pp_read_string(pp, '"');
        if (filename) {
            // Note: we don't actually change current_file here
            // as that would complicate memory management
            free(filename);
        }
    }
    
    return true;
}

// ============================================================================
// C/C++ Import handlers
// ============================================================================

static bool pp_handle_importc(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    bool is_system = false;
    char *header = NULL;
    
    if (*pp->current == '"') {
        header = pp_read_string(pp, '"');
        is_system = false;
    } else if (*pp->current == '<') {
        pp->current++; // skip <
        const char *start = pp->current;
        while (*pp->current && *pp->current != '>' && *pp->current != '\n') {
            pp->current++;
        }
        size_t len = pp->current - start;
        header = malloc(len + 1);
        if (header) {
            memcpy(header, start, len);
            header[len] = '\0';
        }
        if (*pp->current == '>') pp->current++;
        is_system = true;
    } else {
        pp_error(pp, "Expected header name after #importc");
        return false;
    }
    
    if (!header) {
        pp_error(pp, "Invalid #importc header");
        return false;
    }
    
    // Initialize C import context if needed
    if (!g_c_import_ctx) {
        g_c_import_ctx = fcx_c_import_create();
        if (!g_c_import_ctx) {
            pp_error(pp, "Failed to create C import context");
            free(header);
            return false;
        }
        // Add standard include paths
        fcx_c_import_add_include_path(g_c_import_ctx, "/usr/include");
        fcx_c_import_add_include_path(g_c_import_ctx, "/usr/local/include");
    }
    
    // Queue the header for import
    if (!fcx_c_import_header(g_c_import_ctx, header, is_system)) {
        pp_error(pp, "Failed to queue C header '%s' for import", header);
        free(header);
        return false;
    }
    
    // Emit a comment marker so we know C imports were used
    char marker[256];
    snprintf(marker, sizeof(marker), "// [C IMPORT: %s]\n", header);
    pp_output(pp, marker, 0);
    
    free(header);
    return true;
}

static bool pp_handle_importcpp(Preprocessor *pp) {
    pp_skip_hspace(pp);
    
    bool is_system = false;
    char *header = NULL;
    
    if (*pp->current == '"') {
        header = pp_read_string(pp, '"');
        is_system = false;
    } else if (*pp->current == '<') {
        pp->current++; // skip <
        const char *start = pp->current;
        while (*pp->current && *pp->current != '>' && *pp->current != '\n') {
            pp->current++;
        }
        size_t len = pp->current - start;
        header = malloc(len + 1);
        if (header) {
            memcpy(header, start, len);
            header[len] = '\0';
        }
        if (*pp->current == '>') pp->current++;
        is_system = true;
    } else {
        pp_error(pp, "Expected header name after #importcpp");
        return false;
    }
    
    if (!header) {
        pp_error(pp, "Invalid #importcpp header");
        return false;
    }
    
    // Initialize C++ import context if needed
    if (!g_cpp_import_ctx) {
        g_cpp_import_ctx = fcx_c_import_create();
        if (!g_cpp_import_ctx) {
            pp_error(pp, "Failed to create C++ import context");
            free(header);
            return false;
        }
        // Add standard C++ include paths
        fcx_c_import_add_include_path(g_cpp_import_ctx, "/usr/include");
        fcx_c_import_add_include_path(g_cpp_import_ctx, "/usr/include/c++/14");
        fcx_c_import_add_include_path(g_cpp_import_ctx, "/usr/include/c++/13");
        fcx_c_import_add_include_path(g_cpp_import_ctx, "/usr/include/c++/12");
        fcx_c_import_add_include_path(g_cpp_import_ctx, "/usr/local/include");
    }
    
    // Queue the header for import
    if (!fcx_c_import_header(g_cpp_import_ctx, header, is_system)) {
        pp_error(pp, "Failed to queue C++ header '%s' for import", header);
        free(header);
        return false;
    }
    
    // Emit a comment marker so we know C++ imports were used
    char marker[256];
    snprintf(marker, sizeof(marker), "// [C++ IMPORT: %s]\n", header);
    pp_output(pp, marker, 0);
    
    free(header);
    return true;
}

// Get the C import context (for code generation)
CImportContext *preprocessor_get_c_import_context(void) {
    return g_c_import_ctx;
}

// Get the C++ import context (for code generation)
CImportContext *preprocessor_get_cpp_import_context(void) {
    return g_cpp_import_ctx;
}

// Process all pending C/C++ imports
bool preprocessor_process_c_imports(void) {
    bool success = true;
    
    if (g_c_import_ctx) {
        if (!fcx_c_import_process(g_c_import_ctx)) {
            fprintf(stderr, "Warning: Failed to process C imports: %s\n",
                    fcx_c_import_get_error(g_c_import_ctx));
            success = false;
        }
    }
    
    if (g_cpp_import_ctx) {
        if (!fcx_c_import_process(g_cpp_import_ctx)) {
            fprintf(stderr, "Warning: Failed to process C++ imports: %s\n",
                    fcx_c_import_get_error(g_cpp_import_ctx));
            success = false;
        }
    }
    
    return success;
}

// Cleanup C/C++ import contexts
void preprocessor_cleanup_c_imports(void) {
    if (g_c_import_ctx) {
        fcx_c_import_destroy(g_c_import_ctx);
        g_c_import_ctx = NULL;
    }
    if (g_cpp_import_ctx) {
        fcx_c_import_destroy(g_cpp_import_ctx);
        g_cpp_import_ctx = NULL;
    }
}

// ============================================================================
// Macro expansion
// ============================================================================

static char *pp_expand_macros(Preprocessor *pp, const char *text);

static char *pp_expand_function_macro(Preprocessor *pp, const Macro *m, 
                                       const char **args, size_t arg_count) {
    if (arg_count != m->param_count) {
        pp_error(pp, "Macro '%s' expects %zu arguments, got %zu",
                 m->name, m->param_count, arg_count);
        return NULL;
    }
    
    // Simple substitution
    size_t body_len = strlen(m->body);
    size_t result_cap = body_len * 2 + 256;
    char *result = malloc(result_cap);
    if (!result) return NULL;
    
    size_t result_len = 0;
    const char *p = m->body;
    
    while (*p) {
        // Check for parameter reference
        if (isalpha(*p) || *p == '_') {
            const char *start = p;
            while (isalnum(*p) || *p == '_') p++;
            size_t id_len = p - start;
            
            // Check if it's a parameter
            bool found = false;
            for (size_t i = 0; i < m->param_count; i++) {
                if (strlen(m->params[i].name) == id_len &&
                    strncmp(m->params[i].name, start, id_len) == 0) {
                    // Substitute argument
                    size_t arg_len = strlen(args[i]);
                    while (result_len + arg_len + 1 > result_cap) {
                        result_cap *= 2;
                        result = realloc(result, result_cap);
                    }
                    memcpy(result + result_len, args[i], arg_len);
                    result_len += arg_len;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                // Copy identifier as-is
                while (result_len + id_len + 1 > result_cap) {
                    result_cap *= 2;
                    result = realloc(result, result_cap);
                }
                memcpy(result + result_len, start, id_len);
                result_len += id_len;
            }
        } else {
            // Copy character
            if (result_len + 2 > result_cap) {
                result_cap *= 2;
                result = realloc(result, result_cap);
            }
            result[result_len++] = *p++;
        }
    }
    
    result[result_len] = '\0';
    
    // Recursively expand
    char *expanded = pp_expand_macros(pp, result);
    free(result);
    return expanded;
}

static char *pp_expand_macros(Preprocessor *pp, const char *text) {
    size_t text_len = strlen(text);
    size_t result_cap = text_len * 2 + 256;
    char *result = malloc(result_cap);
    if (!result) return NULL;
    
    size_t result_len = 0;
    const char *p = text;
    
    while (*p) {
        // Skip strings
        if (*p == '"' || *p == '\'') {
            char quote = *p;
            result[result_len++] = *p++;
            while (*p && *p != quote) {
                if (*p == '\\' && p[1]) {
                    result[result_len++] = *p++;
                }
                result[result_len++] = *p++;
            }
            if (*p) result[result_len++] = *p++;
            continue;
        }
        
        // Check for identifier (potential macro)
        if (isalpha(*p) || *p == '_') {
            const char *start = p;
            while (isalnum(*p) || *p == '_') p++;
            size_t id_len = p - start;
            
            char *name = malloc(id_len + 1);
            memcpy(name, start, id_len);
            name[id_len] = '\0';
            
            const Macro *m = preprocessor_get_macro(pp, name);
            
            if (m) {
                if (m->type == MACRO_FUNCTION) {
                    // Skip whitespace
                    while (*p == ' ' || *p == '\t') p++;
                    
                    if (*p == '(') {
                        p++; // skip (
                        
                        // Parse arguments
                        const char *args[PP_MAX_MACRO_PARAMS];
                        size_t arg_count = 0;
                        
                        while (*p && *p != ')') {
                            while (*p == ' ' || *p == '\t') p++;
                            
                            const char *arg_start = p;
                            int paren_depth = 0;
                            
                            while (*p && (paren_depth > 0 || (*p != ',' && *p != ')'))) {
                                if (*p == '(') paren_depth++;
                                else if (*p == ')') paren_depth--;
                                p++;
                            }
                            
                            size_t arg_len = p - arg_start;
                            // Trim trailing whitespace
                            while (arg_len > 0 && (arg_start[arg_len-1] == ' ' || 
                                   arg_start[arg_len-1] == '\t')) {
                                arg_len--;
                            }
                            
                            char *arg = malloc(arg_len + 1);
                            memcpy(arg, arg_start, arg_len);
                            arg[arg_len] = '\0';
                            args[arg_count++] = arg;
                            
                            if (*p == ',') p++;
                        }
                        
                        if (*p == ')') p++;
                        
                        char *expanded = pp_expand_function_macro(pp, m, args, arg_count);
                        
                        for (size_t i = 0; i < arg_count; i++) {
                            free((char*)args[i]);
                        }
                        
                        if (expanded) {
                            size_t exp_len = strlen(expanded);
                            while (result_len + exp_len + 1 > result_cap) {
                                result_cap *= 2;
                                result = realloc(result, result_cap);
                            }
                            memcpy(result + result_len, expanded, exp_len);
                            result_len += exp_len;
                            free(expanded);
                        }
                        
                        free(name);
                        continue;
                    }
                } else {
                    // Object-like macro
                    char *expanded = pp_expand_macros(pp, m->body);
                    if (expanded) {
                        size_t exp_len = strlen(expanded);
                        while (result_len + exp_len + 1 > result_cap) {
                            result_cap *= 2;
                            result = realloc(result, result_cap);
                        }
                        memcpy(result + result_len, expanded, exp_len);
                        result_len += exp_len;
                        free(expanded);
                    }
                    free(name);
                    continue;
                }
            }
            
            // Not a macro, copy as-is
            while (result_len + id_len + 1 > result_cap) {
                result_cap *= 2;
                result = realloc(result, result_cap);
            }
            memcpy(result + result_len, start, id_len);
            result_len += id_len;
            free(name);
            continue;
        }
        
        // Copy character
        if (result_len + 2 > result_cap) {
            result_cap *= 2;
            result = realloc(result, result_cap);
        }
        result[result_len++] = *p++;
    }
    
    result[result_len] = '\0';
    return result;
}

// ============================================================================
// Main preprocessing loop
// ============================================================================

static bool pp_process_directive(Preprocessor *pp) {
    pp->current++; // skip #
    pp_skip_hspace(pp);
    
    // Read directive name
    const char *dir_start = pp->current;
    while (isalpha(*pp->current)) {
        pp->current++;
    }
    size_t dir_len = pp->current - dir_start;
    
    // Empty directive (just #)
    if (dir_len == 0) {
        return true;
    }
    
    // Match directive
    if (dir_len == 7 && strncmp(dir_start, "include", 7) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_include(pp);
    }
    else if (dir_len == 6 && strncmp(dir_start, "define", 6) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_define(pp);
    }
    else if (dir_len == 5 && strncmp(dir_start, "undef", 5) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_undef(pp);
    }
    else if (dir_len == 5 && strncmp(dir_start, "ifdef", 5) == 0) {
        return pp_handle_ifdef(pp, false);
    }
    else if (dir_len == 6 && strncmp(dir_start, "ifndef", 6) == 0) {
        return pp_handle_ifdef(pp, true);
    }
    else if (dir_len == 2 && strncmp(dir_start, "if", 2) == 0) {
        return pp_handle_if(pp);
    }
    else if (dir_len == 4 && strncmp(dir_start, "elif", 4) == 0) {
        return pp_handle_elif(pp);
    }
    else if (dir_len == 4 && strncmp(dir_start, "else", 4) == 0) {
        return pp_handle_else(pp);
    }
    else if (dir_len == 5 && strncmp(dir_start, "endif", 5) == 0) {
        return pp_handle_endif(pp);
    }
    else if (dir_len == 5 && strncmp(dir_start, "error", 5) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_error(pp);
    }
    else if (dir_len == 7 && strncmp(dir_start, "warning", 7) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_warning(pp);
    }
    else if (dir_len == 6 && strncmp(dir_start, "pragma", 6) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_pragma(pp);
    }
    else if (dir_len == 4 && strncmp(dir_start, "line", 4) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_line(pp);
    }
    else if (dir_len == 7 && strncmp(dir_start, "importc", 7) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_importc(pp);
    }
    else if (dir_len == 9 && strncmp(dir_start, "importcpp", 9) == 0) {
        if (!pp_is_active(pp)) {
            pp_skip_to_eol(pp);
            return true;
        }
        return pp_handle_importcpp(pp);
    }
    else {
        // Unknown directive
        if (pp_is_active(pp)) {
            char dir_name[32];
            size_t copy_len = dir_len < 31 ? dir_len : 31;
            memcpy(dir_name, dir_start, copy_len);
            dir_name[copy_len] = '\0';
            pp_error(pp, "Unknown preprocessor directive '#%s'", dir_name);
            return false;
        }
        pp_skip_to_eol(pp);
        return true;
    }
}

char *preprocessor_process(Preprocessor *pp, const char *source, 
                           const char *filename) {
    pp->source = source;
    pp->current = source;
    pp->current_file = filename;
    pp->line = 1;
    pp->had_error = false;
    
    // Reset output
    pp->output_size = 0;
    if (pp->output) pp->output[0] = '\0';
    
    for (;;) {
        // Check for end of current file
        while (*pp->current == '\0') {
            // End of current file - pop include stack if possible
            if (pp->include_depth > 0) {
                pp->include_depth--;
                
                // Emit line marker if enabled
                if (pp->emit_line_markers && pp->include_stack[pp->include_depth].filename) {
                    char marker[256];
                    snprintf(marker, sizeof(marker), "\n#line %zu \"%s\"\n",
                             pp->include_stack[pp->include_depth].line,
                             pp->include_stack[pp->include_depth].filename);
                    pp_output(pp, marker, 0);
                }
                
                // Restore state from parent file
                pp->current_file = pp->include_stack[pp->include_depth].filename;
                pp->source = pp->include_stack[pp->include_depth].source;
                pp->current = pp->include_stack[pp->include_depth].current;
                pp->line = pp->include_stack[pp->include_depth].line;
            } else {
                // No more files to process
                goto done;
            }
        }
        
        // Handle newlines
        if (*pp->current == '\n') {
            if (pp_is_active(pp)) {
                pp_output_char(pp, '\n');
            }
            pp->current++;
            pp->line++;
            continue;
        }
        
        // Skip whitespace at start of line, check for directive
        const char *line_start = pp->current;
        while (*pp->current == ' ' || *pp->current == '\t') {
            pp->current++;
        }
        
        // Preprocessor directive
        if (*pp->current == '#') {
            if (!pp_process_directive(pp)) {
                if (pp->had_error) return NULL;
            }
            pp_skip_to_eol(pp);
            continue;
        }
        
        // Not a directive, restore position
        pp->current = line_start;
        
        // Process line content
        while (*pp->current && *pp->current != '\n') {
            // Comments
            if (pp->current[0] == '/' && pp->current[1] == '/') {
                // Single-line comment
                if (pp->keep_comments && pp_is_active(pp)) {
                    while (*pp->current && *pp->current != '\n') {
                        pp_output_char(pp, *pp->current++);
                    }
                } else {
                    pp_skip_to_eol(pp);
                }
                continue;
            }
            
            if (pp->current[0] == '/' && pp->current[1] == '*') {
                // Multi-line comment
                if (pp->keep_comments && pp_is_active(pp)) {
                    pp_output_char(pp, *pp->current++);
                    pp_output_char(pp, *pp->current++);
                }
                else {
                    pp->current += 2;
                }
                
                while (*pp->current && !(pp->current[0] == '*' && pp->current[1] == '/')) {
                    if (*pp->current == '\n') {
                        pp->line++;
                        if (pp_is_active(pp)) pp_output_char(pp, '\n');
                    } else if (pp->keep_comments && pp_is_active(pp)) {
                        pp_output_char(pp, *pp->current);
                    }
                    pp->current++;
                }
                
                if (*pp->current) {
                    if (pp->keep_comments && pp_is_active(pp)) {
                        pp_output_char(pp, *pp->current++);
                        pp_output_char(pp, *pp->current++);
                    } else {
                        pp->current += 2;
                    }
                }
                continue;
            }
            
            // String literals (don't expand macros inside)
            if (*pp->current == '"' || *pp->current == '\'') {
                char quote = *pp->current;
                if (pp_is_active(pp)) pp_output_char(pp, *pp->current);
                pp->current++;
                
                while (*pp->current && *pp->current != quote && *pp->current != '\n') {
                    if (*pp->current == '\\' && pp->current[1]) {
                        if (pp_is_active(pp)) {
                            pp_output_char(pp, *pp->current++);
                            pp_output_char(pp, *pp->current++);
                        } else {
                            pp->current += 2;
                        }
                    } else {
                        if (pp_is_active(pp)) pp_output_char(pp, *pp->current);
                        pp->current++;
                    }
                }
                
                if (*pp->current == quote) {
                    if (pp_is_active(pp)) pp_output_char(pp, *pp->current);
                    pp->current++;
                }
                continue;
            }
            
            // Identifier (potential macro)
            if (pp_is_active(pp) && (isalpha(*pp->current) || *pp->current == '_')) {
                const char *id_start = pp->current;
                while (isalnum(*pp->current) || *pp->current == '_') {
                    pp->current++;
                }
                size_t id_len = pp->current - id_start;
                
                char *name = malloc(id_len + 1);
                memcpy(name, id_start, id_len);
                name[id_len] = '\0';
                
                const Macro *m = preprocessor_get_macro(pp, name);
                
                if (m && m->type == MACRO_OBJECT) {
                    // Expand object-like macro
                    char *expanded = pp_expand_macros(pp, m->body);
                    if (expanded) {
                        pp_output(pp, expanded, 0);
                        free(expanded);
                    }
                } else if (m && m->type == MACRO_FUNCTION) {
                    // Check for (
                    const char *saved = pp->current;
                    while (*pp->current == ' ' || *pp->current == '\t') pp->current++;
                    
                    if (*pp->current == '(') {
                        pp->current++;
                        
                        // Parse arguments
                        const char *args[PP_MAX_MACRO_PARAMS];
                        size_t arg_count = 0;
                        
                        while (*pp->current && *pp->current != ')') {
                            while (*pp->current == ' ' || *pp->current == '\t') pp->current++;
                            
                            const char *arg_start = pp->current;
                            int paren_depth = 0;
                            
                            while (*pp->current && (paren_depth > 0 || 
                                   (*pp->current != ',' && *pp->current != ')'))) {
                                if (*pp->current == '(') paren_depth++;
                                else if (*pp->current == ')') paren_depth--;
                                else if (*pp->current == '\n') pp->line++;
                                pp->current++;
                            }
                            
                            size_t arg_len = pp->current - arg_start;
                            while (arg_len > 0 && (arg_start[arg_len-1] == ' ' || 
                                   arg_start[arg_len-1] == '\t')) {
                                arg_len--;
                            }
                            
                            char *arg = malloc(arg_len + 1);
                            memcpy(arg, arg_start, arg_len);
                            arg[arg_len] = '\0';
                            args[arg_count++] = arg;
                            
                            if (*pp->current == ',') pp->current++;
                        }
                        
                        if (*pp->current == ')') pp->current++;
                        
                        char *expanded = pp_expand_function_macro(pp, m, args, arg_count);
                        
                        for (size_t i = 0; i < arg_count; i++) {
                            free((char*)args[i]);
                        }
                        
                        if (expanded) {
                            pp_output(pp, expanded, 0);
                            free(expanded);
                        }
                    } else {
                        // Not a function call, output as-is
                        pp->current = saved;
                        pp_output(pp, id_start, id_len);
                    }
                } else {
                    // Not a macro
                    pp_output(pp, id_start, id_len);
                }
                
                free(name);
                continue;
            }
            
            // Regular character
            if (pp_is_active(pp)) {
                pp_output_char(pp, *pp->current);
            }
            pp->current++;
        }
    }
    
done:
    // Check for unclosed conditionals
    if (pp->condition_depth > 0) {
        pp_error(pp, "Unterminated #if/#ifdef (started at line %zu)",
                 pp->condition_stack[pp->condition_depth - 1].line);
        return NULL;
    }
    
    return pp->output ? strdup(pp->output) : strdup("");
}

char *preprocessor_process_file_to_string(Preprocessor *pp, const char *filename) {
    char *source = preprocessor_read_file(filename);
    if (!source) {
        pp_error(pp, "Cannot read file '%s': %s", filename, strerror(errno));
        return NULL;
    }
    
    // Get canonical path
    char *resolved = realpath(filename, NULL);
    const char *file_path = resolved ? resolved : filename;
    
    char *result = preprocessor_process(pp, source, file_path);
    
    free(source);
    free(resolved);
    
    return result;
}

// ============================================================================
// Legacy interface (parse to statements)
// ============================================================================

bool preprocessor_process_file(Preprocessor *pp, const char *filename,
                               Stmt ***statements, size_t *stmt_count) {
    // Preprocess the file
    char *preprocessed = preprocessor_process_file_to_string(pp, filename);
    if (!preprocessed) {
        return false;
    }
    
    // Parse the preprocessed source
    Lexer lexer;
    lexer_init(&lexer, preprocessed);
    
    Parser parser;
    parser_init(&parser, &lexer);
    
    // Allocate statement array
    size_t capacity = 64;
    *statements = malloc(capacity * sizeof(Stmt*));
    *stmt_count = 0;
    
    // Parse all statements
    while (!parser_check(&parser, TOK_EOF)) {
        Stmt *stmt = parse_statement(&parser);
        if (!stmt) {
            if (parser.had_error) {
                pp_error(pp, "Parse error in preprocessed output");
                free(preprocessed);
                return false;
            }
            break;
        }
        
        // Grow array if needed
        if (*stmt_count >= capacity) {
            capacity *= 2;
            *statements = realloc(*statements, capacity * sizeof(Stmt*));
        }
        
        (*statements)[(*stmt_count)++] = stmt;
    }
    
    free(preprocessed);
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

Preprocessor *preprocessor_create(const char *std_path) {
    Preprocessor *pp = calloc(1, sizeof(Preprocessor));
    if (!pp) return NULL;
    
    // Set default std path
    if (std_path) {
        preprocessor_add_include_path(pp, std_path);
    } else {
        // Try common locations
        preprocessor_add_include_path(pp, "src/std");
        preprocessor_add_include_path(pp, "./std");
        preprocessor_add_include_path(pp, "/usr/local/include/fcx");
    }
    
    // Define built-in macros
    preprocessor_define(pp, "__FCX__", "1");
    preprocessor_define(pp, "__FCX_VERSION__", "\"0.2.12\"");
    
    // Platform macros
#ifdef __linux__
    preprocessor_define(pp, "__linux__", "1");
    preprocessor_define(pp, "__unix__", "1");
#endif
#ifdef __APPLE__
    preprocessor_define(pp, "__APPLE__", "1");
    preprocessor_define(pp, "__unix__", "1");
#endif
#ifdef _WIN32
    preprocessor_define(pp, "_WIN32", "1");
#endif
    
    // Architecture
#ifdef __x86_64__
    preprocessor_define(pp, "__x86_64__", "1");
#endif
#ifdef __aarch64__
    preprocessor_define(pp, "__aarch64__", "1");
#endif
    
    return pp;
}

void preprocessor_destroy(Preprocessor *pp) {
    if (!pp) return;
    
    // Free include paths
    for (size_t i = 0; i < pp->include_path_count; i++) {
        free(pp->include_paths[i]);
    }
    
    // Free macros
    for (size_t i = 0; i < PP_MAX_MACROS; i++) {
        Macro *m = pp->macros[i];
        while (m) {
            Macro *next = m->next;
            macro_destroy(m);
            m = next;
        }
    }
    
    // Free included files list
    IncludedFile *inc = pp->included_files;
    while (inc) {
        IncludedFile *next = inc->next;
        free(inc->path);
        free(inc);
        inc = next;
    }
    
    // Free output buffer
    free(pp->output);
    
    free(pp);
}

void preprocessor_reset(Preprocessor *pp) {
    pp->condition_depth = 0;
    pp->include_depth = 0;
    pp->had_error = false;
    pp->error_message[0] = '\0';
    pp->output_size = 0;
    if (pp->output) pp->output[0] = '\0';
}

// ============================================================================
// Error handling
// ============================================================================

const char *preprocessor_get_error(Preprocessor *pp) {
    return pp->error_message;
}

SourceLocation preprocessor_get_error_location(Preprocessor *pp) {
    return pp->error_location;
}

bool preprocessor_had_error(Preprocessor *pp) {
    return pp->had_error;
}
