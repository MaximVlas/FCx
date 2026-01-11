#define _POSIX_C_SOURCE 200809L
#include "fc_ir_lower.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Context Management
// ============================================================================

FcIRLowerContext* fc_ir_lower_create(void) {
    FcIRLowerContext* ctx = (FcIRLowerContext*)malloc(sizeof(FcIRLowerContext));
    if (!ctx) return NULL;
    
    ctx->fc_module = NULL;
    ctx->current_function = NULL;
    ctx->current_block = NULL;
    
    ctx->vreg_map = NULL;
    ctx->vreg_map_capacity = 0;
    
    ctx->label_map = NULL;
    ctx->label_map_capacity = 0;
    
    ctx->error_message = NULL;
    ctx->has_error = false;
    
    return ctx;
}

void fc_ir_lower_destroy(FcIRLowerContext* ctx) {
    if (!ctx) return;
    
    free(ctx->vreg_map);
    free(ctx->label_map);
    
    if (ctx->error_message) {
        free(ctx->error_message);
    }
    
    free(ctx);
}

void fc_ir_lower_set_error(FcIRLowerContext* ctx, const char* message) {
    if (!ctx) return;
    
    ctx->has_error = true;
    if (ctx->error_message) {
        free(ctx->error_message);
    }
    ctx->error_message = strdup(message);
}

const char* fc_ir_lower_get_error(const FcIRLowerContext* ctx) {
    return ctx ? ctx->error_message : NULL;
}

// ============================================================================
// Virtual Register and Label Mapping
// ============================================================================

VirtualReg fc_ir_lower_map_vreg(FcIRLowerContext* ctx, VirtualReg fcx_vreg) {
    if (!ctx) return fcx_vreg;
    
    // Ensure capacity
    if (fcx_vreg.id >= ctx->vreg_map_capacity) {
        size_t new_capacity = (fcx_vreg.id + 1) * 2;
        VirtualReg* new_map = (VirtualReg*)realloc(
            ctx->vreg_map, new_capacity * sizeof(VirtualReg));
        
        if (!new_map) return fcx_vreg;
        
        // Initialize new entries
        for (size_t i = ctx->vreg_map_capacity; i < new_capacity; i++) {
            new_map[i].id = 0;
            new_map[i].type = VREG_TYPE_VOID;
            new_map[i].size = 0;
            new_map[i].flags = 0;
        }
        
        ctx->vreg_map = new_map;
        ctx->vreg_map_capacity = new_capacity;
    }
    
    // Check if already mapped
    if (ctx->vreg_map[fcx_vreg.id].id != 0) {
        return ctx->vreg_map[fcx_vreg.id];
    }
    
    // Create new mapping (for now, 1:1 mapping)
    ctx->vreg_map[fcx_vreg.id] = fcx_vreg;
    return fcx_vreg;
}

uint32_t fc_ir_lower_map_label(FcIRLowerContext* ctx, uint32_t fcx_label) {
    if (!ctx) return fcx_label;
    
    // Ensure capacity
    if (fcx_label >= ctx->label_map_capacity) {
        size_t new_capacity = (fcx_label + 1) * 2;
        uint32_t* new_map = (uint32_t*)realloc(
            ctx->label_map, new_capacity * sizeof(uint32_t));
        
        if (!new_map) return fcx_label;
        
        // Initialize new entries
        for (size_t i = ctx->label_map_capacity; i < new_capacity; i++) {
            new_map[i] = 0;
        }
        
        ctx->label_map = new_map;
        ctx->label_map_capacity = new_capacity;
    }
    
    // Check if already mapped
    if (ctx->label_map[fcx_label] != 0) {
        return ctx->label_map[fcx_label];
    }
    
    // Create new mapping (for now, 1:1 mapping)
    ctx->label_map[fcx_label] = fcx_label;
    return fcx_label;
}

// ============================================================================
// Syscall Lowering - System V AMD64 ABI
// ============================================================================

