/**
 * FCx C Import - Zig-based C/C++ to LLVM IR bridge
 * 
 * Uses clang to compile C headers directly to LLVM IR,
 * which can then be linked with FCX generated code.
 */

#ifndef FCX_C_IMPORT_ZIG_H
#define FCX_C_IMPORT_ZIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context type
typedef struct CImportContext CImportContext;

// ============================================================================
// Lifecycle
// ============================================================================

CImportContext *fcx_c_import_create(void);
void fcx_c_import_destroy(CImportContext *ctx);

// ============================================================================
// Configuration
// ============================================================================

bool fcx_c_import_add_include_path(CImportContext *ctx, const char *path);
bool fcx_c_import_add_link_lib(CImportContext *ctx, const char *lib);

// ============================================================================
// Import Functions
// ============================================================================

bool fcx_c_import_header(CImportContext *ctx, const char *header, bool is_system);
bool fcx_c_import_add_function(CImportContext *ctx, const char *func_name);
bool fcx_c_import_process(CImportContext *ctx);

// ============================================================================
// LLVM IR Access - THE KEY FUNCTIONS
// ============================================================================

/**
 * Get the generated LLVM IR as text
 * This IR can be parsed by LLVM and linked with FCX code
 */
const char *fcx_c_import_get_llvm_ir(CImportContext *ctx);

/**
 * Get size of LLVM IR
 */
size_t fcx_c_import_get_llvm_ir_size(CImportContext *ctx);

/**
 * Compile C imports directly to object file
 * This object can be linked with FCX output
 */
bool fcx_c_import_compile_to_object(CImportContext *ctx, const char *output_path);

// ============================================================================
// Link Libraries
// ============================================================================

size_t fcx_c_import_get_link_lib_count(CImportContext *ctx);
const char *fcx_c_import_get_link_lib(CImportContext *ctx, size_t index);

// ============================================================================
// Error Handling
// ============================================================================

const char *fcx_c_import_get_error(CImportContext *ctx);
bool fcx_c_import_had_error(CImportContext *ctx);

// ============================================================================
// Legacy/Compat (not used in new approach)
// ============================================================================

size_t fcx_c_import_get_function_count(CImportContext *ctx);
void *fcx_c_import_get_function(CImportContext *ctx, size_t index);
void *fcx_c_import_find_function(CImportContext *ctx, const char *name);
void *fcx_c_import_find_struct(CImportContext *ctx, const char *name);
char *fcx_c_import_generate_llvm_decls(CImportContext *ctx);
void fcx_c_import_free_string(char *str);

#ifdef __cplusplus
}
#endif

#endif // FCX_C_IMPORT_ZIG_H
