#ifndef FC_IR_LOWER_H
#define FC_IR_LOWER_H

#include "fcx_ir.h"
#include "fc_ir.h"

// FC IR Lowering - Converts FCx IR (high-level) to FC IR (low-level)
// This module handles the transformation from operator-centric IR to x86_64-like IR

// ============================================================================
// Lowering Context
// ============================================================================

typedef struct {
    FcIRModule* fc_module;
    const FcxIRModule* fcx_module;  // Reference to source FCx module
    FcIRFunction* current_function;
    FcIRBasicBlock* current_block;
    
    // Virtual register mapping (FCx IR vreg -> FC IR vreg)
    VirtualReg* vreg_map;
    size_t vreg_map_capacity;
    
    // Label mapping (FCx IR label -> FC IR label)
    uint32_t* label_map;
    size_t label_map_capacity;
    
    // Error tracking
    char* error_message;
    bool has_error;
} FcIRLowerContext;

// ============================================================================
// Lowering Interface
// ============================================================================

// Create and destroy lowering context
FcIRLowerContext* fc_ir_lower_create(void);
void fc_ir_lower_destroy(FcIRLowerContext* ctx);

// Lower entire module
bool fc_ir_lower_module(FcIRLowerContext* ctx, const FcxIRModule* fcx_module);

// Lower individual components
bool fc_ir_lower_function(FcIRLowerContext* ctx, const FcxIRFunction* fcx_function);
bool fc_ir_lower_block(FcIRLowerContext* ctx, const FcxIRBasicBlock* fcx_block);
bool fc_ir_lower_instruction(FcIRLowerContext* ctx, const FcxIRInstruction* fcx_instr);

// ============================================================================
// Specific Lowering Functions
// ============================================================================

// Syscall lowering: FCxIR::Syscall -> System V ABI register setup + syscall
bool fc_ir_lower_syscall(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Memory allocation: FCxIR::Alloc -> FCIR::Call(_fcx_alloc)
bool fc_ir_lower_alloc(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Pointer arithmetic with three-pointer type system
bool fc_ir_lower_ptr_arithmetic(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Atomic operations: FCxIR::Atomic* -> LOCK-prefixed instructions
bool fc_ir_lower_atomic_load(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_atomic_store(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_atomic_swap(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_atomic_cas(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Memory barriers: FCxIR::Fence* -> MFENCE/LFENCE/SFENCE
bool fc_ir_lower_fence(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Arithmetic and bitwise operations
bool fc_ir_lower_binary_op(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_unary_op(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Comparison operations
bool fc_ir_lower_comparison(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// Control flow
bool fc_ir_lower_branch(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_jump(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_call(FcIRLowerContext* ctx, const FcxIRInstruction* instr);
bool fc_ir_lower_return(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// MMIO operations
bool fc_ir_lower_mmio(FcIRLowerContext* ctx, const FcxIRInstruction* instr);

// ============================================================================
// Helper Functions
// ============================================================================

// Virtual register mapping
VirtualReg fc_ir_lower_map_vreg(FcIRLowerContext* ctx, VirtualReg fcx_vreg);
uint32_t fc_ir_lower_map_label(FcIRLowerContext* ctx, uint32_t fcx_label);

// Error handling
void fc_ir_lower_set_error(FcIRLowerContext* ctx, const char* message);
const char* fc_ir_lower_get_error(const FcIRLowerContext* ctx);

#endif // FC_IR_LOWER_H
