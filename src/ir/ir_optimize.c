#define _POSIX_C_SOURCE 200809L
#include "ir_optimize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Constant Folding Pass
// ============================================================================

bool opt_constant_folding(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            // Look for binary operations with constant operands
            if (instr->opcode >= FCXIR_ADD && instr->opcode <= FCXIR_MOD) {
                // Check if both operands are constants
                bool left_is_const = false;
                bool right_is_const = false;
                int64_t left_val = 0;
                int64_t right_val = 0;
                
                // Look backwards for constant definitions
                for (uint32_t j = 0; j < i; j++) {
                    FcxIRInstruction* prev = &block->instructions[j];
                    if (prev->opcode == FCXIR_CONST) {
                        if (prev->u.const_op.dest.id == instr->u.binary_op.left.id) {
                            left_is_const = true;
                            left_val = prev->u.const_op.value;
                        }
                        if (prev->u.const_op.dest.id == instr->u.binary_op.right.id) {
                            right_is_const = true;
                            right_val = prev->u.const_op.value;
                        }
                    }
                }
                
                // If both are constants, fold the operation
                if (left_is_const && right_is_const) {
                    int64_t result = 0;
                    bool can_fold = true;
                    
                    switch (instr->opcode) {
                        case FCXIR_ADD:
                            result = left_val + right_val;
                            break;
                        case FCXIR_SUB:
                            result = left_val - right_val;
                            break;
                        case FCXIR_MUL:
                            result = left_val * right_val;
                            break;
                        case FCXIR_DIV:
                            if (right_val != 0) {
                                result = left_val / right_val;
                            } else {
                                can_fold = false;
                            }
                            break;
                        case FCXIR_MOD:
                            if (right_val != 0) {
                                result = left_val % right_val;
                            } else {
                                can_fold = false;
                            }
                            break;
                        default:
                            can_fold = false;
                            break;
                    }
                    
                    if (can_fold) {
                        // Replace with constant
                        instr->opcode = FCXIR_CONST;
                        instr->u.const_op.value = result;
                        changed = true;
                    }
                }
            }
        }
    }
    
    return changed;
}

// ============================================================================
// Dead Code Elimination Pass
// ============================================================================

