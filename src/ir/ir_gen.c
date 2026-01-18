#define _POSIX_C_SOURCE 200809L
#include "ir_gen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

// ============================================================================
// IR Generator Lifecycle
// ============================================================================

IRGenerator* ir_gen_create(const char* module_name) {
    IRGenerator* gen = (IRGenerator*)malloc(sizeof(IRGenerator));
    if (!gen) return NULL;
    
    gen->module = fcx_ir_module_create(module_name);
    gen->current_function = NULL;
    gen->current_block = NULL;
    
    gen->symbol_table.names = NULL;
    gen->symbol_table.vregs = NULL;
    gen->symbol_table.is_global = NULL;
    gen->symbol_table.global_index = NULL;
    gen->symbol_table.count = 0;
    gen->symbol_table.capacity = 0;
    
    gen->next_label_id = 1;
    gen->current_scope_id = 1;  // Start at scope 1 (0 is global)
    
    // Initialize loop stack for break/continue
    gen->loop_stack.break_targets = NULL;
    gen->loop_stack.continue_targets = NULL;
    gen->loop_stack.depth = 0;
    gen->loop_stack.capacity = 0;
    
    gen->error_message = NULL;
    gen->has_error = false;
    
    return gen;
}

void ir_gen_destroy(IRGenerator* gen) {
    if (!gen) return;
    
    if (gen->module) {
        fcx_ir_module_destroy(gen->module);
    }
    
    for (size_t i = 0; i < gen->symbol_table.count; i++) {
        free(gen->symbol_table.names[i]);
    }
    free(gen->symbol_table.names);
    free(gen->symbol_table.vregs);
    free(gen->symbol_table.is_global);
    free(gen->symbol_table.global_index);
    
    // Free loop stack
    free(gen->loop_stack.break_targets);
    free(gen->loop_stack.continue_targets);
    
    if (gen->error_message) {
        free(gen->error_message);
    }
    
    free(gen);
}

// ============================================================================
// Symbol Table Management
// ============================================================================

static void ensure_symbol_table_capacity(IRGenerator* gen) {
    if (gen->symbol_table.count >= gen->symbol_table.capacity) {
        size_t new_capacity = gen->symbol_table.capacity == 0 ? 16 : gen->symbol_table.capacity * 2;
        
        char** new_names = (char**)realloc(gen->symbol_table.names, new_capacity * sizeof(char*));
        VirtualReg* new_vregs = (VirtualReg*)realloc(gen->symbol_table.vregs, new_capacity * sizeof(VirtualReg));
        bool* new_is_global = (bool*)realloc(gen->symbol_table.is_global, new_capacity * sizeof(bool));
        uint32_t* new_global_index = (uint32_t*)realloc(gen->symbol_table.global_index, new_capacity * sizeof(uint32_t));
        
        if (!new_names || !new_vregs || !new_is_global || !new_global_index) return;
        
        gen->symbol_table.names = new_names;
        gen->symbol_table.vregs = new_vregs;
        gen->symbol_table.is_global = new_is_global;
        gen->symbol_table.global_index = new_global_index;
        gen->symbol_table.capacity = new_capacity;
    }
}

void ir_gen_add_symbol(IRGenerator* gen, const char* name, VirtualReg vreg) {
    if (!gen || !name) return;
    
    ensure_symbol_table_capacity(gen);
    
    gen->symbol_table.names[gen->symbol_table.count] = strdup(name);
    gen->symbol_table.vregs[gen->symbol_table.count] = vreg;
    gen->symbol_table.is_global[gen->symbol_table.count] = false;
    gen->symbol_table.global_index[gen->symbol_table.count] = 0;
    gen->symbol_table.count++;
}

void ir_gen_add_global_symbol(IRGenerator* gen, const char* name, uint32_t global_index) {
    if (!gen || !name) return;
    
    ensure_symbol_table_capacity(gen);
    
    gen->symbol_table.names[gen->symbol_table.count] = strdup(name);
    gen->symbol_table.vregs[gen->symbol_table.count] = (VirtualReg){0};  // Not used for globals
    gen->symbol_table.is_global[gen->symbol_table.count] = true;
    gen->symbol_table.global_index[gen->symbol_table.count] = global_index;
    gen->symbol_table.count++;
}

VirtualReg ir_gen_lookup_symbol(IRGenerator* gen, const char* name, bool* found) {
    VirtualReg invalid = {0};
    *found = false;
    
    if (!gen || !name) return invalid;
    
    for (size_t i = gen->symbol_table.count; i > 0; i--) {
        if (strcmp(gen->symbol_table.names[i - 1], name) == 0) {
            *found = true;
            return gen->symbol_table.vregs[i - 1];
        }
    }
    
    return invalid;
}

bool ir_gen_is_global_symbol(IRGenerator* gen, const char* name, uint32_t* global_index) {
    if (!gen || !name) return false;
    
    for (size_t i = gen->symbol_table.count; i > 0; i--) {
        if (strcmp(gen->symbol_table.names[i - 1], name) == 0) {
            if (gen->symbol_table.is_global[i - 1]) {
                if (global_index) {
                    *global_index = gen->symbol_table.global_index[i - 1];
                }
                return true;
            }
            return false;
        }
    }
    
    return false;
}