bool fc_ir_lower_syscall(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    // System V AMD64 syscall ABI:
    // rax = syscall number
    // rdi, rsi, rdx, r10, r8, r9 = arguments (up to 6)
    // Return value in rax
    // CLOBBERED: rcx, r11 (used by syscall instruction internally)
    
    // Pre-colored syscall registers
    VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rsi_vreg = {.id = 1002, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rdx_vreg = {.id = 1003, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg r10_vreg = {.id = 1004, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg r8_vreg  = {.id = 1005, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg r9_vreg  = {.id = 1006, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rcx_vreg = {.id = 1007, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg r11_vreg = {.id = 1015, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg syscall_regs[] = {rdi_vreg, rsi_vreg, rdx_vreg, r10_vreg, r8_vreg, r9_vreg};
    
    VirtualReg result = fc_ir_lower_map_vreg(ctx, instr->u.syscall_op.dest);
    
    // Get all source arguments first
    VirtualReg args[6];
    uint8_t arg_count = instr->u.syscall_op.arg_count;
    if (arg_count > 6) arg_count = 6;
    
    for (uint8_t i = 0; i < arg_count; i++) {
        args[i] = fc_ir_lower_map_vreg(ctx, instr->u.syscall_op.args[i]);
    }
    
    VirtualReg syscall_num = fc_ir_lower_map_vreg(ctx, instr->u.syscall_op.syscall_num);
    
    // Save rcx and r11 before syscall (they get clobbered)
    // Use PUSH to save them on the stack
    fc_ir_build_push(ctx->current_block, fc_ir_operand_vreg(rcx_vreg));
    fc_ir_build_push(ctx->current_block, fc_ir_operand_vreg(r11_vreg));
    
    // Move arguments to syscall registers in reverse order to avoid clobbering
    for (int i = (int)arg_count - 1; i >= 0; i--) {
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(syscall_regs[i]),
                       fc_ir_operand_vreg(args[i]));
    }
    
    // Load syscall number into rax last
    fc_ir_build_mov(ctx->current_block, 
                    fc_ir_operand_vreg(rax_vreg),
                    fc_ir_operand_vreg(syscall_num));
    
    // Emit syscall instruction
    fc_ir_build_syscall(ctx->current_block);
    
    // Move result from rax to destination (before restoring rcx/r11)
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(result),
                   fc_ir_operand_vreg(rax_vreg));
    
    // Restore r11 and rcx (in reverse order of push)
    fc_ir_build_pop(ctx->current_block, fc_ir_operand_vreg(r11_vreg));
    fc_ir_build_pop(ctx->current_block, fc_ir_operand_vreg(rcx_vreg));
    
    return true;
}

// ============================================================================
// Inline Assembly Lowering
// ============================================================================

bool fc_ir_lower_inline_asm(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    // For inline assembly, we emit a special FC IR instruction that will be
    // passed through to the LLVM backend which handles inline asm natively
    
    // Store the inline asm data pointer and emit the instruction
    fc_ir_build_inline_asm_raw(ctx->current_block, (int64_t)(uintptr_t)&instr->u.inline_asm);
    
    return true;
}

// ============================================================================
// Memory Allocation Lowering
// ============================================================================

bool fc_ir_lower_alloc(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    // Lower FCxIR::Alloc to FCIR::Call(_fcx_alloc) with proper calling convention
    // System V AMD64 calling convention:
    // rdi = size, rsi = alignment
    // Return value in rax
    
    VirtualReg size = fc_ir_lower_map_vreg(ctx, instr->u.alloc_op.size);
    VirtualReg align = fc_ir_lower_map_vreg(ctx, instr->u.alloc_op.align);
    VirtualReg result = fc_ir_lower_map_vreg(ctx, instr->u.alloc_op.dest);
    
    // Move arguments to calling convention registers
    VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rsi_vreg = {.id = 1002, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(rdi_vreg),
                   fc_ir_operand_vreg(size));
    
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(rsi_vreg),
                   fc_ir_operand_vreg(align));
    
    // Call _fcx_alloc (use external call to properly register the function)
    fc_ir_build_call_external(ctx->current_block, ctx->fc_module, "_fcx_alloc");
    
    // Move result from rax to destination
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(result),
                   fc_ir_operand_vreg(rax_vreg));
    
    return true;
}

// ============================================================================
// Pointer Arithmetic Lowering - Three-Pointer Type System
// ============================================================================

bool fc_ir_lower_ptr_arithmetic(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.dest);
    VirtualReg ptr = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.left);
    VirtualReg offset = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.right);
    
    // Handle different pointer types:
    // - ptr<T>: scaled by sizeof(T)
    // - byteptr: scaled by 1
    // - rawptr: arithmetic forbidden (should be caught in semantic analysis)
    
    if (ptr.type == VREG_TYPE_PTR) {
        // Typed pointer - need to scale offset by element size
        // For now, assume 8-byte elements (will be refined with type info)
        VirtualReg scaled_offset = {
            .id = ctx->current_function->next_vreg_id++,
            .type = VREG_TYPE_I64,
            .size = 8,
            .flags = 0
        };
        
        VirtualReg scale = {
            .id = ctx->current_function->next_vreg_id++,
            .type = VREG_TYPE_I64,
            .size = 8,
            .flags = 0
        };
        
        // Get element size based on pointer type
        // For typed pointers, the element size is determined by the pointed-to type
        // Default to 8 bytes (64-bit) for generic pointers
        int64_t element_size = 8;
        if (ptr.size > 0 && ptr.size <= 8) {
            element_size = ptr.size;
        }
        
        // Load scale factor (element size)
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(scale),
                       fc_ir_operand_imm(element_size));
        
        // Multiply offset by scale
        fc_ir_build_binary_op(ctx->current_block, FCIR_IMUL,
                             fc_ir_operand_vreg(scaled_offset),
                             fc_ir_operand_vreg(offset));
        
        // Add scaled offset to pointer
        fc_ir_build_binary_op(ctx->current_block, FCIR_ADD,
                             fc_ir_operand_vreg(dest),
                             fc_ir_operand_vreg(scaled_offset));
    } else if (ptr.type == VREG_TYPE_BYTEPTR) {
        // Byte pointer - no scaling needed
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(dest),
                       fc_ir_operand_vreg(ptr));
        
        fc_ir_build_binary_op(ctx->current_block, FCIR_ADD,
                             fc_ir_operand_vreg(dest),
                             fc_ir_operand_vreg(offset));
    } else {
        fc_ir_lower_set_error(ctx, "Invalid pointer type for arithmetic");
        return false;
    }
    
    return true;
}