bool opt_dead_code_elimination(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    
    // Track which virtual registers are used
    bool* used = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    if (!used) return false;
    
    // Mark all used registers
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_MOV:
                    // MOV is register-to-register, mark source as used
                    used[instr->u.load_store.src.id] = true;
                    break;
                case FCXIR_LOAD:
                case FCXIR_STORE:
                case FCXIR_LOAD_VOLATILE:
                case FCXIR_STORE_VOLATILE:
                    used[instr->u.load_store.src.id] = true;
                    break;
                    
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD:
                case FCXIR_AND:
                case FCXIR_OR:
                case FCXIR_XOR:
                case FCXIR_LSHIFT:
                case FCXIR_RSHIFT:
                case FCXIR_LOGICAL_RSHIFT:
                case FCXIR_ROTATE_LEFT:
                case FCXIR_ROTATE_RIGHT:
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB:
                    used[instr->u.binary_op.left.id] = true;
                    used[instr->u.binary_op.right.id] = true;
                    break;
                    
                case FCXIR_NEG:
                case FCXIR_NOT:
                case FCXIR_ATOMIC_LOAD:
                    used[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_BRANCH:
                    used[instr->u.branch_op.cond.id] = true;
                    break;
                    
                case FCXIR_RETURN:
                    if (instr->u.return_op.has_value) {
                        used[instr->u.return_op.value.id] = true;
                    }
                    break;
                
                case FCXIR_CALL:
                    // Mark all arguments as used
                    for (uint8_t j = 0; j < instr->u.call_op.arg_count; j++) {
                        used[instr->u.call_op.args[j].id] = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Remove instructions that define unused registers
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        uint32_t write_idx = 0;
        
        for (uint32_t read_idx = 0; read_idx < block->instruction_count; read_idx++) {
            FcxIRInstruction* instr = &block->instructions[read_idx];
            bool keep = true;
            
            // Check if this instruction defines a register
            switch (instr->opcode) {
                case FCXIR_CONST:
                    keep = used[instr->u.const_op.dest.id];
                    break;
                case FCXIR_CONST_BIGINT:
                    keep = used[instr->u.const_bigint_op.dest.id];
                    break;
                case FCXIR_MOV:
                case FCXIR_LOAD:
                case FCXIR_LOAD_VOLATILE:
                    keep = used[instr->u.load_store.dest.id];
                    break;
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD:
                case FCXIR_AND:
                case FCXIR_OR:
                case FCXIR_XOR:
                case FCXIR_LSHIFT:
                case FCXIR_RSHIFT:
                case FCXIR_LOGICAL_RSHIFT:
                case FCXIR_ROTATE_LEFT:
                case FCXIR_ROTATE_RIGHT:
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB:
                    keep = used[instr->u.binary_op.dest.id];
                    break;
                default:
                    // Keep all other instructions (stores, branches, etc.)
                    keep = true;
                    break;
            }
            
            if (keep) {
                if (write_idx != read_idx) {
                    block->instructions[write_idx] = block->instructions[read_idx];
                }
                write_idx++;
            } else {
                changed = true;
            }
        }
        
        block->instruction_count = write_idx;
    }
    
    free(used);
    return changed;
}

// ============================================================================
// Loop Invariant Code Motion (Simplified)
// ============================================================================

bool opt_loop_invariant_code_motion(FcxIRFunction* function) {
    if (!function) return false;
    
    // This is a placeholder for loop optimization
    // A full implementation would:
    // 1. Identify loops in the CFG
    // 2. Find loop-invariant computations
    // 3. Move them outside the loop
    
    // For now, just return false (no changes)
    return false;
}

// ============================================================================
// Type Checking Pass
// ============================================================================

bool opt_type_checking(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track types of virtual registers
    VRegType* types = (VRegType*)calloc(function->next_vreg_id, sizeof(VRegType));
    if (!types) return false;
    
    // Initialize all types to I64 (default integer type)
    // This prevents false positives from untracked types
    for (uint32_t i = 0; i < function->next_vreg_id; i++) {
        types[i] = VREG_TYPE_I64;
    }
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_CONST:
                    types[instr->u.const_op.dest.id] = instr->u.const_op.dest.type;
                    break;
                
                case FCXIR_CONST_BIGINT:
                    types[instr->u.const_bigint_op.dest.id] = instr->u.const_bigint_op.dest.type;
                    break;
                
                case FCXIR_MOV:
                    // Propagate type from source to destination
                    types[instr->u.load_store.dest.id] = types[instr->u.load_store.src.id];
                    break;
                    
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD: {
                    // Propagate type to destination (don't warn, just track)
                    VRegType left_type = types[instr->u.binary_op.left.id];
                    types[instr->u.binary_op.dest.id] = left_type;
                    break;
                }
                
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                    // Comparison results are boolean (stored as i64)
                    types[instr->u.binary_op.dest.id] = VREG_TYPE_I64;
                    break;
                
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB: {
                    // Result is a pointer
                    VRegType left_type = types[instr->u.binary_op.left.id];
                    types[instr->u.binary_op.dest.id] = left_type;
                    break;
                }
                
                case FCXIR_CALL:
                    // Call results are typically i64
                    types[instr->u.call_op.dest.id] = VREG_TYPE_I64;
                    break;
                
                default:
                    break;
            }
        }
    }
    
    free(types);
    return !has_error;
}

// ============================================================================
// Pointer Analysis Pass
// ============================================================================

bool opt_pointer_analysis(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track pointer types and their origins
    typedef struct {
        bool is_pointer;
        VRegType ptr_type;
        bool is_null;
        bool is_allocated;
    } PointerInfo;
    
    PointerInfo* ptr_info = (PointerInfo*)calloc(function->next_vreg_id, sizeof(PointerInfo));
    if (!ptr_info) return false;
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    // Mark as allocated pointer
                    ptr_info[instr->u.alloc_op.dest.id].is_pointer = true;
                    ptr_info[instr->u.alloc_op.dest.id].ptr_type = VREG_TYPE_PTR;
                    ptr_info[instr->u.alloc_op.dest.id].is_allocated = true;
                    ptr_info[instr->u.alloc_op.dest.id].is_null = false;
                    break;
                    
                case FCXIR_CONST:
                    // Check for null pointer (0)
                    if (instr->u.const_op.value == 0 && 
                        instr->u.const_op.dest.type == VREG_TYPE_PTR) {
                        ptr_info[instr->u.const_op.dest.id].is_pointer = true;
                        ptr_info[instr->u.const_op.dest.id].is_null = true;
                    }
                    break;
                    
                case FCXIR_MOV:
                    // MOV is register-to-register, propagate pointer info
                    ptr_info[instr->u.load_store.dest.id] = ptr_info[instr->u.load_store.src.id];
                    break;
                    
                case FCXIR_LOAD:
                case FCXIR_STORE:
                case FCXIR_LOAD_VOLATILE:
                case FCXIR_STORE_VOLATILE:
                    // Check for null pointer dereference
                    if (ptr_info[instr->u.load_store.src.id].is_null) {
                        fprintf(stderr, "Warning: Potential null pointer dereference\n");
                        has_error = true;
                    }
                    break;
                    
                case FCXIR_DEALLOC:
                    // Mark as deallocated
                    ptr_info[instr->u.unary_op.src.id].is_allocated = false;
                    break;
                    
                case FCXIR_PTR_CAST:
                    // Track pointer type changes
                    ptr_info[instr->u.ptr_op.dest.id].is_pointer = true;
                    ptr_info[instr->u.ptr_op.dest.id].ptr_type = instr->u.ptr_op.target_type;
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    free(ptr_info);
    return !has_error;
}