// Update an existing symbol's vreg value
bool ir_gen_update_symbol(IRGenerator* gen, const char* name, VirtualReg vreg) {
    if (!gen || !name) return false;
    
    // Search from end to find most recent definition
    for (size_t i = gen->symbol_table.count; i > 0; i--) {
        if (strcmp(gen->symbol_table.names[i - 1], name) == 0) {
            gen->symbol_table.vregs[i - 1] = vreg;
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Helper Functions
// ============================================================================

uint32_t ir_gen_alloc_label(IRGenerator* gen) {
    return gen->next_label_id++;
}

// Allocate a new scope ID for arena allocations
uint32_t ir_gen_enter_scope(IRGenerator* gen) {
    return ++gen->current_scope_id;
}

// Exit current scope
void ir_gen_exit_scope(IRGenerator* gen) {
    if (gen->current_scope_id > 1) {
        gen->current_scope_id--;
    }
}

// Get current scope ID
uint32_t ir_gen_current_scope(IRGenerator* gen) {
    return gen->current_scope_id;
}

// ============================================================================
// Loop Stack Management (for break/continue)
// ============================================================================

// Push a new loop context onto the stack
static void ir_gen_push_loop(IRGenerator* gen, uint32_t break_target, uint32_t continue_target) {
    if (!gen) return;
    
    // Grow stack if needed
    if (gen->loop_stack.depth >= gen->loop_stack.capacity) {
        size_t new_capacity = gen->loop_stack.capacity == 0 ? 8 : gen->loop_stack.capacity * 2;
        
        uint32_t* new_break = (uint32_t*)realloc(gen->loop_stack.break_targets, 
                                                  new_capacity * sizeof(uint32_t));
        uint32_t* new_continue = (uint32_t*)realloc(gen->loop_stack.continue_targets,
                                                     new_capacity * sizeof(uint32_t));
        
        if (!new_break || !new_continue) return;
        
        gen->loop_stack.break_targets = new_break;
        gen->loop_stack.continue_targets = new_continue;
        gen->loop_stack.capacity = new_capacity;
    }
    
    gen->loop_stack.break_targets[gen->loop_stack.depth] = break_target;
    gen->loop_stack.continue_targets[gen->loop_stack.depth] = continue_target;
    gen->loop_stack.depth++;
}

// Pop the current loop context from the stack
static void ir_gen_pop_loop(IRGenerator* gen) {
    if (!gen || gen->loop_stack.depth == 0) return;
    gen->loop_stack.depth--;
}

// Get the current break target (or 0 if not in a loop)
static uint32_t ir_gen_get_break_target(IRGenerator* gen) {
    if (!gen || gen->loop_stack.depth == 0) return 0;
    return gen->loop_stack.break_targets[gen->loop_stack.depth - 1];
}

// Get the current continue target (or 0 if not in a loop)
static uint32_t ir_gen_get_continue_target(IRGenerator* gen) {
    if (!gen || gen->loop_stack.depth == 0) return 0;
    return gen->loop_stack.continue_targets[gen->loop_stack.depth - 1];
}

// Compute a simple type hash for slab allocator
// Uses FNV-1a hash algorithm
static uint32_t compute_type_hash(const char* type_name) {
    if (!type_name) return 0;
    
    uint32_t hash = 2166136261u;  // FNV offset basis
    while (*type_name) {
        hash ^= (uint8_t)*type_name++;
        hash *= 16777619u;  // FNV prime
    }
    return hash;
}

VirtualReg ir_gen_alloc_temp(IRGenerator* gen, VRegType type) {
    return fcx_ir_alloc_vreg(gen->current_function, type);
}

void ir_gen_set_error(IRGenerator* gen, const char* message) {
    if (!gen) return;
    
    gen->has_error = true;
    if (gen->error_message) {
        free(gen->error_message);
    }
    gen->error_message = strdup(message);
}

const char* ir_gen_get_error(IRGenerator* gen) {
    return gen ? gen->error_message : NULL;
}

// Type mapping helper
VRegType ir_gen_map_type_kind(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return VREG_TYPE_I8;
        case TYPE_I16: return VREG_TYPE_I16;
        case TYPE_I32: return VREG_TYPE_I32;
        case TYPE_I64: return VREG_TYPE_I64;
        case TYPE_I128: return VREG_TYPE_I128;
        case TYPE_I256: return VREG_TYPE_I256;
        case TYPE_I512: return VREG_TYPE_I512;
        case TYPE_I1024: return VREG_TYPE_I1024;
        case TYPE_U8: return VREG_TYPE_U8;
        case TYPE_U16: return VREG_TYPE_U16;
        case TYPE_U32: return VREG_TYPE_U32;
        case TYPE_U64: return VREG_TYPE_U64;
        case TYPE_U128: return VREG_TYPE_U128;
        case TYPE_U256: return VREG_TYPE_U256;
        case TYPE_U512: return VREG_TYPE_U512;
        case TYPE_U1024: return VREG_TYPE_U1024;
        case TYPE_F32: return VREG_TYPE_F32;
        case TYPE_F64: return VREG_TYPE_F64;
        case TYPE_BOOL: return VREG_TYPE_BOOL;
        case TYPE_PTR: return VREG_TYPE_PTR;
        case TYPE_RAWPTR: return VREG_TYPE_RAWPTR;
        case TYPE_BYTEPTR: return VREG_TYPE_BYTEPTR;
        default: return VREG_TYPE_I64;
    }
}

// Type inference for literals
VRegType ir_gen_infer_literal_type(const LiteralValue* literal) {
    switch (literal->type) {
        case LIT_INTEGER: {
            // Default to i64 for all integer literals to avoid assembly issues
            // with byte-sized operations (IMUL, IDIV don't have byte forms)
            // The optimizer can narrow types later if needed
            return VREG_TYPE_I64;
        }
        case LIT_BIGINT: {
            // Determine type based on number of limbs used
            uint8_t num_limbs = literal->value.bigint.num_limbs;
            if (num_limbs <= 2) {
                return VREG_TYPE_I128;  // Up to 128 bits
            } else if (num_limbs <= 4) {
                return VREG_TYPE_I256;  // Up to 256 bits
            } else if (num_limbs <= 8) {
                return VREG_TYPE_I512;  // Up to 512 bits
            } else {
                return VREG_TYPE_I1024; // Up to 1024 bits
            }
        }
        case LIT_FLOAT:
            // Default to f64 for floating point literals
            return VREG_TYPE_F64;
        case LIT_STRING:
            // String literals are pointers to character data
            return VREG_TYPE_PTR;
        case LIT_CHARACTER:
            return VREG_TYPE_I8;
        case LIT_BOOLEAN:
            return VREG_TYPE_BOOL;
        case LIT_RAW_BYTES:
            // Raw bytes are byte pointers
            return VREG_TYPE_BYTEPTR;
        default:
            return VREG_TYPE_I64; // Default fallback
    }
}

// ============================================================================
// Operator Desugaring - Syscall Operations
// ============================================================================

VirtualReg ir_gen_desugar_syscall(IRGenerator* gen, Expr* expr) {
    if (!gen || !expr || expr->type != EXPR_SYSCALL_OP) {
        ir_gen_set_error(gen, "Invalid syscall expression");
        return (VirtualReg){0};
    }
    
    // Desugar: fd $/ buf, len -> FCxIR::Syscall { num: 1, args: [fd, buf, len] }
    // $/ is write syscall (syscall number 1 on Linux x86_64)
    // /$ is read syscall (syscall number 0)
    
    VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
    VirtualReg syscall_num = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
    
    // Determine syscall number based on operator
    int64_t syscall_number = 0;
    switch (expr->data.syscall_op.syscall_type) {
        case SYSCALL_WRITE:  // $/ operator
            syscall_number = 1;  // sys_write
            break;
        case SYSCALL_READ:   // /$ operator
            syscall_number = 0;  // sys_read
            break;
        case SYSCALL_RAW:    // sys% operator
            // syscall_num expression is provided
            if (expr->data.syscall_op.syscall_num) {
                syscall_num = ir_gen_generate_expression(gen, expr->data.syscall_op.syscall_num);
            }
            break;
        default:
            ir_gen_set_error(gen, "Unknown syscall operator");
            return (VirtualReg){0};
    }
    
    if (expr->data.syscall_op.syscall_type != SYSCALL_RAW) {
        fcx_ir_build_const(gen->current_block, syscall_num, syscall_number);
    }
    
    // Generate arguments
    size_t arg_count = expr->data.syscall_op.arg_count;
    VirtualReg* args = NULL;
    
    if (arg_count > 0) {
        args = (VirtualReg*)malloc(arg_count * sizeof(VirtualReg));
        for (size_t i = 0; i < arg_count; i++) {
            args[i] = ir_gen_generate_expression(gen, expr->data.syscall_op.args[i]);
        }
    }
    
    fcx_ir_build_syscall(gen->current_block, result, syscall_num, args, (uint8_t)arg_count);
    
    free(args);
    return result;
}

// ============================================================================
// Operator Desugaring - Atomic Operations
// ============================================================================

VirtualReg ir_gen_desugar_atomic_op(IRGenerator* gen, Expr* expr) {
    if (!gen || !expr || expr->type != EXPR_ATOMIC_OP) {
        ir_gen_set_error(gen, "Invalid atomic expression");
        return (VirtualReg){0};
    }
    
    VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
    
    switch (expr->data.atomic_op.op) {
        case ATOMIC_READ: {
            // ! operator -> atomic load
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[0]);
            fcx_ir_build_atomic_load(gen->current_block, result, ptr);
            break;
        }
        
        case ATOMIC_WRITE: {
            // !! operator -> atomic store
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[0]);
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[1]);
            fcx_ir_build_atomic_store(gen->current_block, ptr, value);
            result = value;
            break;
        }
        
        case ATOMIC_SWAP: {
            // <==> operator -> atomic swap
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[0]);
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[1]);
            fcx_ir_build_atomic_swap(gen->current_block, result, ptr, value);
            break;
        }
        
        case ATOMIC_CAS: {
            // <=> operator (ternary) -> compare-and-swap
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[0]);
            VirtualReg expected = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[1]);
            VirtualReg new_val = ir_gen_generate_expression(gen, expr->data.atomic_op.operands[2]);
            fcx_ir_build_atomic_cas(gen->current_block, result, ptr, expected, new_val);
            break;
        }
        
        default:
            ir_gen_set_error(gen, "Unknown atomic operator");
            return (VirtualReg){0};
    }
    
    return result;
}

// ============================================================================
// Operator Desugaring - Memory Operations
// ============================================================================