// ============================================================================
// Atomic Operations Lowering - LOCK-prefixed instructions
// ============================================================================

bool fc_ir_lower_atomic_load(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.unary_op.dest);
    VirtualReg ptr = fc_ir_lower_map_vreg(ctx, instr->u.unary_op.src);
    
    // Atomic load: MOV from memory with appropriate ordering
    // For aligned loads, MOV is atomic on x86_64
    FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
    
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   mem_op);
    
    return true;
}

bool fc_ir_lower_atomic_store(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg ptr = fc_ir_lower_map_vreg(ctx, instr->u.load_store.dest);
    VirtualReg value = fc_ir_lower_map_vreg(ctx, instr->u.load_store.src);
    
    // Atomic store: XCHG (implicitly locked)
    FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
    
    fc_ir_build_xchg(ctx->current_block,
                    mem_op,
                    fc_ir_operand_vreg(value),
                    false);  // XCHG is implicitly locked
    
    return true;
}

bool fc_ir_lower_atomic_swap(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.dest);
    VirtualReg ptr = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.left);
    VirtualReg value = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.right);
    
    // Atomic swap: LOCK XCHG
    FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
    
    // Move value to destination first (XCHG will swap it)
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_vreg(value));
    
    // XCHG dest with memory
    fc_ir_build_xchg(ctx->current_block,
                    mem_op,
                    fc_ir_operand_vreg(dest),
                    true);  // Explicitly locked
    
    return true;
}

bool fc_ir_lower_atomic_cas(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.atomic_cas.dest);
    VirtualReg ptr = fc_ir_lower_map_vreg(ctx, instr->u.atomic_cas.ptr);
    VirtualReg expected = fc_ir_lower_map_vreg(ctx, instr->u.atomic_cas.expected);
    VirtualReg new_val = fc_ir_lower_map_vreg(ctx, instr->u.atomic_cas.new_val);
    
    // Atomic CAS: LOCK CMPXCHG
    // rax = expected value
    // memory location = ptr
    // register = new_val
    
    VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    
    // Move expected value to rax
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(rax_vreg),
                   fc_ir_operand_vreg(expected));
    
    // LOCK CMPXCHG [ptr], new_val
    FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
    
    fc_ir_build_cmpxchg(ctx->current_block,
                       mem_op,
                       fc_ir_operand_vreg(new_val),
                       true);  // Locked
    
    // Move result (old value) from rax to dest
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_vreg(rax_vreg));
    
    return true;
}

// ============================================================================
// Memory Barrier Lowering
// ============================================================================

bool fc_ir_lower_fence(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    switch (instr->opcode) {
        case FCXIR_FENCE_FULL:
            fc_ir_build_mfence(ctx->current_block);
            break;
            
        case FCXIR_FENCE_ACQUIRE:
            fc_ir_build_lfence(ctx->current_block);
            break;
            
        case FCXIR_FENCE_RELEASE:
            fc_ir_build_sfence(ctx->current_block);
            break;
            
        default:
            fc_ir_lower_set_error(ctx, "Unknown fence type");
            return false;
    }
    
    return true;
}

// ============================================================================
// Binary and Unary Operations Lowering
// ============================================================================