// ============================================================================
// Memory Safety Analysis Pass
// ============================================================================

bool opt_memory_safety_analysis(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track allocated and freed pointers
    bool* allocated = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* freed = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    
    if (!allocated || !freed) {
        free(allocated);
        free(freed);
        return false;
    }
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    allocated[instr->u.alloc_op.dest.id] = true;
                    freed[instr->u.alloc_op.dest.id] = false;
                    break;
                    
                case FCXIR_DEALLOC:
                    if (freed[instr->u.unary_op.src.id]) {
                        fprintf(stderr, "Warning: Double free detected\n");
                        has_error = true;
                    }
                    if (!allocated[instr->u.unary_op.src.id]) {
                        fprintf(stderr, "Warning: Freeing unallocated memory\n");
                        has_error = true;
                    }
                    freed[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_MOV:
                    // MOV is register-to-register, propagate freed status
                    if (freed[instr->u.load_store.src.id]) {
                        freed[instr->u.load_store.dest.id] = true;
                    }
                    break;
                    
                case FCXIR_LOAD:
                case FCXIR_STORE:
                    if (freed[instr->u.load_store.src.id]) {
                        fprintf(stderr, "Warning: Use after free detected\n");
                        has_error = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    free(allocated);
    free(freed);
    return !has_error;
}

// ============================================================================
// Leak Detection Pass
// ============================================================================

bool opt_leak_detection(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track allocated pointers that are never freed
    bool* allocated = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* freed = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* escaped = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    
    if (!allocated || !freed || !escaped) {
        free(allocated);
        free(freed);
        free(escaped);
        return false;
    }
    
    // First pass: track allocations and frees
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    allocated[instr->u.alloc_op.dest.id] = true;
                    break;
                    
                case FCXIR_DEALLOC:
                    freed[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_RETURN:
                    // If returning a pointer, it escapes
                    if (instr->u.return_op.has_value) {
                        escaped[instr->u.return_op.value.id] = true;
                    }
                    break;
                    
                case FCXIR_CALL:
                    // Pointers passed to functions escape
                    for (uint8_t j = 0; j < instr->u.call_op.arg_count; j++) {
                        escaped[instr->u.call_op.args[j].id] = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Check for leaks
    bool has_leaks = false;
    for (uint32_t i = 0; i < function->next_vreg_id; i++) {
        if (allocated[i] && !freed[i] && !escaped[i]) {
            fprintf(stderr, "Warning: Potential memory leak for %%v%u\n", i);
            has_leaks = true;
        }
    }
    
    free(allocated);
    free(freed);
    free(escaped);
    
    return !has_leaks;
}

// ============================================================================
// Run All Optimization Passes
// ============================================================================

bool ir_optimize_function(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    
    // Run optimization passes (silently)
    if (opt_constant_folding(function)) {
        changed = true;
    }
    
    if (opt_dead_code_elimination(function)) {
        changed = true;
    }
    
    // Run analysis passes (silently, only report errors)
    opt_type_checking(function);
    opt_pointer_analysis(function);
    opt_memory_safety_analysis(function);
    opt_leak_detection(function);
    
    return changed;
}

bool ir_optimize_module(FcxIRModule* module) {
    if (!module) return false;
    
    bool changed = false;
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        if (ir_optimize_function(&module->functions[i])) {
            changed = true;
        }
    }
    
    return changed;
}