VirtualReg ir_gen_desugar_memory_op(IRGenerator* gen, Expr* expr) {
    if (!gen || !expr || expr->type != EXPR_MEMORY_OP) {
        ir_gen_set_error(gen, "Invalid memory expression");
        return (VirtualReg){0};
    }
    
    VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_PTR);
    
    switch (expr->data.memory_op.op) {
        case MEM_ALLOCATE: {
            // mem>size,align -> FCxIR::Alloc { size, align, type: ptr<T> }
            VirtualReg size = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            VirtualReg align = (expr->data.memory_op.operand_count > 1) ?
                ir_gen_generate_expression(gen, expr->data.memory_op.operands[1]) :
                ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            
            if (expr->data.memory_op.operand_count <= 1) {
                fcx_ir_build_const(gen->current_block, align, 8); // default alignment
            }
            
            fcx_ir_build_alloc(gen->current_block, result, size, align);
            break;
        }
        
        case MEM_DEALLOCATE: {
            // >mem ptr -> FCxIR::Dealloc { ptr }
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            // Build dealloc instruction directly
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_DEALLOC;
            instr.operand_count = 1;
            instr.u.unary_op.src = ptr;
            // Add instruction manually since we don't have a builder for dealloc
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            result = ptr;
            break;
        }
        
        case MEM_STACK_ALLOC: {
            // stack>size -> FCxIR::StackAlloc { size }
            VirtualReg size = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            // Build stack alloc instruction directly
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_STACK_ALLOC;
            instr.operand_count = 1;
            instr.u.alloc_op.dest = result;
            instr.u.alloc_op.size = size;
            // Add instruction manually
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            break;
        }
        
        case MEM_ARENA_ALLOC: {
            // arena>scope,align -> FCxIR::ArenaAlloc { scope_id, size, align }
            VirtualReg size = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            VirtualReg align = (expr->data.memory_op.operand_count > 1) ?
                ir_gen_generate_expression(gen, expr->data.memory_op.operands[1]) :
                ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            
            if (expr->data.memory_op.operand_count <= 1) {
                fcx_ir_build_const(gen->current_block, align, 8);
            }
            
            uint32_t scope_id = ir_gen_current_scope(gen);
            fcx_ir_build_arena_alloc(gen->current_block, result, size, align, scope_id);
            break;
        }
        
        case MEM_SLAB_ALLOC: {
            // slab>cache,type -> FCxIR::SlabAlloc { size, type_hash }
            VirtualReg size = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            // Compute type hash from the type annotation if available
            uint32_t type_hash = 0;
            if (expr->data.memory_op.operand_count > 1 && 
                expr->data.memory_op.operands[1]->type == EXPR_IDENTIFIER) {
                type_hash = compute_type_hash(expr->data.memory_op.operands[1]->data.identifier);
            } else {
                // Default type hash based on size
                type_hash = compute_type_hash("unknown");
            }
            fcx_ir_build_slab_alloc(gen->current_block, result, size, type_hash);
            break;
        }
        
        case MEM_MMIO_MAP: {
            // @>addr -> FCxIR::MmioRead { address }
            // For now, treat as constant address
            if (expr->data.memory_op.operands[0]->type == EXPR_LITERAL &&
                expr->data.memory_op.operands[0]->data.literal.type == LIT_INTEGER) {
                uint64_t address = (uint64_t)expr->data.memory_op.operands[0]->data.literal.value.integer;
                fcx_ir_build_mmio_read(gen->current_block, result, address);
            } else {
                ir_gen_set_error(gen, "MMIO address must be a constant");
                return (VirtualReg){0};
            }
            break;
        }
        
        case MEM_STACK_DEALLOC: {
            // >stack ptr - Stack deallocation (no-op in most cases, stack is auto-managed)
            // Just evaluate the operand and return it
            result = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            break;
        }
        
        case MEM_ARENA_RESET: {
            // >arena scope_id - Reset arena for scope
            uint32_t scope_id = ir_gen_current_scope(gen);
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_ARENA_RESET;
            instr.operand_count = 1;
            instr.u.arena_op.scope_id = scope_id;
            // Add instruction
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            break;
        }
        
        case MEM_SLAB_FREE: {
            // >slab ptr, type_hash - Free slab-allocated object
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            uint32_t type_hash = 0;
            if (expr->data.memory_op.operand_count > 1 && 
                expr->data.memory_op.operands[1]->type == EXPR_IDENTIFIER) {
                type_hash = compute_type_hash(expr->data.memory_op.operands[1]->data.identifier);
            }
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_SLAB_FREE;
            instr.operand_count = 2;
            instr.u.slab_op.ptr = ptr;
            instr.u.slab_op.type_hash = type_hash;
            // Add instruction
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            result = ptr;
            break;
        }
        
        case MEM_ALIGN_UP: {
            // align_up> value, alignment -> (value + alignment - 1) & ~(alignment - 1)
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            VirtualReg align = ir_gen_generate_expression(gen, expr->data.memory_op.operands[1]);
            
            // Optimized: use single instruction if alignment is power of 2
            // result = (value + align - 1) & ~(align - 1)
            VirtualReg one = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, one, 1);
            
            VirtualReg align_minus_1 = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_SUB, align_minus_1, align, one);
            
            VirtualReg value_plus = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_ADD, value_plus, value, align_minus_1);
            
            VirtualReg mask = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_unary_op(gen->current_block, FCXIR_NOT, mask, align_minus_1);
            
            fcx_ir_build_binary_op(gen->current_block, FCXIR_AND, result, value_plus, mask);
            break;
        }
        
        case MEM_ALIGN_DOWN: {
            // align_down> value, alignment -> value & ~(alignment - 1)
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            VirtualReg align = ir_gen_generate_expression(gen, expr->data.memory_op.operands[1]);
            
            VirtualReg one = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, one, 1);
            
            VirtualReg align_minus_1 = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_SUB, align_minus_1, align, one);
            
            VirtualReg mask = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_unary_op(gen->current_block, FCXIR_NOT, mask, align_minus_1);
            
            fcx_ir_build_binary_op(gen->current_block, FCXIR_AND, result, value, mask);
            break;
        }
        
        case MEM_IS_ALIGNED: {
            // is_aligned?> value, alignment -> (value & (alignment - 1)) == 0
            result.type = VREG_TYPE_BOOL;
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            VirtualReg align = ir_gen_generate_expression(gen, expr->data.memory_op.operands[1]);
            
            VirtualReg one = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, one, 1);
            
            VirtualReg align_minus_1 = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_SUB, align_minus_1, align, one);
            
            VirtualReg masked = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_AND, masked, value, align_minus_1);
            
            VirtualReg zero = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, zero, 0);
            
            fcx_ir_build_binary_op(gen->current_block, FCXIR_CMP_EQ, result, masked, zero);
            break;
        }
        
        case MEM_PREFETCH: {
            // prefetch> ptr - Prefetch memory for reading
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_PREFETCH;
            instr.operand_count = 1;
            instr.u.unary_op.src = ptr;
            instr.u.unary_op.dest = result;
            // Add instruction
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            result = ptr;
            break;
        }
        
        case MEM_PREFETCH_WRITE: {
            // prefetch_write> ptr - Prefetch memory for writing
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.memory_op.operands[0]);
            FcxIRInstruction instr = {0};
            instr.opcode = FCXIR_PREFETCH_WRITE;
            instr.operand_count = 1;
            instr.u.unary_op.src = ptr;
            instr.u.unary_op.dest = result;
            // Add instruction
            if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                    gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                if (new_instructions) {
                    gen->current_block->instructions = new_instructions;
                    gen->current_block->instruction_capacity = new_capacity;
                }
            }
            gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
            result = ptr;
            break;
        }
        
        case MEM_MMIO_UNMAP:
        case MEM_LAYOUT_ACCESS:
            // Not yet implemented
            ir_gen_set_error(gen, "Memory operation not yet implemented");
            return (VirtualReg){0};
        
        default:
            ir_gen_set_error(gen, "Unknown memory operator");
            return (VirtualReg){0};
    }
    
    return result;
}

// ============================================================================
// Operator Desugaring - Binary Operations
// ============================================================================

VirtualReg ir_gen_desugar_binary_op(IRGenerator* gen, Expr* expr) {
    if (!gen || !expr || expr->type != EXPR_BINARY) {
        ir_gen_set_error(gen, "Invalid binary expression");
        return (VirtualReg){0};
    }
    
    // Check if this is a syscall operator
    if (expr->data.binary.op == OP_WRITE_SYSCALL || 
        expr->data.binary.op == OP_READ_SYSCALL) {
        ir_gen_set_error(gen, "Syscall operator requires 3 arguments: fd $/ buffer, length");
        return (VirtualReg){0};
    }
    
    // Handle comma operator specially for syscall completion
    if (expr->data.binary.op == TOK_COMMA) {
        // Check if left side is a syscall operator
        if (expr->data.binary.left->type == EXPR_BINARY &&
            (expr->data.binary.left->data.binary.op == OP_WRITE_SYSCALL ||
             expr->data.binary.left->data.binary.op == OP_READ_SYSCALL)) {
            
            Expr* syscall_expr = expr->data.binary.left;
            
            VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            VirtualReg syscall_num = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            
            int64_t syscall_number = (syscall_expr->data.binary.op == OP_WRITE_SYSCALL) ? 1 : 0;
            fcx_ir_build_const(gen->current_block, syscall_num, syscall_number);
            
            VirtualReg fd = ir_gen_generate_expression(gen, syscall_expr->data.binary.left);
            VirtualReg buffer = ir_gen_generate_expression(gen, syscall_expr->data.binary.right);
            VirtualReg length = ir_gen_generate_expression(gen, expr->data.binary.right);
            
            VirtualReg args[3] = {fd, buffer, length};
            fcx_ir_build_syscall(gen->current_block, result, syscall_num, args, 3);
            
            return result;
        }
        
        // Normal comma operator - evaluate left, then right, return right
        (void)ir_gen_generate_expression(gen, expr->data.binary.left);
        return ir_gen_generate_expression(gen, expr->data.binary.right);
    }
    
    VirtualReg left = ir_gen_generate_expression(gen, expr->data.binary.left);
    VirtualReg right = ir_gen_generate_expression(gen, expr->data.binary.right);
    VirtualReg result = ir_gen_alloc_temp(gen, left.type);
    
    // Map token kinds to FCx IR opcodes
    FcxIROpcode opcode;
    
    switch (expr->data.binary.op) {
        // Basic arithmetic (using assignment operators and saturating ops)
        case OP_ADD_ASSIGN:
        case OP_SAT_ADD:
        case OP_WRAP_ADD:
        case OP_CHECKED_ADD:
            opcode = FCXIR_ADD;
            break;
        case OP_SUB_ASSIGN:
        case OP_SAT_SUB:
        case OP_WRAP_SUB:
        case OP_CHECKED_SUB:
            opcode = FCXIR_SUB;
            break;
        case OP_MUL_ASSIGN:
        case OP_SAT_MUL:
        case OP_WRAP_MUL:
        case OP_CHECKED_MUL:
            opcode = FCXIR_MUL;
            break;
        case OP_DIV:
        case OP_INT_DIV:
            opcode = FCXIR_DIV;
            break;
        case OP_MOD_DIVISOR:
            opcode = FCXIR_MOD;
            break;
            
        // Bitwise operations
        case OP_BITFIELD_EXTRACT:
        case OP_SHIFT_MASK:
            opcode = FCXIR_AND;
            break;
        case OP_PUSH_INTO:
        case OP_IMPLIES:
            opcode = FCXIR_OR;
            break;
        case OP_BITWISE_ROTATE_XOR:
            opcode = FCXIR_XOR;
            break;
            
        // Shift and rotate
        case OP_LSHIFT:
        case OP_LSHIFT_ASSIGN:
            opcode = FCXIR_LSHIFT;
            break;
        case OP_RSHIFT:
            opcode = FCXIR_RSHIFT;
            break;
        case OP_LOGICAL_RSHIFT:
            opcode = FCXIR_LOGICAL_RSHIFT;
            break;
        case OP_ROTATE_LEFT:
            opcode = FCXIR_ROTATE_LEFT;
            break;
        case OP_ROTATE_RIGHT:
            opcode = FCXIR_ROTATE_RIGHT;
            break;
            
        // Comparison
        case OP_EQ:
        case OP_EQ_MAYBE:
            opcode = FCXIR_CMP_EQ;
            break;
        case OP_NE:
        case OP_NE_MAYBE:
        case OP_PATTERN_NE:
            opcode = FCXIR_CMP_NE;
            break;
        case OP_LT:
        case OP_LE_MAYBE:
        case OP_LT_DOUBLE:
            opcode = FCXIR_CMP_LT;
            break;
        case OP_LE:
        case OP_LE_OR_FLAG:
            opcode = FCXIR_CMP_LE;
            break;
        case OP_GT:
        case OP_GE_MAYBE:
        case OP_GT_DOUBLE:
            opcode = FCXIR_CMP_GT;
            break;
        case OP_GE:
            opcode = FCXIR_CMP_GE;
            break;
            
        // Pointer arithmetic
        case OP_SLICE_START:
            opcode = FCXIR_PTR_ADD;
            break;
        case OP_SLICE_END:
            opcode = FCXIR_PTR_SUB;
            break;
            
        // Min/Max operations (map to comparison + select)
        case OP_MIN:
        case OP_MAX:
        case OP_CLAMP:
            // For now, treat as comparison - proper implementation would need conditional
            opcode = FCXIR_CMP_LT;
            break;
            
        default: {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unsupported binary operator: %d", expr->data.binary.op);
            ir_gen_set_error(gen, error_msg);
            return (VirtualReg){0};
        }
    }
    
    fcx_ir_build_binary_op(gen->current_block, opcode, result, left, right);
    return result;
}

