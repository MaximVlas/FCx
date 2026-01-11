/**
 * FCx LLVM Code Generation Implementation
 * 
 * Translates FC IR to LLVM IR, preserving FCx operator semantics.
 * This module bridges the gap between FCx's operator-centric design
 * and LLVM's SSA-based IR.
 * 
 * Compilation flow:
 *   FCx Source → FCx IR → FC IR → [FCx/FC Optimizations] → LLVM IR → Executable
 */
#include "llvm_codegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

LLVMCodegenContext *llvm_codegen_create(const CpuFeatures *features,
                                          const LLVMBackendConfig *config) {
    LLVMCodegenContext *ctx = calloc(1, sizeof(LLVMCodegenContext));
    if (!ctx) return NULL;
    
    ctx->backend = llvm_backend_create(features, config);
    if (!ctx->backend) {
        free(ctx);
        return NULL;
    }
    
    // Initialize statistics to zero (calloc already does this, but explicit is clearer)
    ctx->operators_emitted = 0;
    ctx->atomics_emitted = 0;
    ctx->syscalls_emitted = 0;
    ctx->memory_ops_emitted = 0;
    ctx->fc_module = NULL;
    ctx->current_function = NULL;
    
    return ctx;
}

void llvm_codegen_destroy(LLVMCodegenContext *ctx) {
    if (!ctx) return;
    
    // Clean up backend first (it owns LLVM resources)
    if (ctx->backend) {
        llvm_backend_destroy(ctx->backend);
        ctx->backend = NULL;
    }
    
    // Clear references (defensive programming)
    ctx->fc_module = NULL;
    ctx->current_function = NULL;
    
    // Free the context itself
    free(ctx);
}