bool fc_ir_lower_binary_op(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.dest);
    VirtualReg left = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.left);
    VirtualReg right = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.right);
    
    // Move left operand to dest first (x86_64 two-operand form)
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_vreg(left));
    
    // Map FCx IR opcode to FC IR opcode
    FcIROpcode fc_opcode;
    
    switch (instr->opcode) {
        case FCXIR_ADD:
            fc_opcode = FCIR_ADD;
            break;
        case FCXIR_SUB:
            fc_opcode = FCIR_SUB;
            break;
        case FCXIR_MUL:
            fc_opcode = FCIR_IMUL;
            break;
        case FCXIR_DIV:
            fc_opcode = FCIR_IDIV;
            break;
        case FCXIR_MOD:
            fc_opcode = FCIR_IDIV;  // MOD uses IDIV, remainder is in RDX
            break;
        case FCXIR_AND:
            fc_opcode = FCIR_AND;
            break;
        case FCXIR_OR:
            fc_opcode = FCIR_OR;
            break;
        case FCXIR_XOR:
            fc_opcode = FCIR_XOR;
            break;
        case FCXIR_LSHIFT:
            fc_opcode = FCIR_SHL;
            break;
        case FCXIR_RSHIFT:
            fc_opcode = FCIR_SAR;
            break;
        case FCXIR_LOGICAL_RSHIFT:
            fc_opcode = FCIR_SHR;
            break;
        case FCXIR_ROTATE_LEFT:
            fc_opcode = FCIR_ROL;
            break;
        case FCXIR_ROTATE_RIGHT:
            fc_opcode = FCIR_ROR;
            break;
        default:
            fc_ir_lower_set_error(ctx, "Unsupported binary operation");
            return false;
    }
    
    // Apply operation
    fc_ir_build_binary_op(ctx->current_block,
                         fc_opcode,
                         fc_ir_operand_vreg(dest),
                         fc_ir_operand_vreg(right));
    
    return true;
}

bool fc_ir_lower_unary_op(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.unary_op.dest);
    VirtualReg src = fc_ir_lower_map_vreg(ctx, instr->u.unary_op.src);
    
    // Move source to dest first
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_vreg(src));
    
    // Map FCx IR opcode to FC IR opcode
    FcIROpcode fc_opcode;
    
    switch (instr->opcode) {
        case FCXIR_NEG:
            fc_opcode = FCIR_NEG;
            break;
        case FCXIR_NOT:
            fc_opcode = FCIR_NOT;
            break;
        default:
            fc_ir_lower_set_error(ctx, "Unsupported unary operation");
            return false;
    }
    
    // Apply operation
    fc_ir_build_unary_op(ctx->current_block, fc_opcode, fc_ir_operand_vreg(dest));
    
    return true;
}

// ============================================================================
// Comparison Operations Lowering
// ============================================================================

bool fc_ir_lower_comparison(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.dest);
    VirtualReg left = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.left);
    VirtualReg right = fc_ir_lower_map_vreg(ctx, instr->u.binary_op.right);
    
    // For LLVM backend compatibility, we use a simpler approach:
    // 1. Emit CMP instruction to set flags
    // 2. Store the comparison type in a special way that the LLVM backend can recognize
    //
    // The LLVM backend will convert this to an icmp + zext pattern
    
    // Emit CMP instruction
    fc_ir_build_cmp(ctx->current_block,
                   fc_ir_operand_vreg(left),
                   fc_ir_operand_vreg(right));
    
    // Determine the condition code based on comparison type
    FcIROpcode setcc_opcode;
    switch (instr->opcode) {
        case FCXIR_CMP_EQ:
            setcc_opcode = FCIR_JE;
            break;
        case FCXIR_CMP_NE:
            setcc_opcode = FCIR_JNE;
            break;
        case FCXIR_CMP_LT:
            setcc_opcode = FCIR_JL;
            break;
        case FCXIR_CMP_LE:
            setcc_opcode = FCIR_JLE;
            break;
        case FCXIR_CMP_GT:
            setcc_opcode = FCIR_JG;
            break;
        case FCXIR_CMP_GE:
            setcc_opcode = FCIR_JGE;
            break;
        default:
            fc_ir_lower_set_error(ctx, "Unsupported comparison operation");
            return false;
    }
    
    // Emit a special SETCC-like instruction
    // We'll use a MOV with a special immediate value that encodes the condition
    // The LLVM backend will recognize this pattern and generate the correct icmp
    //
    // Pattern: CMP left, right followed by MOV dest, condition_code
    // where condition_code is a negative value encoding the comparison type
    //
    // Actually, let's use a simpler approach: emit a conditional jump to a label
    // that sets dest to 1, with fallthrough setting dest to 0, but ensure the
    // labels are within the same basic block context for the LLVM backend
    
    // For now, use the simplest approach: emit the comparison and store the
    // condition code in the destination register. The LLVM backend will need
    // to handle this specially.
    //
    // We'll use a special encoding: store the condition code as a negative immediate
    // The LLVM backend will recognize this and generate an icmp + zext
    
    // Store a marker value that the LLVM backend can recognize
    // We use the pattern: MOV dest, -(condition_code + 1000)
    // This allows the LLVM backend to detect this is a comparison result
    int64_t condition_marker = -(int64_t)(setcc_opcode + 1000);
    
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_imm(condition_marker));
    
    return true;
}