// ============================================================================
// Expression Generation
// ============================================================================

VirtualReg ir_gen_generate_expression(IRGenerator* gen, Expr* expr) {
    if (!gen || !expr) {
        ir_gen_set_error(gen, "Null expression");
        return (VirtualReg){0};
    }
    
    switch (expr->type) {
        case EXPR_LITERAL: {
            // Infer the appropriate type for this literal
            VRegType inferred_type = ir_gen_infer_literal_type(&expr->data.literal);
            VirtualReg result = ir_gen_alloc_temp(gen, inferred_type);
            
            switch (expr->data.literal.type) {
                case LIT_INTEGER:
                    fcx_ir_build_const(gen->current_block, result, expr->data.literal.value.integer);
                    break;
                    
                case LIT_FLOAT: {
                    // For float literals, we need to store them as data and load them
                    // For now, convert to integer representation (bit pattern)
                    union {
                        double d;
                        int64_t i;
                    } converter;
                    converter.d = expr->data.literal.value.floating;
                    fcx_ir_build_const(gen->current_block, result, converter.i);
                    break;
                }
                
                case LIT_STRING: {
                    // String literals need to be stored in data section
                    // Add string to module's string literal table
                    const char* str = expr->data.literal.value.string;
                    size_t len = strlen(str);
                    
                    // Add string to module and get its ID
                    uint32_t string_id = fcx_ir_module_add_string(gen->module, str, len + 1);
                    
                    // Store the string ID - this will be resolved to an address during lowering
                    // The string ID is encoded as a negative number to distinguish from regular constants
                    // During assembly emission, we'll emit a .string directive and reference it
                    fcx_ir_build_const(gen->current_block, result, -(int64_t)string_id);
                    
                    // Mark this vreg as holding a string reference
                    result.flags |= 0x8000; // String reference flag
                    break;
                }
                
                case LIT_CHARACTER:
                    fcx_ir_build_const(gen->current_block, result, (int64_t)expr->data.literal.value.character);
                    break;
                    
                case LIT_BOOLEAN:
                    fcx_ir_build_const(gen->current_block, result, expr->data.literal.value.boolean ? 1 : 0);
                    break;
                    
                case LIT_RAW_BYTES: {
                    // Raw bytes need to be stored in data section similar to strings
                    // For now, treat raw bytes as a string literal with the byte data
                    const uint8_t* bytes = expr->data.literal.value.raw_bytes.data;
                    size_t len = expr->data.literal.value.raw_bytes.length;
                    
                    // Add raw bytes to module's string table (reusing string infrastructure)
                    uint32_t bytes_id = fcx_ir_module_add_string(gen->module, (const char*)bytes, len);
                    
                    // Store the bytes ID as a negative number (same as strings)
                    fcx_ir_build_const(gen->current_block, result, -(int64_t)bytes_id);
                    
                    // Mark this vreg as holding a raw bytes reference
                    result.flags |= 0x4000; // Raw bytes reference flag
                    break;
                }
                
                case LIT_BIGINT: {
                    // Bigint literal - use limb-based constant builder
                    fcx_ir_build_const_bigint(gen->current_block, result, 
                                              expr->data.literal.value.bigint.limbs,
                                              expr->data.literal.value.bigint.num_limbs);
                    break;
                }
                
                default:
                    ir_gen_set_error(gen, "Unsupported literal type");
                    return (VirtualReg){0};
            }
            return result;
        }
        
        case EXPR_IDENTIFIER: {
            const char* name = expr->data.identifier;
            
            // Check if this is a global variable
            uint32_t global_index;
            if (ir_gen_is_global_symbol(gen, name, &global_index)) {
                // Global variable - generate a load from the global
                VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                
                // Use FCXIR_LOAD_GLOBAL opcode
                FcxIRInstruction instr = {0};
                instr.opcode = FCXIR_LOAD_GLOBAL;
                instr.operand_count = 1;
                instr.u.global_op.vreg = result;
                instr.u.global_op.global_index = global_index;
                
                // Add instruction to current block
                if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                    uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                    FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                        gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                    if (new_instructions) {
                        gen->current_block->instructions = new_instructions;
                        gen->current_block->instruction_capacity = new_capacity;
                    }
                }
                gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
                
                return result;
            }
            
            // Local variable - look up in symbol table
            bool found;
            VirtualReg vreg = ir_gen_lookup_symbol(gen, name, &found);
            if (!found) {
                char error[256];
                snprintf(error, sizeof(error), "Undefined variable: %s", name);
                ir_gen_set_error(gen, error);
                return (VirtualReg){0};
            }
            return vreg;
        }
        
        case EXPR_BINARY:
            return ir_gen_desugar_binary_op(gen, expr);
            
        case EXPR_UNARY: {
            // Handle print> operator FIRST before evaluating operand
            // (we need to check if operand is a literal before evaluation)
            TokenKind unary_op = expr->data.unary.op;
            if (unary_op == OP_STACK_ALLOC || unary_op == 91 ||
                unary_op == OP_PRINT_COMPACT || unary_op == OP_FORMAT_PRINT) {
                    // print> operator - generate function call to _fcx_println (with newline)
                    
                    VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                    
                    // Check if the operand expression is a string literal
                    Expr* operand_expr = expr->data.unary.operand;
                    if (operand_expr->type == EXPR_LITERAL && 
                        operand_expr->data.literal.type == LIT_STRING) {
                        // String literal print - call _fcx_println(str)
                        const char* str = operand_expr->data.literal.value.string;
                        size_t len = strlen(str);
                        
                        // Add the string to the module
                        uint32_t string_id = fcx_ir_module_add_string(gen->module, str, len + 1);
                        
                        // Create a vreg for the string pointer
                        VirtualReg str_ptr = ir_gen_alloc_temp(gen, VREG_TYPE_PTR);
                        fcx_ir_build_const(gen->current_block, str_ptr, -(int64_t)string_id);
                        
                        // Generate function call: _fcx_println(str_ptr)
                        VirtualReg args[1] = { str_ptr };
                        fcx_ir_build_call(gen->current_block, result, "_fcx_println", args, 1);
                        
                    } else if (operand_expr->type == EXPR_LITERAL && 
                               operand_expr->data.literal.type == LIT_INTEGER) {
                        // Integer literal print - call _fcx_println_int(value)
                        int64_t value = operand_expr->data.literal.value.integer;
                        
                        VirtualReg int_val = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                        fcx_ir_build_const(gen->current_block, int_val, value);
                        
                        // Generate function call: _fcx_println_int(value)
                        VirtualReg args[1] = { int_val };
                        fcx_ir_build_call(gen->current_block, result, "_fcx_println_int", args, 1);
                        
                    } else if (operand_expr->type == EXPR_UNARY && 
                               operand_expr->data.unary.op == OP_SUB_ASSIGN &&
                               operand_expr->data.unary.operand->type == EXPR_LITERAL &&
                               operand_expr->data.unary.operand->data.literal.type == LIT_INTEGER) {
                        // Negated integer literal: -123 is parsed as -(123)
                        int64_t value = -operand_expr->data.unary.operand->data.literal.value.integer;
                        
                        VirtualReg int_val = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                        fcx_ir_build_const(gen->current_block, int_val, value);
                        
                        // Generate function call: _fcx_println_int(value)
                        VirtualReg args[1] = { int_val };
                        fcx_ir_build_call(gen->current_block, result, "_fcx_println_int", args, 1);
                        
                    } else {
                        // Variable or expression - evaluate it first, then determine type
                        VirtualReg operand = ir_gen_generate_expression(gen, operand_expr);
                        
                        // Check the operand type and call the appropriate print function
                        VirtualReg args[1] = { operand };
                        const char* print_func;
                        
                        switch (operand.type) {
                            case VREG_TYPE_I128:
                                print_func = "_fcx_println_i128";
                                break;
                            case VREG_TYPE_U128:
                                print_func = "_fcx_println_u128";
                                break;
                            case VREG_TYPE_I256:
                                print_func = "_fcx_println_i256";
                                break;
                            case VREG_TYPE_U256:
                                print_func = "_fcx_println_u256";
                                break;
                            case VREG_TYPE_I512:
                                print_func = "_fcx_println_i512";
                                break;
                            case VREG_TYPE_U512:
                                print_func = "_fcx_println_u512";
                                break;
                            case VREG_TYPE_I1024:
                                print_func = "_fcx_println_i1024";
                                break;
                            case VREG_TYPE_U1024:
                                print_func = "_fcx_println_u1024";
                                break;
                            case VREG_TYPE_F32:
                                print_func = "_fcx_println_f32";
                                break;
                            case VREG_TYPE_F64:
                                print_func = "_fcx_println_f64";
                                break;
                            case VREG_TYPE_BOOL:
                                print_func = "_fcx_println_bool";
                                break;
                            case VREG_TYPE_PTR:
                            case VREG_TYPE_RAWPTR:
                            case VREG_TYPE_BYTEPTR:
                                print_func = "_fcx_println_ptr";
                                break;
                            case VREG_TYPE_I8:
                                print_func = "_fcx_println_char";
                                break;
                            case VREG_TYPE_U8:
                                print_func = "_fcx_println_u8";
                                break;
                            case VREG_TYPE_I16:
                            case VREG_TYPE_U16:
                            case VREG_TYPE_I32:
                            case VREG_TYPE_U32:
                            case VREG_TYPE_I64:
                            case VREG_TYPE_U64:
                            default:
                                print_func = "_fcx_println_int";
                                break;
                        }
                        
                        fcx_ir_build_call(gen->current_block, result, print_func, args, 1);
                    }
                    
                    return result;
            }
            
            // For all other unary operators, evaluate operand first
            VirtualReg operand = ir_gen_generate_expression(gen, expr->data.unary.operand);
            VirtualReg result = ir_gen_alloc_temp(gen, operand.type);
            
            FcxIROpcode opcode;
            switch (unary_op) {
                case OP_SUB_ASSIGN:
                    // Unary minus (negation)
                    opcode = FCXIR_NEG;
                    break;
                case OP_ATOMIC_XOR:
                    // Bitwise NOT
                    opcode = FCXIR_NOT;
                    break;
                case OP_ATOMIC_READ:
                    // Atomic read (!) - generate atomic load
                    fcx_ir_build_atomic_load(gen->current_block, result, operand);
                    return result;
                case OP_ABS:
                    // Absolute value - for now, use NEG (proper impl needs conditional)
                    opcode = FCXIR_NEG;
                    break;
                case OP_POPCOUNT:
                case OP_CLZ:
                case OP_CTZ:
                case OP_BYTESWAP:
                case OP_SQRT:
                case OP_RSQRT:
                case OP_FLOOR:
                case OP_CEIL:
                case OP_TRUNC:
                case OP_ROUND: {
                    // These are intrinsic operations - generate call to runtime
                    VirtualReg call_result = ir_gen_alloc_temp(gen, operand.type);
                    const char* func_name = "_fcx_intrinsic";
                    fcx_ir_build_call(gen->current_block, call_result, func_name, &operand, 1);
                    return call_result;
                }
                default:
                    ir_gen_set_error(gen, "Unsupported unary operator");
                    return (VirtualReg){0};
            }
            
            fcx_ir_build_unary_op(gen->current_block, opcode, result, operand);
            return result;
        }
        
        case EXPR_TERNARY: {
            // Ternary expression: condition ? then_expr : else_expr
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            VirtualReg cond = ir_gen_generate_expression(gen, expr->data.ternary.first);
            
            // Refresh current_block pointer
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* then_block = fcx_ir_block_create(gen->current_function, "ternary.then");
            uint32_t then_label = then_block->id;
            FcxIRBasicBlock* else_block = fcx_ir_block_create(gen->current_function, "ternary.else");
            uint32_t else_label = else_block->id;
            FcxIRBasicBlock* merge_block = fcx_ir_block_create(gen->current_function, "ternary.merge");
            uint32_t merge_label = merge_block->id;
            
            // Refresh current_block pointer and emit branch
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            fcx_ir_build_branch(gen->current_block, cond, then_label, else_label);
            
            // Then block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, then_label);
            VirtualReg then_val = ir_gen_generate_expression(gen, expr->data.ternary.second);
            fcx_ir_build_jump(gen->current_block, merge_label);
            
            // Else block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, else_label);
            VirtualReg else_val = ir_gen_generate_expression(gen, expr->data.ternary.third);
            (void)else_val;  // Suppress unused warning - used in PHI node in full SSA
            fcx_ir_build_jump(gen->current_block, merge_label);
            
            // Merge block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, merge_label);
            
            // For proper SSA, we need a PHI node. Since we don't have full PHI support,
            // we use a stack slot to hold the result from both branches.
            VirtualReg result = ir_gen_alloc_temp(gen, then_val.type);
            fcx_ir_build_mov(gen->current_block, result, then_val);
            
            return result;
        }
        
        case EXPR_MULTI_ASSIGN: {
            // Multiple assignment: a:b:=0:1
            // Generate values first, then assign
            size_t count = expr->data.multi_assign.count;
            VirtualReg* values = (VirtualReg*)malloc(count * sizeof(VirtualReg));
            
            // Generate all values
            for (size_t i = 0; i < count; i++) {
                values[i] = ir_gen_generate_expression(gen, expr->data.multi_assign.values[i]);
            }
            
            // Assign to targets
            for (size_t i = 0; i < count; i++) {
                if (expr->data.multi_assign.targets[i]->type == EXPR_IDENTIFIER) {
                    const char* name = expr->data.multi_assign.targets[i]->data.identifier;
                    ir_gen_add_symbol(gen, name, values[i]);
                }
            }
            
            VirtualReg result = values[count - 1];
            free(values);
            return result;
        }
        
        case EXPR_FUNCTION_DEF: {
            // Function definition expression (for lambdas/closures)
            // For now, just return a placeholder
            ir_gen_set_error(gen, "Function definition expressions not yet supported");
            return (VirtualReg){0};
        }
        
        case EXPR_ASSIGNMENT: {
            VirtualReg value = ir_gen_generate_expression(gen, expr->data.assignment.value);
            
            if (expr->data.assignment.target->type == EXPR_IDENTIFIER) {
                const char* name = expr->data.assignment.target->data.identifier;
                
                // Check if this is a global variable
                uint32_t global_index;
                if (ir_gen_is_global_symbol(gen, name, &global_index)) {
                    // Global variable - generate a store to the global
                    FcxIRInstruction instr = {0};
                    instr.opcode = FCXIR_STORE_GLOBAL;
                    instr.operand_count = 1;
                    instr.u.global_op.vreg = value;
                    instr.u.global_op.global_index = global_index;
                    
                    // Add instruction to current block
                    if (gen->current_block->instruction_count >= gen->current_block->instruction_capacity) {
                        uint32_t new_capacity = gen->current_block->instruction_capacity == 0 ? 16 : gen->current_block->instruction_capacity * 2;
                        FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
                            gen->current_block->instructions, new_capacity * sizeof(FcxIRInstruction));
                        if (new_instructions) {
                            gen->current_block->instructions = new_instructions;
                            gen->current_block->instruction_capacity = new_capacity;
                        }
                    }
                    gen->current_block->instructions[gen->current_block->instruction_count++] = instr;
                    
                    return value;
                }
                
                // Local variable
                bool found;
                VirtualReg existing = ir_gen_lookup_symbol(gen, name, &found);
                
                if (!found) {
                    // New variable - add to symbol table
                    ir_gen_add_symbol(gen, name, value);
                } else {
                    // Update existing variable - generate MOV to copy new value to original vreg
                    // This is important for loops where the variable needs to be updated in place
                    fcx_ir_build_mov(gen->current_block, existing, value);
                    // Don't update symbol table - keep pointing to original vreg
                    // The MOV instruction will update the value at runtime
                }
            } else if (expr->data.assignment.target->type == EXPR_DEREF) {
                // Pointer dereference assignment: @ptr := value
                // Generate store instruction
                VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.assignment.target->data.deref.pointer);
                fcx_ir_build_store(gen->current_block, ptr, value, 0);
            } else if (expr->data.assignment.target->type == EXPR_INDEX) {
                // Array index assignment: arr[i] := value
                // Compute address and store
                Expr* index_expr = expr->data.assignment.target;
                VirtualReg base = ir_gen_generate_expression(gen, index_expr->data.index.base);
                VirtualReg index = ir_gen_generate_expression(gen, index_expr->data.index.index);
                
                // Get element size (default to 8 bytes)
                int64_t element_size = index_expr->data.index.element_size;
                if (element_size == 0) {
                    element_size = 8;
                }
                
                // Scale index by element size
                VirtualReg scaled_index = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                VirtualReg scale = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_const(gen->current_block, scale, element_size);
                fcx_ir_build_binary_op(gen->current_block, FCXIR_MUL, scaled_index, index, scale);
                
                // Add to base pointer
                VirtualReg addr = ir_gen_alloc_temp(gen, VREG_TYPE_PTR);
                fcx_ir_build_binary_op(gen->current_block, FCXIR_ADD, addr, base, scaled_index);
                
                // Store value
                fcx_ir_build_store(gen->current_block, addr, value, 0);
            }
            
            return value;
        }
        
        case EXPR_CALL: {
            // Generate function call
            const char* func_name = NULL;
            if (expr->data.call.function->type == EXPR_IDENTIFIER) {
                func_name = expr->data.call.function->data.identifier;
            } else {
                ir_gen_set_error(gen, "Function call must use identifier");
                return (VirtualReg){0};
            }
            
            VirtualReg* args = NULL;
            if (expr->data.call.arg_count > 0) {
                args = (VirtualReg*)malloc(expr->data.call.arg_count * sizeof(VirtualReg));
                for (size_t i = 0; i < expr->data.call.arg_count; i++) {
                    args[i] = ir_gen_generate_expression(gen, expr->data.call.args[i]);
                }
            }
            
            VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_call(gen->current_block, result, func_name, args, (uint8_t)expr->data.call.arg_count);
            
            free(args);
            return result;
        }
        
        case EXPR_CONDITIONAL: {
            // Transform: ?(n<=0) -> ret 0 into proper conditional branches
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            VirtualReg cond = ir_gen_generate_expression(gen, expr->data.conditional.condition);
            
            // Refresh current_block pointer
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* then_block = fcx_ir_block_create(gen->current_function, "then");
            uint32_t then_label = then_block->id;
            FcxIRBasicBlock* else_block = fcx_ir_block_create(gen->current_function, "else");
            uint32_t else_label = else_block->id;
            FcxIRBasicBlock* merge_block = fcx_ir_block_create(gen->current_function, "merge");
            uint32_t merge_label = merge_block->id;
            
            // Refresh current_block pointer and emit branch
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            fcx_ir_build_branch(gen->current_block, cond, then_label, else_label);
            
            // Then block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, then_label);
            VirtualReg then_val = ir_gen_generate_expression(gen, expr->data.conditional.then_expr);
            fcx_ir_build_jump(gen->current_block, merge_label);
            
            // Else block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, else_label);
            VirtualReg else_val = ir_gen_generate_expression(gen, expr->data.conditional.else_expr);
            (void)else_val;  // Suppress unused warning - used in PHI node in full SSA
            fcx_ir_build_jump(gen->current_block, merge_label);
            
            // Merge block with PHI
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, merge_label);
            
            // For proper SSA, we need a PHI node. Using simplified approach:
            VirtualReg result = ir_gen_alloc_temp(gen, then_val.type);
            fcx_ir_build_mov(gen->current_block, result, then_val);
            
            return result;
        }
        
        case EXPR_SYSCALL_OP:
            return ir_gen_desugar_syscall(gen, expr);
            
        case EXPR_ATOMIC_OP:
            return ir_gen_desugar_atomic_op(gen, expr);
            
        case EXPR_MEMORY_OP:
            return ir_gen_desugar_memory_op(gen, expr);
            
        case EXPR_INDEX: {
            // Array/pointer indexing: ptr[index]
            // Compute: base + (index * element_size)
            VirtualReg base = ir_gen_generate_expression(gen, expr->data.index.base);
            VirtualReg index = ir_gen_generate_expression(gen, expr->data.index.index);
            
            // Get element size (default to 8 bytes for 64-bit values)
            int64_t element_size = expr->data.index.element_size;
            if (element_size == 0) {
                element_size = 8; // Default to 8 bytes
            }
            
            // Scale index by element size
            VirtualReg scaled_index = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            VirtualReg scale = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, scale, element_size);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_MUL, scaled_index, index, scale);
            
            // Add to base pointer
            VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_PTR);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_ADD, result, base, scaled_index);
            
            // Load from computed address
            VirtualReg loaded = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_load(gen->current_block, loaded, result, 0);
            
            return loaded;
        }
        
        case EXPR_DEREF: {
            // Pointer dereference: @ptr
            VirtualReg ptr = ir_gen_generate_expression(gen, expr->data.deref.pointer);
            
            if (expr->data.deref.is_write && expr->data.deref.value) {
                // Store operation
                VirtualReg value = ir_gen_generate_expression(gen, expr->data.deref.value);
                fcx_ir_build_store(gen->current_block, ptr, value, 0);
                return value;
            } else {
                // Load operation
                VirtualReg result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_load(gen->current_block, result, ptr, 0);
                return result;
            }
        }
        
        case EXPR_INLINE_ASM: {
            // Inline assembly: asm% "template" : outputs : inputs : clobbers
            // Generate input values
            VirtualReg* inputs = NULL;
            if (expr->data.inline_asm.input_count > 0) {
                inputs = malloc(expr->data.inline_asm.input_count * sizeof(VirtualReg));
                for (size_t i = 0; i < expr->data.inline_asm.input_count; i++) {
                    if (expr->data.inline_asm.input_exprs[i]) {
                        inputs[i] = ir_gen_generate_expression(gen, expr->data.inline_asm.input_exprs[i]);
                    } else {
                        inputs[i] = (VirtualReg){0};
                    }
                }
            }
            
            // Allocate output registers
            VirtualReg* outputs = NULL;
            VirtualReg result = {0};
            if (expr->data.inline_asm.output_count > 0) {
                outputs = malloc(expr->data.inline_asm.output_count * sizeof(VirtualReg));
                for (size_t i = 0; i < expr->data.inline_asm.output_count; i++) {
                    outputs[i] = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                }
                result = outputs[0]; // First output is the result
            } else {
                result = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            }
            
            // Build inline asm instruction
            fcx_ir_build_inline_asm(gen->current_block,
                                    expr->data.inline_asm.asm_template,
                                    (const char**)expr->data.inline_asm.output_constraints,
                                    outputs,
                                    (uint8_t)expr->data.inline_asm.output_count,
                                    (const char**)expr->data.inline_asm.input_constraints,
                                    inputs,
                                    (uint8_t)expr->data.inline_asm.input_count,
                                    (const char**)expr->data.inline_asm.clobbers,
                                    (uint8_t)expr->data.inline_asm.clobber_count,
                                    expr->data.inline_asm.is_volatile);
            
            // Store outputs back to their target variables
            for (size_t i = 0; i < expr->data.inline_asm.output_count; i++) {
                if (expr->data.inline_asm.output_exprs && expr->data.inline_asm.output_exprs[i]) {
                    Expr* out_expr = expr->data.inline_asm.output_exprs[i];
                    if (out_expr->type == EXPR_IDENTIFIER) {
                        // Look up the variable and copy the output to it
                        const char* var_name = out_expr->data.identifier;
                        bool found;
                        VirtualReg var_vreg = ir_gen_lookup_symbol(gen, var_name, &found);
                        if (found && var_vreg.id != 0) {
                            // Use MOV to copy the asm output to the variable
                            fcx_ir_build_mov(gen->current_block, var_vreg, outputs[i]);
                        }
                    }
                }
            }
            
            return result;
        }
            
        default:
            ir_gen_set_error(gen, "Unsupported expression type");
            return (VirtualReg){0};
    }
}