bool llvm_codegen_module(LLVMCodegenContext *ctx, const FcIRModule *module) {
    if (!ctx || !module) return false;
    if (!ctx->backend) return false;
    
    // Store module reference
    ctx->fc_module = module;
    
    // Reset statistics for new module
    ctx->operators_emitted = 0;
    ctx->atomics_emitted = 0;
    ctx->syscalls_emitted = 0;
    ctx->memory_ops_emitted = 0;
    
    // Emit the module through the backend
    bool success = llvm_emit_module(ctx->backend, module);
    
    if (success) {
        // Update statistics from backend
        ctx->operators_emitted = ctx->backend->instruction_count;
        
        // Count atomic operations, syscalls, and memory ops from module
        for (uint32_t i = 0; i < module->function_count; i++) {
            const FcIRFunction *fn = &module->functions[i];
            for (uint32_t j = 0; j < fn->block_count; j++) {
                const FcIRBasicBlock *blk = &fn->blocks[j];
                for (uint32_t k = 0; k < blk->instruction_count; k++) {
                    const FcIRInstruction *instr = &blk->instructions[k];
                    
                    // Count atomic operations
                    if (instr->opcode == FCIR_XCHG || instr->opcode == FCIR_XADD ||
                        instr->opcode == FCIR_CMPXCHG || instr->opcode == FCIR_MFENCE ||
                        instr->opcode == FCIR_LFENCE || instr->opcode == FCIR_SFENCE) {
                        ctx->atomics_emitted++;
                    }
                    
                    // Count syscalls
                    if (instr->opcode == FCIR_SYSCALL) {
                        ctx->syscalls_emitted++;
                    }
                    
                    // Count memory operations (MOV with memory operands)
                    if (instr->opcode == FCIR_MOV || instr->opcode == FCIR_MOVZX ||
                        instr->opcode == FCIR_MOVSX || instr->opcode == FCIR_LEA) {
                        if (instr->operand_count > 0) {
                            if (instr->operands[0].type == FC_OPERAND_MEMORY ||
                                (instr->operand_count > 1 && 
                                 instr->operands[1].type == FC_OPERAND_MEMORY)) {
                                ctx->memory_ops_emitted++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    return success;
}

bool llvm_codegen_function(LLVMCodegenContext *ctx, const FcIRFunction *function) {
    if (!ctx || !function) return false;
    if (!ctx->backend) return false;
    
    // Store current function reference for debugging/diagnostics
    ctx->current_function = function;
    
    // Emit through backend
    bool success = llvm_emit_function(ctx->backend, function);
    
    // Clear current function on failure (defensive)
    if (!success) {
        ctx->current_function = NULL;
    }
    
    return success;
}

bool llvm_codegen_executable(LLVMCodegenContext *ctx, const FcIRModule *module,
                              const char *output_path) {
    if (!ctx || !module || !output_path) return false;
    if (!ctx->backend) return false;
    
    // Validate output path is not empty
    if (output_path[0] == '\0') return false;
    
    // Generate module IR
    if (!llvm_codegen_module(ctx, module)) return false;
    
    // Verify module before compiling (catches IR errors early)
    if (!llvm_verify_module(ctx->backend)) return false;
    
    // Compile and link to executable
    return llvm_compile_and_link(ctx->backend, output_path);
}

bool llvm_codegen_object(LLVMCodegenContext *ctx, const FcIRModule *module,
                          const char *output_path) {
    if (!ctx || !module || !output_path) return false;
    if (!ctx->backend) return false;
    
    // Validate output path
    if (output_path[0] == '\0') return false;
    
    // Generate module IR
    if (!llvm_codegen_module(ctx, module)) return false;
    
    // Verify before generating object file
    if (!llvm_verify_module(ctx->backend)) return false;
    
    // Generate object file
    return llvm_generate_object_file(ctx->backend, output_path);
}

bool llvm_codegen_assembly(LLVMCodegenContext *ctx, const FcIRModule *module,
                            const char *output_path) {
    if (!ctx || !module || !output_path) return false;
    if (!ctx->backend) return false;
    
    // Validate output path
    if (output_path[0] == '\0') return false;
    
    // Generate module IR
    if (!llvm_codegen_module(ctx, module)) return false;
    
    // Verify before generating assembly
    if (!llvm_verify_module(ctx->backend)) return false;
    
    // Generate assembly file
    return llvm_generate_assembly(ctx->backend, output_path);
}

bool llvm_codegen_bitcode(LLVMCodegenContext *ctx, const FcIRModule *module,
                           const char *output_path) {
    if (!ctx || !module || !output_path) return false;
    if (!ctx->backend) return false;
    
    // Validate output path
    if (output_path[0] == '\0') return false;
    
    // Generate module IR
    if (!llvm_codegen_module(ctx, module)) return false;
    
    // Verify before generating bitcode
    if (!llvm_verify_module(ctx->backend)) return false;
    
    // Generate bitcode file
    return llvm_generate_bitcode(ctx->backend, output_path);
}

void llvm_codegen_print_ir(LLVMCodegenContext *ctx, FILE *output) {
    if (!ctx) return;
    if (!ctx->backend) return;
    
    // Use stderr as fallback if output is NULL
    FILE *out = output ? output : stderr;
    
    // Print the LLVM IR module
    llvm_print_module(ctx->backend, out);
}

void llvm_codegen_print_stats(const LLVMCodegenContext *ctx) {
    if (!ctx) return;
    
    printf("\n=== LLVM Codegen Statistics ===\n");
    
    // Print FC IR level statistics
    printf("FC IR Instructions:   %u\n", ctx->operators_emitted);
    printf("Atomic Operations:    %u\n", ctx->atomics_emitted);
    printf("Syscalls:             %u\n", ctx->syscalls_emitted);
    printf("Memory Operations:    %u\n", ctx->memory_ops_emitted);
    
    // Calculate and print percentages if we have data
    if (ctx->operators_emitted > 0) {
        float atomic_pct = (ctx->atomics_emitted * 100.0f) / ctx->operators_emitted;
        float syscall_pct = (ctx->syscalls_emitted * 100.0f) / ctx->operators_emitted;
        float memory_pct = (ctx->memory_ops_emitted * 100.0f) / ctx->operators_emitted;
        
        printf("  Atomic:  %.1f%%\n", atomic_pct);
        printf("  Syscall: %.1f%%\n", syscall_pct);
        printf("  Memory:  %.1f%%\n", memory_pct);
    }
    
    // Print backend statistics if available
    if (ctx->backend) {
        llvm_print_statistics(ctx->backend);
    }
}

const char *llvm_codegen_get_error(const LLVMCodegenContext *ctx) {
    if (!ctx) return "Invalid codegen context (NULL)";
    if (!ctx->backend) return "Backend not initialized";
    
    // Get error from backend
    const char *error = llvm_backend_get_error(ctx->backend);
    
    // Provide default message if backend has no error
    return error ? error : "No error";
}

LLVMBackend *llvm_codegen_get_backend(LLVMCodegenContext *ctx) {
    return ctx ? ctx->backend : NULL;
}

LLVMModuleRef llvm_codegen_get_module(LLVMCodegenContext *ctx) {
    // Safely navigate through pointers
    if (!ctx) return NULL;
    if (!ctx->backend) return NULL;
    
    return ctx->backend->module;
}