// ============================================================================
// Control Flow Lowering
// ============================================================================

bool fc_ir_lower_branch(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    VirtualReg cond = fc_ir_lower_map_vreg(ctx, instr->u.branch_op.cond);
    uint32_t true_label = fc_ir_lower_map_label(ctx, instr->u.branch_op.true_label);
    uint32_t false_label = fc_ir_lower_map_label(ctx, instr->u.branch_op.false_label);
    
    // Compare condition with zero
    fc_ir_build_cmp(ctx->current_block,
                   fc_ir_operand_vreg(cond),
                   fc_ir_operand_imm(0));
    
    // Jump if not equal (condition is true)
    fc_ir_build_jcc(ctx->current_block, FCIR_JNE, true_label);
    
    // Fall through or jump to false label
    fc_ir_build_jmp(ctx->current_block, false_label);
    
    return true;
}

bool fc_ir_lower_jump(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    uint32_t label = fc_ir_lower_map_label(ctx, instr->u.jump_op.label_id);
    
    fc_ir_build_jmp(ctx->current_block, label);
    
    return true;
}

bool fc_ir_lower_call(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    // System V AMD64 calling convention:
    // Arguments in rdi, rsi, rdx, rcx, r8, r9
    // Return value in rax
    
    uint32_t arg_reg_ids[] = {1001, 1002, 1003, 1007, 1005, 1006};
    
    // Check if this is a bigint print function that needs special handling
    const char* func_name = instr->u.call_op.function;
    bool is_bigint_print = func_name && (
        strcmp(func_name, "_fcx_println_i128") == 0 ||
        strcmp(func_name, "_fcx_println_u128") == 0 ||
        strcmp(func_name, "_fcx_println_i256") == 0 ||
        strcmp(func_name, "_fcx_println_u256") == 0 ||
        strcmp(func_name, "_fcx_println_i512") == 0 ||
        strcmp(func_name, "_fcx_println_u512") == 0 ||
        strcmp(func_name, "_fcx_println_i1024") == 0 ||
        strcmp(func_name, "_fcx_println_u1024") == 0
    );
    
    // Move arguments to calling convention registers
    for (uint8_t i = 0; i < instr->u.call_op.arg_count && i < 6; i++) {
        VirtualReg arg = fc_ir_lower_map_vreg(ctx, instr->u.call_op.args[i]);
        
        // For bigint print functions, preserve the original type for the first argument
        VRegType arg_type = VREG_TYPE_I64;
        uint8_t arg_size = 8;
        if (is_bigint_print && i == 0) {
            arg_type = arg.type;
            arg_size = arg.size;
        }
        
        VirtualReg arg_reg = {
            .id = arg_reg_ids[i],
            .type = arg_type,
            .size = arg_size,
            .flags = 0
        };
        
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(arg_reg),
                       fc_ir_operand_vreg(arg));
    }
    
    // Check if this is an external function (runtime function)
    bool is_external = (func_name && 
                       (strncmp(func_name, "_fcx_", 5) == 0 || 
                        strncmp(func_name, "_external_", 10) == 0));
    
    if (is_external) {
        // Use external function call
        fc_ir_build_call_external(ctx->current_block, ctx->fc_module, func_name);
    } else {
        // Use regular function call
        fc_ir_build_call(ctx->current_block, func_name);
    }
    
    // Move result from rax to destination
    VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.call_op.dest);
    VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    
    fc_ir_build_mov(ctx->current_block,
                   fc_ir_operand_vreg(dest),
                   fc_ir_operand_vreg(rax_vreg));
    
    return true;
}

bool fc_ir_lower_return(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    if (instr->u.return_op.has_value) {
        // Move return value to rax
        VirtualReg value = fc_ir_lower_map_vreg(ctx, instr->u.return_op.value);
        VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
        
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(rax_vreg),
                       fc_ir_operand_vreg(value));
    }
    
    fc_ir_build_ret(ctx->current_block);
    
    return true;
}

// ============================================================================
// MMIO Operations Lowering
// ============================================================================

