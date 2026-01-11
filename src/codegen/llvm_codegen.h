/**
 * FCx LLVM Code Generation Interface
 * 
 * This module provides the bridge between FCx operator semantics and LLVM IR.
 * The compilation flow is:
 *   FCx Source → FCx IR → FC IR → [FCx/FC Optimizations] → LLVM IR → [LLVM Opts] → Executable
 * 
 * Key design decisions:
 * - FCx/FC IR preserved for custom optimizations and debugging
 * - LLVM handles final code generation, register allocation, and linking
 * - Operator semantics defined in FC IR, translated to LLVM IR
 */

#ifndef FCX_LLVM_CODEGEN_H
#define FCX_LLVM_CODEGEN_H

#include "llvm_backend.h"
#include "../ir/fc_ir.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * LLVM code generation context
 * Wraps the LLVM backend with additional state for operator handling
 */
typedef struct {
    LLVMBackend *backend;
    const FcIRModule *fc_module;
    
    const FcIRFunction *current_function;
    uint32_t current_block_idx;
    
    uint32_t operators_emitted;
    uint32_t atomics_emitted;
    uint32_t syscalls_emitted;
    uint32_t memory_ops_emitted;
} LLVMCodegenContext;

/**
 * Create LLVM codegen context with optional CPU features and config
 */
LLVMCodegenContext *llvm_codegen_create(const CpuFeatures *features, 
                                         const LLVMBackendConfig *config);

/**
 * Destroy LLVM codegen context and free resources
 */
void llvm_codegen_destroy(LLVMCodegenContext *ctx);

/**
 * Generate LLVM IR from FC IR module
 */
bool llvm_codegen_module(LLVMCodegenContext *ctx, const FcIRModule *module);

/**
 * Generate LLVM IR for a single function
 */
bool llvm_codegen_function(LLVMCodegenContext *ctx, const FcIRFunction *function);

/**
 * Generate executable from FC IR module (IR + optimization + linking)
 */
bool llvm_codegen_executable(LLVMCodegenContext *ctx, const FcIRModule *module,
                              const char *output_path);

/**
 * Generate object file from FC IR module
 */
bool llvm_codegen_object(LLVMCodegenContext *ctx, const FcIRModule *module,
                          const char *output_path);

/**
 * Generate assembly file from FC IR module
 */
bool llvm_codegen_assembly(LLVMCodegenContext *ctx, const FcIRModule *module,
                            const char *output_path);

/**
 * Generate LLVM bitcode from FC IR module
 */
bool llvm_codegen_bitcode(LLVMCodegenContext *ctx, const FcIRModule *module,
                           const char *output_path);

/**
 * Print generated LLVM IR to output stream
 */
void llvm_codegen_print_ir(LLVMCodegenContext *ctx, FILE *output);

/**
 * Print codegen statistics
 */
void llvm_codegen_print_stats(const LLVMCodegenContext *ctx);

/**
 * Get error message from last operation
 */
const char *llvm_codegen_get_error(const LLVMCodegenContext *ctx);

/**
 * Get the underlying LLVM backend
 */
LLVMBackend *llvm_codegen_get_backend(LLVMCodegenContext *ctx);

/**
 * Get the LLVM module reference
 */
LLVMModuleRef llvm_codegen_get_module(LLVMCodegenContext *ctx);

#endif /* FCX_LLVM_CODEGEN_H */