// ============================================================================
// Loop Generation
// ============================================================================

bool ir_gen_generate_loop(IRGenerator* gen, Stmt* stmt) {
    if (!gen || !stmt || stmt->type != STMT_LOOP) {
        ir_gen_set_error(gen, "Invalid loop statement");
        return false;
    }
    
    switch (stmt->data.loop.loop_type) {
        case LOOP_TRADITIONAL: {
            // Traditional loop: loop { body }
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* header_block = fcx_ir_block_create(gen->current_function, "loop.header");
            uint32_t header_id = header_block->id;
            FcxIRBasicBlock* body_block = fcx_ir_block_create(gen->current_function, "loop.body");
            uint32_t body_id = body_block->id;
            FcxIRBasicBlock* exit_block = fcx_ir_block_create(gen->current_function, "loop.exit");
            uint32_t exit_id = exit_block->id;
            
            // Refresh current_block pointer and jump to header
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            fcx_ir_build_jump(gen->current_block, header_id);
            
            // Header block (condition check if present)
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, header_id);
            
            if (stmt->data.loop.condition) {
                VirtualReg cond = ir_gen_generate_expression(gen, stmt->data.loop.condition);
                fcx_ir_build_branch(gen->current_block, cond, body_id, exit_id);
            } else {
                // Infinite loop
                fcx_ir_build_jump(gen->current_block, body_id);
            }
            
            // Body block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, body_id);
            
            // Push loop context: break goes to exit, continue goes to header
            ir_gen_push_loop(gen, exit_id, header_id);
            
            for (size_t i = 0; i < stmt->data.loop.body.count; i++) {
                if (!ir_gen_generate_statement(gen, stmt->data.loop.body.statements[i])) {
                    ir_gen_pop_loop(gen);
                    return false;
                }
            }
            
            ir_gen_pop_loop(gen);
            
            // Jump back to header (only if block doesn't have a terminator)
            if (gen->current_block->instruction_count == 0 ||
                (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN)) {
                fcx_ir_build_jump(gen->current_block, header_id);
            }
            
            // Exit block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, exit_id);
            break;
        }
        
        case LOOP_WHILE: {
            // While loop: while condition { body }
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* preheader_block = fcx_ir_block_create(gen->current_function, "while.preheader");
            uint32_t preheader_id = preheader_block->id;
            FcxIRBasicBlock* body_block = fcx_ir_block_create(gen->current_function, "while.body");
            uint32_t body_id = body_block->id;
            FcxIRBasicBlock* exit_block = fcx_ir_block_create(gen->current_function, "while.exit");
            uint32_t exit_id = exit_block->id;
            
            // Refresh current_block pointer and jump to preheader
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            fcx_ir_build_jump(gen->current_block, preheader_id);
            
            // Preheader block - check condition before first iteration
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, preheader_id);
            
            if (stmt->data.loop.condition) {
                VirtualReg cond = ir_gen_generate_expression(gen, stmt->data.loop.condition);
                fcx_ir_build_branch(gen->current_block, cond, body_id, exit_id);
            } else {
                ir_gen_set_error(gen, "While loop requires condition");
                return false;
            }
            
            // Body block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, body_id);
            
            // Push loop context: break goes to exit, continue goes to preheader (re-check condition)
            ir_gen_push_loop(gen, exit_id, preheader_id);
            
            for (size_t i = 0; i < stmt->data.loop.body.count; i++) {
                if (!ir_gen_generate_statement(gen, stmt->data.loop.body.statements[i])) {
                    ir_gen_pop_loop(gen);
                    return false;
                }
            }
            
            ir_gen_pop_loop(gen);
            
            // At end of body, re-evaluate condition with updated variables (only if no terminator)
            if (gen->current_block->instruction_count == 0 ||
                (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN)) {
                VirtualReg cond2 = ir_gen_generate_expression(gen, stmt->data.loop.condition);
                fcx_ir_build_branch(gen->current_block, cond2, body_id, exit_id);
            }
            
            // Exit block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, exit_id);
            break;
        }
        
        case LOOP_COUNT: {
            // Count loop: (expr) << n: body
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* header_block = fcx_ir_block_create(gen->current_function, "count.header");
            uint32_t header_id = header_block->id;
            FcxIRBasicBlock* body_block = fcx_ir_block_create(gen->current_function, "count.body");
            uint32_t body_id = body_block->id;
            FcxIRBasicBlock* exit_block = fcx_ir_block_create(gen->current_function, "count.exit");
            uint32_t exit_id = exit_block->id;
            
            // Refresh current_block pointer
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            // Initialize counter
            VirtualReg counter = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, counter, 0);
            
            // Get loop count
            VirtualReg count = ir_gen_generate_expression(gen, stmt->data.loop.condition);
            
            // Jump to header
            fcx_ir_build_jump(gen->current_block, header_id);
            
            // Header block (counter < count)
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, header_id);
            
            VirtualReg cmp_result = ir_gen_alloc_temp(gen, VREG_TYPE_BOOL);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_CMP_LT, cmp_result, counter, count);
            fcx_ir_build_branch(gen->current_block, cmp_result, body_id, exit_id);
            
            // Body block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, body_id);
            
            // Push loop context: break goes to exit, continue goes to header
            ir_gen_push_loop(gen, exit_id, header_id);
            
            for (size_t i = 0; i < stmt->data.loop.body.count; i++) {
                if (!ir_gen_generate_statement(gen, stmt->data.loop.body.statements[i])) {
                    ir_gen_pop_loop(gen);
                    return false;
                }
            }
            
            ir_gen_pop_loop(gen);
            
            // Increment counter (only if no terminator)
            if (gen->current_block->instruction_count == 0 ||
                (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN)) {
                VirtualReg one = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_const(gen->current_block, one, 1);
                VirtualReg new_counter = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_binary_op(gen->current_block, FCXIR_ADD, new_counter, counter, one);
                
                // Jump back to header
                fcx_ir_build_jump(gen->current_block, header_id);
            }
            
            // Exit block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, exit_id);
            break;
        }
        
        case LOOP_RANGE: {
            // Range loop: i </ n: body
            // Save current block ID before creating new blocks
            uint32_t current_block_id = gen->current_block->id;
            
            // Create blocks and save their IDs
            FcxIRBasicBlock* header_block = fcx_ir_block_create(gen->current_function, "range.header");
            uint32_t header_id = header_block->id;
            FcxIRBasicBlock* body_block = fcx_ir_block_create(gen->current_function, "range.body");
            uint32_t body_id = body_block->id;
            FcxIRBasicBlock* exit_block = fcx_ir_block_create(gen->current_function, "range.exit");
            uint32_t exit_id = exit_block->id;
            
            // Refresh current_block pointer
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            // Initialize loop variable
            VirtualReg loop_var = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
            fcx_ir_build_const(gen->current_block, loop_var, 0);
            
            // Get upper bound
            VirtualReg bound = ir_gen_generate_expression(gen, stmt->data.loop.condition);
            
            // Jump to header
            fcx_ir_build_jump(gen->current_block, header_id);
            
            // Header block (loop_var < bound)
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, header_id);
            
            VirtualReg cmp_result = ir_gen_alloc_temp(gen, VREG_TYPE_BOOL);
            fcx_ir_build_binary_op(gen->current_block, FCXIR_CMP_LT, cmp_result, loop_var, bound);
            fcx_ir_build_branch(gen->current_block, cmp_result, body_id, exit_id);
            
            // Body block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, body_id);
            
            // Push loop context: break goes to exit, continue goes to header
            ir_gen_push_loop(gen, exit_id, header_id);
            
            for (size_t i = 0; i < stmt->data.loop.body.count; i++) {
                if (!ir_gen_generate_statement(gen, stmt->data.loop.body.statements[i])) {
                    ir_gen_pop_loop(gen);
                    return false;
                }
            }
            
            ir_gen_pop_loop(gen);
            
            // Increment loop variable (only if no terminator)
            if (gen->current_block->instruction_count == 0 ||
                (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN)) {
                VirtualReg one = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_const(gen->current_block, one, 1);
                VirtualReg new_var = ir_gen_alloc_temp(gen, VREG_TYPE_I64);
                fcx_ir_build_binary_op(gen->current_block, FCXIR_ADD, new_var, loop_var, one);
                
                // Jump back to header
                fcx_ir_build_jump(gen->current_block, header_id);
            }
            
            // Exit block
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, exit_id);
            break;
        }
        
        default:
            ir_gen_set_error(gen, "Unknown loop type");
            return false;
    }
    
    return !gen->has_error;
}