bool fc_ir_lower_mmio(FcIRLowerContext* ctx, const FcxIRInstruction* instr) {
    if (!ctx || !instr) return false;
    
    if (instr->opcode == FCXIR_MMIO_READ) {
        VirtualReg dest = fc_ir_lower_map_vreg(ctx, instr->u.mmio_op.dest);
        
        // Create memory operand with absolute address
        VirtualReg addr_reg = {
            .id = ctx->current_function->next_vreg_id++,
            .type = VREG_TYPE_I64,
            .size = 8,
            .flags = 0
        };
        
        // Load absolute address
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(addr_reg),
                       fc_ir_operand_imm((int64_t)instr->u.mmio_op.address));
        
        // Load from memory with volatile semantics
        FcOperand mem_op = fc_ir_operand_mem(addr_reg, (VirtualReg){0}, 0, 1);
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(dest),
                       mem_op);
        
    } else if (instr->opcode == FCXIR_MMIO_WRITE) {
        VirtualReg value = fc_ir_lower_map_vreg(ctx, instr->u.mmio_op.value);
        
        // Create memory operand with absolute address
        VirtualReg addr_reg = {
            .id = ctx->current_function->next_vreg_id++,
            .type = VREG_TYPE_I64,
            .size = 8,
            .flags = 0
        };
        
        // Load absolute address
        fc_ir_build_mov(ctx->current_block,
                       fc_ir_operand_vreg(addr_reg),
                       fc_ir_operand_imm((int64_t)instr->u.mmio_op.address));
        
        // Store to memory with volatile semantics
        FcOperand mem_op = fc_ir_operand_mem(addr_reg, (VirtualReg){0}, 0, 1);
        fc_ir_build_mov(ctx->current_block,
                       mem_op,
                       fc_ir_operand_vreg(value));
    }
    
    return true;
}

// ============================================================================
// Instruction Lowering Dispatcher
// ============================================================================

