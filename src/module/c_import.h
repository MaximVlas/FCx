/**
 * FCx C Import - Bridge to C libraries via clang
 * 
 * Handles #importc <header.h> directives by:
 * 1. Using clang to parse C headers
 * 2. Extracting function declarations
 * 3. Generating LLVM IR declarations for FCX to link against
 * 
 * Usage in FCX:
 *   #importc <stdio.h>
 *   #importc <math.h>
 *   #importc "mylib.h"
 */

#ifndef FCX_C_IMPORT_H
#define FCX_C_IMPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Maximum limits
#define C_IMPORT_MAX_HEADERS 64
#define C_IMPORT_MAX_FUNCTIONS 1024
#define C_IMPORT_MAX_PARAMS 32

// C type kinds (simplified mapping to FCX types)
typedef enum {
    C_TYPE_VOID,
    C_TYPE_BOOL,
    C_TYPE_CHAR,
    C_TYPE_SCHAR,
    C_TYPE_UCHAR,
    C_TYPE_SHORT,
    C_TYPE_USHORT,
    C_TYPE_INT,
    C_TYPE_UINT,
    C_TYPE_LONG,
    C_TYPE_ULONG,
    C_TYPE_LONGLONG,
    C_TYPE_ULONGLONG,
    C_TYPE_FLOAT,
    C_TYPE_DOUBLE,
    C_TYPE_LONGDOUBLE,
    C_TYPE_POINTER,
    C_TYPE_ARRAY,
    C_TYPE_STRUCT,
    C_TYPE_UNION,
    C_TYPE_ENUM,
    C_TYPE_FUNCTION,
    C_TYPE_UNKNOWN
} CTypeKind;

// C type representation
typedef struct CType {
    CTypeKind kind;
    bool is_const;
    bool is_volatile;
    bool is_restrict;
    struct CType *pointee;      // For pointers
    struct CType *element;      // For arrays
    size_t array_size;          // For fixed-size arrays
    const char *name;           // For struct/union/enum names
} CType;

// C function parameter
typedef struct {
    const char *name;           // Parameter name (may be NULL)
    CType *type;                // Parameter type
} CParam;

// C function declaration
typedef struct {
    const char *name;           // Function name
    CType *return_type;         // Return type
    CParam *params;             // Parameters
    size_t param_count;
    bool is_variadic;           // Has ... parameter
    bool is_inline;
    bool is_static;
    const char *header;         // Which header it came from
} CFuncDecl;

// C struct/union field
typedef struct {
    const char *name;
    CType *type;
    size_t offset;              // Byte offset in struct
    size_t bit_offset;          // For bitfields
    size_t bit_width;           // For bitfields (0 if not bitfield)
} CField;

// C struct/union declaration
typedef struct {
    const char *name;
    bool is_union;
    CField *fields;
    size_t field_count;
    size_t size;                // Total size in bytes
    size_t alignment;           // Alignment requirement
} CStructDecl;

// C enum constant
typedef struct {
    const char *name;
    int64_t value;
} CEnumConst;

// C enum declaration
typedef struct {
    const char *name;
    CEnumConst *constants;
    size_t constant_count;
} CEnumDecl;

// C typedef
typedef struct {
    const char *name;
    CType *type;
} CTypedef;

// C macro (simple object-like macros that expand to constants)
typedef struct {
    const char *name;
    const char *value;          // String representation
    bool is_integer;
    int64_t int_value;
} CMacroConst;

// Imported header info
typedef struct {
    const char *path;           // Header path
    bool is_system;             // <header.h> vs "header.h"
    bool processed;             // Already processed?
} CImportHeader;

// C Import context
typedef struct CImportContext {
    // Imported headers
    CImportHeader headers[C_IMPORT_MAX_HEADERS];
    size_t header_count;
    
    // Extracted declarations
    CFuncDecl *functions;
    size_t function_count;
    size_t function_capacity;
    
    CStructDecl *structs;
    size_t struct_count;
    size_t struct_capacity;
    
    CEnumDecl *enums;
    size_t enum_count;
    size_t enum_capacity;
    
    CTypedef *typedefs;
    size_t typedef_count;
    size_t typedef_capacity;
    
    CMacroConst *macros;
    size_t macro_count;
    size_t macro_capacity;
    
    // Include paths for clang
    char **include_paths;
    size_t include_path_count;
    
    // Clang path
    const char *clang_path;
    
    // Temp directory for generated files
    const char *temp_dir;
    
    // Error handling
    bool had_error;
    char error_message[512];
    
    // Libraries to link
    char **link_libs;
    size_t link_lib_count;
} CImportContext;

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * Create C import context
 */
CImportContext *c_import_create(void);

/**
 * Destroy C import context
 */
void c_import_destroy(CImportContext *ctx);

// ============================================================================
// Configuration
// ============================================================================

/**
 * Set clang executable path (default: "clang")
 */
void c_import_set_clang(CImportContext *ctx, const char *path);

/**
 * Add include path for clang
 */
bool c_import_add_include_path(CImportContext *ctx, const char *path);

/**
 * Add library to link
 */
bool c_import_add_link_lib(CImportContext *ctx, const char *lib);

// ============================================================================
// Import functions
// ============================================================================

/**
 * Import a C header file
 * @param ctx Import context
 * @param header Header name (e.g., "stdio.h" or "mylib.h")
 * @param is_system true for <header.h>, false for "header.h"
 * @return true on success
 */
bool c_import_header(CImportContext *ctx, const char *header, bool is_system);

/**
 * Process all pending imports
 * Runs clang to extract declarations
 */
bool c_import_process(CImportContext *ctx);

// ============================================================================
// Query functions
// ============================================================================

/**
 * Find a function by name
 */
const CFuncDecl *c_import_find_function(CImportContext *ctx, const char *name);

/**
 * Find a struct/union by name
 */
const CStructDecl *c_import_find_struct(CImportContext *ctx, const char *name);

/**
 * Find an enum by name
 */
const CEnumDecl *c_import_find_enum(CImportContext *ctx, const char *name);

/**
 * Find a typedef by name
 */
const CTypedef *c_import_find_typedef(CImportContext *ctx, const char *name);

/**
 * Find a macro constant by name
 */
const CMacroConst *c_import_find_macro(CImportContext *ctx, const char *name);

// ============================================================================
// LLVM IR Generation
// ============================================================================

/**
 * Generate LLVM IR declarations for all imported functions
 * @param ctx Import context
 * @return LLVM IR string (caller must free)
 */
char *c_import_generate_llvm_decls(CImportContext *ctx);

/**
 * Generate LLVM IR declaration for a specific function
 */
char *c_import_generate_func_decl(const CFuncDecl *func);

// ============================================================================
// Type conversion
// ============================================================================

/**
 * Convert C type to LLVM IR type string
 */
const char *c_type_to_llvm(const CType *type);

/**
 * Convert C type to FCX type name
 */
const char *c_type_to_fcx(const CType *type);

/**
 * Get size of C type in bytes
 */
size_t c_type_size(const CType *type);

/**
 * Get alignment of C type
 */
size_t c_type_align(const CType *type);

// ============================================================================
// Error handling
// ============================================================================

/**
 * Get last error message
 */
const char *c_import_get_error(CImportContext *ctx);

/**
 * Check if context had an error
 */
bool c_import_had_error(CImportContext *ctx);

#endif // FCX_C_IMPORT_H