// ============================================================================
// Statement Generation
// ============================================================================

bool ir_gen_generate_statement(IRGenerator* gen, Stmt* stmt) {
    if (!gen || !stmt) return false;
    
    switch (stmt->type) {
        case STMT_EXPRESSION: {
            ir_gen_generate_expression(gen, stmt->data.expression);
            return !gen->has_error;
        }
        
        case STMT_LET: {
            VirtualReg value;
            
            // Determine the variable type - type annotation takes precedence
            VRegType var_type = VREG_TYPE_I64; // Default type
            if (stmt->data.let.type_annotation) {
                var_type = ir_gen_map_type_kind(stmt->data.let.type_annotation->kind);
            }
            
            if (stmt->data.let.initializer) {
                VirtualReg init_value = ir_gen_generate_expression(gen, stmt->data.let.initializer);
                // Use the declared type if specified, otherwise use the initializer's type
                if (stmt->data.let.type_annotation) {
                    value = ir_gen_alloc_temp(gen, var_type);
                    // If types differ, we need to handle type conversion
                    // For bigint types (i128, i256, etc.), we need to properly extend the value
                    if (var_type != init_value.type) {
                        // Check if we're extending to a larger integer type
                        bool is_bigint_target = (var_type == VREG_TYPE_I128 || var_type == VREG_TYPE_I256 ||
                                                 var_type == VREG_TYPE_I512 || var_type == VREG_TYPE_I1024 ||
                                                 var_type == VREG_TYPE_U128 || var_type == VREG_TYPE_U256 ||
                                                 var_type == VREG_TYPE_U512 || var_type == VREG_TYPE_U1024);
                        bool is_smaller_source = (init_value.type == VREG_TYPE_I8 || init_value.type == VREG_TYPE_I16 ||
                                                  init_value.type == VREG_TYPE_I32 || init_value.type == VREG_TYPE_I64 ||
                                                  init_value.type == VREG_TYPE_U8 || init_value.type == VREG_TYPE_U16 ||
                                                  init_value.type == VREG_TYPE_U32 || init_value.type == VREG_TYPE_U64);
                        
                        if (is_bigint_target && is_smaller_source) {
                            // For bigint targets with smaller sources, we need to generate a proper
                            // bigint constant if the source is a constant, or extend at runtime
                            // For now, just do a mov and let the LLVM backend handle the extension
                            fcx_ir_build_mov(gen->current_block, value, init_value);
                        } else {
                            fcx_ir_build_mov(gen->current_block, value, init_value);
                        }
                    } else {
                        fcx_ir_build_mov(gen->current_block, value, init_value);
                    }
                } else {
                    value = ir_gen_alloc_temp(gen, init_value.type);
                    fcx_ir_build_mov(gen->current_block, value, init_value);
                }
            } else {
                // Uninitialized variable - allocate register with appropriate type
                value = ir_gen_alloc_temp(gen, var_type);
                // Initialize to zero
                fcx_ir_build_const(gen->current_block, value, 0);
            }
            
            ir_gen_add_symbol(gen, stmt->data.let.name, value);
            return !gen->has_error;
        }
        
        case STMT_RETURN: {
            if (stmt->data.return_value) {
                VirtualReg value = ir_gen_generate_expression(gen, stmt->data.return_value);
                fcx_ir_build_return(gen->current_block, value, true);
            } else {
                VirtualReg dummy = {0};
                fcx_ir_build_return(gen->current_block, dummy, false);
            }
            return !gen->has_error;
        }
        
        case STMT_HALT: {
            // Halt is similar to return but may have different semantics
            if (stmt->data.return_value) {
                VirtualReg value = ir_gen_generate_expression(gen, stmt->data.return_value);
                fcx_ir_build_return(gen->current_block, value, true);
            } else {
                VirtualReg dummy = {0};
                fcx_ir_build_return(gen->current_block, dummy, false);
            }
            return !gen->has_error;
        }
        
        case STMT_IF: {
            // Save current block ID before creating new blocks (realloc may invalidate pointers)
            uint32_t current_block_id = gen->current_block->id;
            
            VirtualReg cond = ir_gen_generate_expression(gen, stmt->data.if_stmt.condition);
            
            // Refresh current_block pointer in case expression generation created new blocks
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            // Create blocks and save their IDs (pointers may be invalidated by subsequent creates)
            FcxIRBasicBlock* then_block = fcx_ir_block_create(gen->current_function, "if.then");
            uint32_t then_label = then_block->id;
            
            uint32_t else_label = 0;
            if (stmt->data.if_stmt.else_branch.count > 0) {
                FcxIRBasicBlock* else_block = fcx_ir_block_create(gen->current_function, "if.else");
                else_label = else_block->id;
            }
            
            FcxIRBasicBlock* merge_block = fcx_ir_block_create(gen->current_function, "if.merge");
            uint32_t merge_label = merge_block->id;
            
            // Refresh current_block pointer after creating new blocks (realloc may have moved memory)
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, current_block_id);
            
            fcx_ir_build_branch(gen->current_block, cond, then_label, 
                               else_label ? else_label : merge_label);
            
            // Then block - look up by ID since pointers may be stale
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, then_label);
            for (size_t i = 0; i < stmt->data.if_stmt.then_branch.count; i++) {
                if (!ir_gen_generate_statement(gen, stmt->data.if_stmt.then_branch.statements[i])) {
                    return false;
                }
            }
            // Only emit jump if block doesn't already have a terminator (return, jump, branch)
            if (gen->current_block->instruction_count == 0 ||
                (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                 gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH)) {
                fcx_ir_build_jump(gen->current_block, merge_label);
            }
            
            // Else block (if present)
            if (stmt->data.if_stmt.else_branch.count > 0) {
                gen->current_block = fcx_ir_block_get_by_id(gen->current_function, else_label);
                for (size_t i = 0; i < stmt->data.if_stmt.else_branch.count; i++) {
                    if (!ir_gen_generate_statement(gen, stmt->data.if_stmt.else_branch.statements[i])) {
                        return false;
                    }
                }
                // Only emit jump if block doesn't already have a terminator (return, jump, branch)
                if (gen->current_block->instruction_count == 0 ||
                    (gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_RETURN &&
                     gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_JUMP &&
                     gen->current_block->instructions[gen->current_block->instruction_count - 1].opcode != FCXIR_BRANCH)) {
                    fcx_ir_build_jump(gen->current_block, merge_label);
                }
            }
            
            // Merge block - look up by ID
            gen->current_block = fcx_ir_block_get_by_id(gen->current_function, merge_label);
            
            return !gen->has_error;
        }
        
        case STMT_LOOP: {
            return ir_gen_generate_loop(gen, stmt);
        }
        
        case STMT_BREAK: {
            uint32_t break_target = ir_gen_get_break_target(gen);
            if (break_target == 0) {
                ir_gen_set_error(gen, "break statement outside of loop");
                return false;
            }
            fcx_ir_build_jump(gen->current_block, break_target);
            return !gen->has_error;
        }
        
        case STMT_CONTINUE: {
            uint32_t continue_target = ir_gen_get_continue_target(gen);
            if (continue_target == 0) {
                ir_gen_set_error(gen, "continue statement outside of loop");
                return false;
            }
            fcx_ir_build_jump(gen->current_block, continue_target);
            return !gen->has_error;
        }
        
        case STMT_MOD:
            // Module declarations are handled at a higher level
            // For now, silently skip them during IR generation
            return true;
        
        case STMT_USE:
            // Use declarations are handled during symbol resolution
            // For now, silently skip them during IR generation
            return true;
        
        default:
            ir_gen_set_error(gen, "Unsupported statement type");
            return false;
    }
}