bool fc_ir_lower_instruction(FcIRLowerContext* ctx, const FcxIRInstruction* fcx_instr) {
    if (!ctx || !fcx_instr) return false;
    
    switch (fcx_instr->opcode) {
        // Constants and loads/stores
        case FCXIR_CONST: {
            VirtualReg dest = fc_ir_lower_map_vreg(ctx, fcx_instr->u.const_op.dest);
            fc_ir_build_mov(ctx->current_block,
                           fc_ir_operand_vreg(dest),
                           fc_ir_operand_imm(fcx_instr->u.const_op.value));
            return true;
        }
        
        case FCXIR_CONST_BIGINT: {
            // Bigint constants - use bigint operand type
            VirtualReg dest = fc_ir_lower_map_vreg(ctx, fcx_instr->u.const_bigint_op.dest);
            fc_ir_build_mov(ctx->current_block,
                           fc_ir_operand_vreg(dest),
                           fc_ir_operand_bigint(fcx_instr->u.const_bigint_op.limbs,
                                               fcx_instr->u.const_bigint_op.num_limbs));
            return true;
        }
        
        case FCXIR_MOV: {
            // Register-to-register move (not memory)
            VirtualReg dest = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.dest);
            VirtualReg src = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.src);
            fc_ir_build_mov(ctx->current_block,
                           fc_ir_operand_vreg(dest),
                           fc_ir_operand_vreg(src));
            return true;
        }
        
        case FCXIR_LOAD: {
            VirtualReg dest = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.dest);
            VirtualReg src = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.src);
            FcOperand mem_op = fc_ir_operand_mem(src, (VirtualReg){0}, 
                                              fcx_instr->u.load_store.offset, 1);
            fc_ir_build_mov(ctx->current_block,
                           fc_ir_operand_vreg(dest),
                           mem_op);
            return true;
        }
        
        case FCXIR_STORE: {
            VirtualReg dest = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.dest);
            VirtualReg src = fc_ir_lower_map_vreg(ctx, fcx_instr->u.load_store.src);
            FcOperand mem_op = fc_ir_operand_mem(dest, (VirtualReg){0},
                                              fcx_instr->u.load_store.offset, 1);
            fc_ir_build_mov(ctx->current_block,
                           mem_op,
                           fc_ir_operand_vreg(src));
            return true;
        }
        
        // Arithmetic and bitwise operations
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
            return fc_ir_lower_binary_op(ctx, fcx_instr);
        
        case FCXIR_NEG:
        case FCXIR_NOT:
            return fc_ir_lower_unary_op(ctx, fcx_instr);
        
        // Comparison operations
        case FCXIR_CMP_EQ:
        case FCXIR_CMP_NE:
        case FCXIR_CMP_LT:
        case FCXIR_CMP_LE:
        case FCXIR_CMP_GT:
        case FCXIR_CMP_GE:
            return fc_ir_lower_comparison(ctx, fcx_instr);
        
        // Memory allocation
        case FCXIR_ALLOC:
        case FCXIR_ARENA_ALLOC:
        case FCXIR_SLAB_ALLOC:
            return fc_ir_lower_alloc(ctx, fcx_instr);
        
        case FCXIR_DEALLOC: {
            // Deallocation - call _fcx_free runtime function
            VirtualReg ptr = fc_ir_lower_map_vreg(ctx, fcx_instr->u.unary_op.src);
            
            // Move ptr to rdi (first argument in System V AMD64 ABI)
            VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            fc_ir_build_mov(ctx->current_block,
                           fc_ir_operand_vreg(rdi_vreg),
                           fc_ir_operand_vreg(ptr));
            
            // Call _fcx_free (note the underscore prefix for runtime functions)
            fc_ir_build_call_external(ctx->current_block, ctx->fc_module, "_fcx_free");
            return true;
        }
        
        case FCXIR_STACK_ALLOC: {
            // Stack allocation - use alloca-like pattern
            // For now, just call _fcx_alloc (proper stack alloc would use RSP manipulation)
            VirtualReg size = fc_ir_lower_map_vreg(ctx, fcx_instr->u.alloc_op.size);
            VirtualReg result = fc_ir_lower_map_vreg(ctx, fcx_instr->u.alloc_op.dest);
            
            VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            VirtualReg rsi_vreg = {.id = 1002, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            VirtualReg rax_vreg = {.id = 1000, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(rdi_vreg), fc_ir_operand_vreg(size));
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(rsi_vreg), fc_ir_operand_imm(16)); // 16-byte align
            fc_ir_build_call_external(ctx->current_block, ctx->fc_module, "_fcx_alloc");
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(result), fc_ir_operand_vreg(rax_vreg));
            return true;
        }
        
        case FCXIR_ARENA_RESET: {
            // Arena reset - call _fcx_arena_reset(scope_id)
            VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(rdi_vreg), 
                           fc_ir_operand_imm(fcx_instr->u.arena_op.scope_id));
            fc_ir_build_call_external(ctx->current_block, ctx->fc_module, "_fcx_arena_reset");
            return true;
        }
        
        case FCXIR_SLAB_FREE: {
            // Slab free - call _fcx_slab_free(ptr, type_hash)
            VirtualReg ptr = fc_ir_lower_map_vreg(ctx, fcx_instr->u.slab_op.ptr);
            VirtualReg rdi_vreg = {.id = 1001, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            VirtualReg rsi_vreg = {.id = 1002, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
            
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(rdi_vreg), fc_ir_operand_vreg(ptr));
            fc_ir_build_mov(ctx->current_block, fc_ir_operand_vreg(rsi_vreg), 
                           fc_ir_operand_imm(fcx_instr->u.slab_op.type_hash));
            fc_ir_build_call_external(ctx->current_block, ctx->fc_module, "_fcx_slab_free");
            return true;
        }
        
        case FCXIR_PREFETCH: {
            // Prefetch for reading - emit PREFETCHT0 instruction
            VirtualReg ptr = fc_ir_lower_map_vreg(ctx, fcx_instr->u.unary_op.src);
            FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
            fc_ir_build_prefetch(ctx->current_block, mem_op, 0); // hint=0 for T0 (all cache levels)
            return true;
        }
        
        case FCXIR_PREFETCH_WRITE: {
            // Prefetch for writing - emit PREFETCHW instruction
            VirtualReg ptr = fc_ir_lower_map_vreg(ctx, fcx_instr->u.unary_op.src);
            FcOperand mem_op = fc_ir_operand_mem(ptr, (VirtualReg){0}, 0, 1);
            fc_ir_build_prefetch(ctx->current_block, mem_op, 1); // hint=1 for write
            return true;
        }
        
        // Atomic operations
        case FCXIR_ATOMIC_LOAD:
            return fc_ir_lower_atomic_load(ctx, fcx_instr);
        
        case FCXIR_ATOMIC_STORE:
            return fc_ir_lower_atomic_store(ctx, fcx_instr);
        
        case FCXIR_ATOMIC_SWAP:
            return fc_ir_lower_atomic_swap(ctx, fcx_instr);
        
        case FCXIR_ATOMIC_CAS:
            return fc_ir_lower_atomic_cas(ctx, fcx_instr);
        
        // Memory barriers
        case FCXIR_FENCE_FULL:
        case FCXIR_FENCE_ACQUIRE:
        case FCXIR_FENCE_RELEASE:
            return fc_ir_lower_fence(ctx, fcx_instr);
        
        // Syscall
        case FCXIR_SYSCALL:
            return fc_ir_lower_syscall(ctx, fcx_instr);
        
        // Inline assembly
        case FCXIR_INLINE_ASM:
            return fc_ir_lower_inline_asm(ctx, fcx_instr);
        
        // MMIO
        case FCXIR_MMIO_READ:
        case FCXIR_MMIO_WRITE:
            return fc_ir_lower_mmio(ctx, fcx_instr);
        
        // Pointer operations
        case FCXIR_PTR_ADD:
        case FCXIR_PTR_SUB:
            return fc_ir_lower_ptr_arithmetic(ctx, fcx_instr);
        
        // Control flow
        case FCXIR_BRANCH:
            return fc_ir_lower_branch(ctx, fcx_instr);
        
        case FCXIR_JUMP:
            return fc_ir_lower_jump(ctx, fcx_instr);
        
        case FCXIR_CALL:
            return fc_ir_lower_call(ctx, fcx_instr);
        
        case FCXIR_RETURN:
            return fc_ir_lower_return(ctx, fcx_instr);
        
        default:
            fc_ir_lower_set_error(ctx, "Unsupported FCx IR instruction");
            return false;
    }
}

