/**
 * FCx Hierarchical Multi-Stage Optimizer (HMSO) - Core Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include "../ir/fcx_ir.h"
#include "../ir/ir_optimize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

// ============================================================================
// Default Configurations
// ============================================================================

const HMSOConfig HMSO_CONFIG_O0 = {
    .level = OPT_LEVEL_O0,
    .enable_expensive_opts = false,
    .inline_threshold = 0,
    .unroll_count = 1,
    .vectorize = false,
    .enable_lto = false,
    .lto_iterations = 0,
    .chunk_size_min = 1,
    .chunk_size_max = 1000,
    .num_threads = 1,
    .convergence_threshold = 0.0,
    .use_profile = false,
    .profile_path = NULL,
};

const HMSOConfig HMSO_CONFIG_O1 = {
    .level = OPT_LEVEL_O1,
    .enable_expensive_opts = false,
    .inline_threshold = 50,
    .unroll_count = 2,
    .vectorize = false,
    .enable_lto = false,
    .lto_iterations = 0,
    .chunk_size_min = 10,
    .chunk_size_max = 200,
    .num_threads = 1,
    .convergence_threshold = 0.0,
    .use_profile = false,
    .profile_path = NULL,
};

const HMSOConfig HMSO_CONFIG_O2 = {
    .level = OPT_LEVEL_O2,
    .enable_expensive_opts = false,
    .inline_threshold = 100,
    .unroll_count = 4,
    .vectorize = true,
    .enable_lto = true,
    .lto_iterations = 1,
    .chunk_size_min = 20,
    .chunk_size_max = 300,
    .num_threads = 4,
    .convergence_threshold = 0.01,
    .use_profile = false,
    .profile_path = NULL,
};

const HMSOConfig HMSO_CONFIG_O3 = {
    .level = OPT_LEVEL_O3,
    .enable_expensive_opts = true,
    .inline_threshold = 200,
    .unroll_count = 8,
    .vectorize = true,
    .enable_lto = true,
    .lto_iterations = 3,
    .chunk_size_min = 10,
    .chunk_size_max = 100,
    .num_threads = 8,
    .convergence_threshold = 0.005,
    .use_profile = true,
    .profile_path = NULL,
};

const HMSOConfig HMSO_CONFIG_OMAX = {
    .level = OPT_LEVEL_OMAX,
    .enable_expensive_opts = true,
    .inline_threshold = 500,
    .unroll_count = 16,
    .vectorize = true,
    .enable_lto = true,
    .lto_iterations = 10,
    .chunk_size_min = 5,
    .chunk_size_max = 50,
    .num_threads = 16,
    .convergence_threshold = 0.001,
    .use_profile = true,
    .profile_path = NULL,
};

// ============================================================================
// Context Management
// ============================================================================

HMSOContext *hmso_create(const HMSOConfig *config) {
    HMSOContext *ctx = (HMSOContext *)calloc(1, sizeof(HMSOContext));
    if (!ctx) return NULL;
    
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config = HMSO_CONFIG_O2;  // Default to O2
    }
    
    ctx->global_index = NULL;
    ctx->chunks = NULL;
    ctx->num_chunks = 0;
    ctx->threads = NULL;
    ctx->num_threads = ctx->config.num_threads;
    
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    
    return ctx;
}

void hmso_destroy(HMSOContext *ctx) {
    if (!ctx) return;
    
    if (ctx->global_index) {
        hmso_free_global_index(ctx->global_index);
    }
    
    if (ctx->chunks) {
        for (uint32_t i = 0; i < ctx->num_chunks; i++) {
            if (ctx->chunks[i]) {
                free(ctx->chunks[i]->function_indices);
                free(ctx->chunks[i]->optimized_ir);
                free(ctx->chunks[i]);
            }
        }
        free(ctx->chunks);
    }
    
    free(ctx->threads);
    free(ctx);
}

// ============================================================================
// Utility Functions
// ============================================================================

// FNV-1a hash for strings
static uint64_t hash_string(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Hash file contents
uint64_t hmso_hash_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    
    uint64_t hash = 14695981039346656037ULL;
    int c;
    while ((c = fgetc(f)) != EOF) {
        hash ^= (uint8_t)c;
        hash *= 1099511628211ULL;
    }
    
    fclose(f);
    return hash;
}

// Hash function IR for incremental builds
uint64_t hmso_hash_function(const FcxIRFunction *func) {
    if (!func) return 0;
    
    uint64_t hash = hash_string(func->name);
    
    // Hash instruction opcodes and operands
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction *instr = &block->instructions[i];
            hash ^= instr->opcode;
            hash *= 1099511628211ULL;
        }
    }
    
    return hash;
}

// ============================================================================
// Stage 0: Summary Generation
// ============================================================================

// Calculate cyclomatic complexity
static uint32_t calculate_complexity(FcxIRFunction *func) {
    if (!func) return 0;
    
    uint32_t edges = 0;
    uint32_t nodes = func->block_count;
    
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        edges += block->successor_count;
    }
    
    // M = E - N + 2P (P = 1 for single function)
    return edges - nodes + 2;
}

// Analyze function flags
static uint32_t analyze_function_flags(FcxIRFunction *func) {
    if (!func) return FUNC_FLAG_NONE;
    
    uint32_t flags = FUNC_FLAG_NONE;
    bool has_calls = false;
    bool has_stores = false;
    bool has_loads = false;
    bool has_atomics = false;
    bool has_syscalls = false;
    bool may_return = false;
    
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction *instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_CALL:
                    has_calls = true;
                    break;
                case FCXIR_STORE:
                case FCXIR_STORE_VOLATILE:
                    has_stores = true;
                    break;
                case FCXIR_LOAD:
                case FCXIR_LOAD_VOLATILE:
                    has_loads = true;
                    break;
                case FCXIR_ATOMIC_LOAD:
                case FCXIR_ATOMIC_STORE:
                case FCXIR_ATOMIC_SWAP:
                case FCXIR_ATOMIC_CAS:
                case FCXIR_ATOMIC_ADD:
                case FCXIR_ATOMIC_SUB:
                    has_atomics = true;
                    break;
                case FCXIR_SYSCALL:
                    has_syscalls = true;
                    break;
                case FCXIR_RETURN:
                    may_return = true;
                    break;
                default:
                    break;
            }
        }
    }
    
    if (!has_calls) {
        flags |= FUNC_FLAG_LEAF;
    }
    
    // Pure functions have no side effects - syscalls are side effects
    if (!has_stores && !has_calls && !has_syscalls) {
        flags |= FUNC_FLAG_PURE;
        if (!has_loads) {
            flags |= FUNC_FLAG_CONST;
        }
    }
    
    if (!may_return) {
        flags |= FUNC_FLAG_NORETURN;
    }
    
    if (has_atomics) {
        flags |= FUNC_FLAG_HAS_ATOMICS;
    }
    
    if (has_syscalls) {
        flags |= FUNC_FLAG_HAS_SYSCALLS;
    }
    
    return flags;
}

// Analyze memory access patterns
static uint32_t analyze_memory_access(FcxIRFunction *func) {
    if (!func) return MEM_ACCESS_NONE;
    
    uint32_t access = MEM_ACCESS_NONE;
    
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction *instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_LOAD:
                case FCXIR_LOAD_VOLATILE:
                case FCXIR_ATOMIC_LOAD:
                    access |= MEM_ACCESS_READ;
                    break;
                case FCXIR_STORE:
                case FCXIR_STORE_VOLATILE:
                case FCXIR_ATOMIC_STORE:
                    access |= MEM_ACCESS_WRITE;
                    break;
                case FCXIR_ALLOC:
                case FCXIR_STACK_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                    access |= MEM_ACCESS_ALLOC;
                    break;
                case FCXIR_DEALLOC:
                    access |= MEM_ACCESS_FREE;
                    break;
                default:
                    break;
            }
        }
    }
    
    return access;
}

// Calculate inline cost heuristic
static uint32_t calculate_inline_cost(FcxIRFunction *func) {
    if (!func) return UINT32_MAX;
    
    uint32_t cost = 0;
    
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction *instr = &block->instructions[i];
            
            // Different instructions have different costs
            switch (instr->opcode) {
                case FCXIR_CONST:
                    cost += 1;
                    break;
                case FCXIR_CONST_BIGINT:
                    cost += 2;  // Bigint constants are slightly more expensive
                    break;
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_AND:
                case FCXIR_OR:
                case FCXIR_XOR:
                    cost += 2;
                    break;
                case FCXIR_MUL:
                    cost += 3;
                    break;
                case FCXIR_DIV:
                case FCXIR_MOD:
                    cost += 10;
                    break;
                case FCXIR_LOAD:
                case FCXIR_STORE:
                    cost += 5;
                    break;
                case FCXIR_CALL:
                    cost += 20;
                    break;
                case FCXIR_SYSCALL:
                    cost += 50;
                    break;
                default:
                    cost += 2;
                    break;
            }
        }
    }
    
    return cost;
}

// Extract call sites from function
static void extract_callsites(FcxIRFunction *func, FunctionSummary *summary) {
    if (!func || !summary) return;
    
    // Count call sites first
    uint32_t count = 0;
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            if (block->instructions[i].opcode == FCXIR_CALL) {
                count++;
            }
        }
    }
    
    if (count == 0) {
        summary->num_callsites = 0;
        summary->callsites = NULL;
        return;
    }
    
    summary->callsites = (CallSite *)calloc(count, sizeof(CallSite));
    if (!summary->callsites) return;
    
    uint32_t idx = 0;
    for (uint32_t b = 0; b < func->block_count; b++) {
        FcxIRBasicBlock *block = &func->blocks[b];
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction *instr = &block->instructions[i];
            if (instr->opcode == FCXIR_CALL) {
                CallSite *site = &summary->callsites[idx++];
                site->callee_name = strdup(instr->u.call_op.function);
                site->callee_hash = hash_string(site->callee_name);
                site->call_count = 1;
                site->arg_count = instr->u.call_op.arg_count;
                site->is_indirect = false;  // Direct calls only for now
                site->is_tail_call = false;  // TODO: detect tail calls
            }
        }
    }
    
    summary->num_callsites = count;
}

// Generate summary for a single function
static FunctionSummary *generate_function_summary(FcxIRFunction *func) {
    if (!func) return NULL;
    
    FunctionSummary *summary = (FunctionSummary *)calloc(1, sizeof(FunctionSummary));
    if (!summary) return NULL;
    
    summary->name = strdup(func->name);
    summary->hash = hmso_hash_function(func);
    
    // Count instructions
    summary->instruction_count = 0;
    for (uint32_t b = 0; b < func->block_count; b++) {
        summary->instruction_count += func->blocks[b].instruction_count;
    }
    
    summary->basic_block_count = func->block_count;
    summary->cyclomatic_complexity = calculate_complexity(func);
    summary->loop_depth_max = 0;  // TODO: implement loop detection
    
    summary->flags = analyze_function_flags(func);
    summary->memory_access = analyze_memory_access(func);
    
    extract_callsites(func, summary);
    
    summary->inline_cost = calculate_inline_cost(func);
    summary->is_inline_candidate = (summary->inline_cost < 100 && 
                                    !(summary->flags & FUNC_FLAG_NOINLINE));
    
    return summary;
}

CompilationSummary *hmso_generate_summary(FcxIRModule *module) {
    if (!module) return NULL;
    
    CompilationSummary *summary = (CompilationSummary *)calloc(1, sizeof(CompilationSummary));
    if (!summary) return NULL;
    
    summary->num_functions = module->function_count;
    summary->functions = (FunctionSummary *)calloc(module->function_count, 
                                                    sizeof(FunctionSummary));
    
    if (!summary->functions) {
        free(summary);
        return NULL;
    }
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        FunctionSummary *func_sum = generate_function_summary(&module->functions[i]);
        if (func_sum) {
            summary->functions[i] = *func_sum;
            free(func_sum);
        }
    }
    
    // TODO: Extract globals and build internal call edges
    summary->num_globals = 0;
    summary->globals = NULL;
    summary->num_edges = 0;
    summary->edges = NULL;
    
    return summary;
}