// ============================================================================
// Function Generation
// ============================================================================

bool ir_gen_generate_function(IRGenerator* gen, Stmt* func_stmt) {
    if (!gen || !func_stmt || func_stmt->type != STMT_FUNCTION) {
        ir_gen_set_error(gen, "Invalid function statement");
        return false;
    }
    
    // Map return type from function declaration
    VRegType return_type = VREG_TYPE_I32;  // Default to i32
    if (func_stmt->data.function.return_type) {
        return_type = ir_gen_map_type_kind(func_stmt->data.function.return_type->kind);
    }
    
    // Create function
    gen->current_function = fcx_ir_function_create(
        func_stmt->data.function.name,
        return_type
    );
    
    // Enter a new scope for function parameters and locals
    ir_gen_enter_scope(gen);
    
    // Create entry block
    gen->current_block = fcx_ir_block_create(gen->current_function, "entry");
    
    // Allocate parameter array if there are parameters
    if (func_stmt->data.function.param_count > 0) {
        gen->current_function->parameter_count = (uint8_t)func_stmt->data.function.param_count;
        gen->current_function->parameters = malloc(func_stmt->data.function.param_count * sizeof(VirtualReg));
    }
    
    // Add function parameters to symbol table
    for (size_t i = 0; i < func_stmt->data.function.param_count; i++) {
        const char* param_name = func_stmt->data.function.params[i].name;
        VRegType param_type = VREG_TYPE_I64;  // Default to i64
        if (func_stmt->data.function.params[i].type) {
            param_type = ir_gen_map_type_kind(func_stmt->data.function.params[i].type->kind);
        }
        VirtualReg param_vreg = ir_gen_alloc_temp(gen, param_type);
        ir_gen_add_symbol(gen, param_name, param_vreg);
        
        // Store parameter vreg in function's parameter array
        if (gen->current_function->parameters) {
            gen->current_function->parameters[i] = param_vreg;
        }
    }
    
    // Generate function body
    for (size_t i = 0; i < func_stmt->data.function.body.count; i++) {
        if (!ir_gen_generate_statement(gen, func_stmt->data.function.body.statements[i])) {
            ir_gen_exit_scope(gen);
            return false;
        }
    }
    
    // Exit function scope
    ir_gen_exit_scope(gen);
    
    // Add function to module
    fcx_ir_module_add_function(gen->module, gen->current_function);
    
    return !gen->has_error;
}