// ============================================================================
// Block and Function Lowering
// ============================================================================

bool fc_ir_lower_block(FcIRLowerContext* ctx, const FcxIRBasicBlock* fcx_block) {
    if (!ctx || !fcx_block) return false;
    
    // Create corresponding FC IR block
    ctx->current_block = fc_ir_block_create(ctx->current_function, fcx_block->name);
    if (!ctx->current_block) {
        fc_ir_lower_set_error(ctx, "Failed to create FC IR block");
        return false;
    }
    
    // Preserve the FCx IR block ID so that jump targets match
    ctx->current_block->id = fcx_block->id;
    
    // Lower all instructions in the block
    for (uint32_t i = 0; i < fcx_block->instruction_count; i++) {
        if (!fc_ir_lower_instruction(ctx, &fcx_block->instructions[i])) {
            return false;
        }
    }
    
    return true;
}

bool fc_ir_lower_function(FcIRLowerContext* ctx, const FcxIRFunction* fcx_function) {
    if (!ctx || !fcx_function) return false;
    
    // Create corresponding FC IR function
    ctx->current_function = fc_ir_function_create(fcx_function->name, fcx_function->return_type);
    if (!ctx->current_function) {
        fc_ir_lower_set_error(ctx, "Failed to create FC IR function");
        return false;
    }
    
    // Copy parameter information
    if (fcx_function->parameter_count > 0 && fcx_function->parameters) {
        ctx->current_function->parameter_count = fcx_function->parameter_count;
        ctx->current_function->parameters = malloc(fcx_function->parameter_count * sizeof(VirtualReg));
        if (ctx->current_function->parameters) {
            for (uint8_t i = 0; i < fcx_function->parameter_count; i++) {
                ctx->current_function->parameters[i] = fcx_function->parameters[i];
            }
        }
    }
    
    // Lower all basic blocks
    for (uint32_t i = 0; i < fcx_function->block_count; i++) {
        if (!fc_ir_lower_block(ctx, &fcx_function->blocks[i])) {
            return false;
        }
    }
    
    // Compute stack frame layout
    fc_ir_compute_frame_layout(ctx->current_function);
    
    // Add function to module
    if (!fc_ir_module_add_function(ctx->fc_module, ctx->current_function)) {
        return false;
    }
    
    return true;
}

// ============================================================================
// Module Lowering
// ============================================================================

bool fc_ir_lower_module(FcIRLowerContext* ctx, const FcxIRModule* fcx_module) {
    if (!ctx || !fcx_module) return false;
    
    // Create FC IR module
    ctx->fc_module = fc_ir_module_create(fcx_module->name);
    if (!ctx->fc_module) {
        fc_ir_lower_set_error(ctx, "Failed to create FC IR module");
        return false;
    }
    
    // Detect CPU features
    CpuFeatures features = fc_ir_detect_cpu_features();
    fc_ir_module_set_cpu_features(ctx->fc_module, features);
    
    // Copy string literals from FCx IR to FC IR
    if (fcx_module->string_count > 0) {
        ctx->fc_module->string_literals = malloc(fcx_module->string_count * sizeof(FcIRStringLiteral));
        if (ctx->fc_module->string_literals) {
            ctx->fc_module->string_count = fcx_module->string_count;
            ctx->fc_module->string_capacity = fcx_module->string_count;
            for (uint32_t i = 0; i < fcx_module->string_count; i++) {
                ctx->fc_module->string_literals[i].id = fcx_module->string_literals[i].id;
                ctx->fc_module->string_literals[i].data = strdup(fcx_module->string_literals[i].data);
                ctx->fc_module->string_literals[i].length = fcx_module->string_literals[i].length;
            }
        }
    }
    
    // Lower all functions
    for (uint32_t i = 0; i < fcx_module->function_count; i++) {
        if (!fc_ir_lower_function(ctx, &fcx_module->functions[i])) {
            return false;
        }
    }
    
    return true;
}