// ============================================================================
// Module Generation
// ============================================================================

bool ir_gen_generate_module(IRGenerator* gen, Stmt** statements, size_t stmt_count) {
    if (!gen || !statements) return false;
    
    // First pass: collect global variables
    for (size_t i = 0; i < stmt_count; i++) {
        Stmt* stmt = statements[i];
        
        if (stmt->type == STMT_LET) {
            // Global variable declaration
            const char* name = stmt->data.let.name;
            
            // Add global variable to module
            if (gen->module) {
                if (gen->module->global_count >= gen->module->global_capacity) {
                    size_t new_cap = gen->module->global_capacity == 0 ? 16 : gen->module->global_capacity * 2;
                    gen->module->globals = realloc(gen->module->globals, new_cap * sizeof(FcxIRGlobal));
                    gen->module->global_capacity = new_cap;
                }
                
                uint32_t global_index = gen->module->global_count;
                FcxIRGlobal* global = &gen->module->globals[gen->module->global_count++];
                global->name = strdup(name);
                global->vreg = (VirtualReg){0};  // Not used - globals are accessed by name
                global->type = VREG_TYPE_I64;
                global->is_const = stmt->data.let.is_const;
                
                // Set initial value
                if (stmt->data.let.initializer && 
                    stmt->data.let.initializer->type == EXPR_LITERAL &&
                    stmt->data.let.initializer->data.literal.type == LIT_INTEGER) {
                    global->init_value = stmt->data.let.initializer->data.literal.value.integer;
                    global->has_init = true;
                } else {
                    global->init_value = 0;
                    global->has_init = false;
                }
                
                // Add to symbol table as a global
                ir_gen_add_global_symbol(gen, name, global_index);
            }
        }
    }
    
    // Second pass: generate functions
    for (size_t i = 0; i < stmt_count; i++) {
        Stmt* stmt = statements[i];
        
        if (stmt->type == STMT_FUNCTION) {
            if (!ir_gen_generate_function(gen, stmt)) {
                return false;
            }
        }
    }
    
    return !gen->has_error;
}
