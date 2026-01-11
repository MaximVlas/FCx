#define _POSIX_C_SOURCE 200809L
#include "fc_ir_abi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void fc_ir_abi_init_sysv_amd64(SysVAMD64ABI* abi) {
    if (!abi) return;
    
    abi->int_arg_regs[0] = 1001;  // rdi
    abi->int_arg_regs[1] = 1002;  // rsi
    abi->int_arg_regs[2] = 1003;  // rdx
    abi->int_arg_regs[3] = 1007;  // rcx
    abi->int_arg_regs[4] = 1005;  // r8
    abi->int_arg_regs[5] = 1006;  // r9
    abi->int_arg_count = 6;
    abi->int_return_reg = 1000;   // rax
    abi->int_return_reg2 = 1003;  // rdx
    abi->callee_saved_mask = 0;
    abi->callee_saved_mask |= (1ULL << 0);  // rbx (vreg 1008)
    abi->callee_saved_mask |= (1ULL << 1);  // rbp (vreg 1009)
    abi->callee_saved_mask |= (1ULL << 2);  // r12 (vreg 1012)
    abi->callee_saved_mask |= (1ULL << 3);  // r13 (vreg 1013)
    abi->callee_saved_mask |= (1ULL << 4);  // r14 (vreg 1014)
    abi->callee_saved_mask |= (1ULL << 5);  // r15 (vreg 1015)
    
    abi->stack_alignment = 16;
    abi->red_zone_size = 128;
}

void fc_ir_abi_init_fastcall(FastcallABI* abi) {
    if (!abi) return;
    
    abi->int_arg_regs[0] = 1001;
    abi->int_arg_regs[1] = 1002;
    abi->int_arg_regs[2] = 1003;
    abi->int_arg_regs[3] = 1007;
    abi->int_arg_regs[4] = 1005;
    abi->int_arg_regs[5] = 1006;
    
    abi->int_return_reg = 1000;
    abi->callee_saved_mask = 0;
    abi->stack_alignment = 16;
}

void fc_ir_abi_init_syscall(SyscallABI* abi) {
    if (!abi) return;
    
    abi->syscall_num_reg = 1000;  // rax
    
    // Syscall argument registers: rdi, rsi, rdx, r10, r8, r9
    abi->arg_regs[0] = 1001;  // rdi
    abi->arg_regs[1] = 1002;  // rsi
    abi->arg_regs[2] = 1003;  // rdx
    abi->arg_regs[3] = 1004;  // r10 (note: different from regular calls)
    abi->arg_regs[4] = 1005;  // r8
    abi->arg_regs[5] = 1006;  // r9
    
    abi->return_reg = 1000;   // rax
}

// ============================================================================
// Red Zone Optimization
// ============================================================================

bool fc_ir_abi_can_use_red_zone(const FcIRFunction* function) {
    return fc_ir_can_use_red_zone(function);
}

int32_t fc_ir_abi_allocate_red_zone(StackFrame* frame, uint8_t size) {
    if (!frame || frame->red_zone_used + size > 128) {
        return -1;
    }
    
    int32_t offset = -(frame->red_zone_used + size);
    frame->red_zone_used += size;
    frame->uses_red_zone = true;
    
    return offset;
}

// ============================================================================
// Stack Frame Layout
// ============================================================================

void fc_ir_abi_compute_frame_layout(FcIRFunction* function) {
    fc_ir_compute_frame_layout(function);
}

int32_t fc_ir_abi_allocate_spill_slot(StackFrame* frame, uint8_t size) {
    if (!frame) return -1;
    
    // Align spill area
    int32_t aligned_size = (frame->spill_area_size + 7) & ~7;
    int32_t offset = -(frame->local_area_size + aligned_size + size);
    
    frame->spill_area_size = aligned_size + size;
    
    return offset;
}

int32_t fc_ir_abi_allocate_local(StackFrame* frame, uint8_t size, uint8_t alignment) {
    return fc_ir_allocate_stack_slot(frame, size, alignment);
}

// ============================================================================
// Function Prologue Generation
// ============================================================================

bool fc_ir_abi_generate_prologue(FcIRFunction* function, FcIRBasicBlock* entry_block) {
    if (!function || !entry_block) return false;
    
    StackFrame* frame = &function->stack_frame;
    
    // Check if we can use red zone optimization
    if (fc_ir_abi_can_use_red_zone(function)) {
        return fc_ir_abi_generate_leaf_prologue(function, entry_block);
    }
    
    // Standard prologue with frame pointer
    // push rbp
    VirtualReg rbp = {.id = 1009, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    fc_ir_build_push(entry_block, fc_ir_operand_vreg(rbp));
    
    // mov rbp, rsp
    VirtualReg rsp = {.id = 1010, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    fc_ir_build_mov(entry_block, 
                   fc_ir_operand_vreg(rbp),
                   fc_ir_operand_vreg(rsp));
    
    // sub rsp, frame_size (allocate stack frame)
    if (frame->frame_size > 0) {
        fc_ir_build_binary_op(entry_block, FCIR_SUB,
                             fc_ir_operand_vreg(rsp),
                             fc_ir_operand_imm(frame->frame_size));
    }
    
    // Save callee-saved registers
    uint64_t saved_mask = frame->saved_regs_mask;
    if (saved_mask != 0) {
        // Push callee-saved registers
        // Map bit positions to actual register IDs
        uint32_t callee_saved_regs[] = {1008, 1009, 1012, 1013, 1014, 1015};  // rbx, rbp, r12-r15
        
        for (size_t i = 0; i < sizeof(callee_saved_regs) / sizeof(callee_saved_regs[0]); i++) {
            if (saved_mask & (1ULL << i)) {
                VirtualReg reg = {
                    .id = callee_saved_regs[i],
                    .type = VREG_TYPE_I64,
                    .size = 8,
                    .flags = 0
                };
                fc_ir_build_push(entry_block, fc_ir_operand_vreg(reg));
            }
        }
    }
    
    return true;
}

bool fc_ir_abi_generate_leaf_prologue(FcIRFunction* function, FcIRBasicBlock* entry_block) {
    if (!function || !entry_block) return false;
    
    // Leaf function using red zone - no prologue needed!
    // The red zone (128 bytes below RSP) can be used without adjusting RSP
    
    (void)entry_block;  // No instructions to emit
    
    return true;
}

// ============================================================================
// Function Epilogue Generation
// ============================================================================

bool fc_ir_abi_generate_epilogue(FcIRFunction* function, FcIRBasicBlock* exit_block) {
    if (!function || !exit_block) return false;
    
    StackFrame* frame = &function->stack_frame;
    
    // Check if we used red zone optimization
    if (frame->uses_red_zone) {
        return fc_ir_abi_generate_leaf_epilogue(function, exit_block);
    }
    
    // Restore callee-saved registers (in reverse order)
    uint64_t saved_mask = frame->saved_regs_mask;
    if (saved_mask != 0) {
        // Map bit positions to actual register IDs (in reverse order for popping)
        uint32_t callee_saved_regs[] = {1015, 1014, 1013, 1012, 1009, 1008};  // r15-r12, rbp, rbx
        
        for (size_t i = 0; i < sizeof(callee_saved_regs) / sizeof(callee_saved_regs[0]); i++) {
            // Check corresponding bit position (5, 4, 3, 2, 1, 0)
            size_t bit_pos = sizeof(callee_saved_regs) / sizeof(callee_saved_regs[0]) - 1 - i;
            if (saved_mask & (1ULL << bit_pos)) {
                VirtualReg reg = {
                    .id = callee_saved_regs[i],
                    .type = VREG_TYPE_I64,
                    .size = 8,
                    .flags = 0
                };
                fc_ir_build_pop(exit_block, fc_ir_operand_vreg(reg));
            }
        }
    }
    
    // Standard epilogue
    // mov rsp, rbp
    VirtualReg rbp = {.id = 1009, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    VirtualReg rsp = {.id = 1010, .type = VREG_TYPE_I64, .size = 8, .flags = 0};
    
    fc_ir_build_mov(exit_block,
                   fc_ir_operand_vreg(rsp),
                   fc_ir_operand_vreg(rbp));
    
    // pop rbp
    fc_ir_build_pop(exit_block, fc_ir_operand_vreg(rbp));
    
    // ret
    fc_ir_build_ret(exit_block);
    
    return true;
}

bool fc_ir_abi_generate_leaf_epilogue(FcIRFunction* function, FcIRBasicBlock* exit_block) {
    if (!function || !exit_block) return false;
    
    // Leaf function using red zone - just return
    fc_ir_build_ret(exit_block);
    
    return true;
}

// ============================================================================
// Parameter Setup
// ============================================================================

bool fc_ir_abi_setup_parameters(FcIRFunction* function, FcIRBasicBlock* entry_block) {
    if (!function || !entry_block) return false;
    
    SysVAMD64ABI abi;
    fc_ir_abi_init_sysv_amd64(&abi);
    
    // Move parameters from ABI registers to virtual registers
    for (uint8_t i = 0; i < function->parameter_count && i < abi.int_arg_count; i++) {
        VirtualReg param = function->parameters[i];
        VirtualReg abi_reg = {
            .id = abi.int_arg_regs[i],
            .type = param.type,
            .size = param.size,
            .flags = 0
        };
        
        fc_ir_build_mov(entry_block,
                       fc_ir_operand_vreg(param),
                       fc_ir_operand_vreg(abi_reg));
    }
    
    return true;
}

bool fc_ir_abi_setup_call_args(FcIRBasicBlock* block, VirtualReg* args, uint8_t arg_count) {
    if (!block || !args) return false;
    
    SysVAMD64ABI abi;
    fc_ir_abi_init_sysv_amd64(&abi);
    
    // Move arguments to ABI registers
    for (uint8_t i = 0; i < arg_count && i < abi.int_arg_count; i++) {
        VirtualReg abi_reg = {
            .id = abi.int_arg_regs[i],
            .type = args[i].type,
            .size = args[i].size,
            .flags = 0
        };
        
        fc_ir_build_mov(block,
                       fc_ir_operand_vreg(abi_reg),
                       fc_ir_operand_vreg(args[i]));
    }
    
    // Handle stack-passed arguments (beyond 6 parameters)
    // System V AMD64 ABI: arguments 7+ are passed on the stack
    // They are pushed in reverse order (right to left)
    if (arg_count > abi.int_arg_count) {
        // Push arguments in reverse order
        for (size_t i = arg_count; i > abi.int_arg_count; i--) {
            size_t arg_idx = i - 1;
            // PUSH instruction for stack argument
            FcOperand arg_operand = fc_ir_operand_vreg(args[arg_idx]);
            fc_ir_build_push(block, arg_operand);
        }
    }
    
    return true;
}

bool fc_ir_abi_setup_syscall_args(FcIRBasicBlock* block, VirtualReg* args, uint8_t arg_count) {
    if (!block) return false;
    
    SyscallABI abi;
    fc_ir_abi_init_syscall(&abi);
    
    // Move arguments to syscall ABI registers
    // Syscall uses: rdi, rsi, rdx, r10, r8, r9 (note: r10 instead of rcx)
    for (uint8_t i = 0; i < arg_count && i < 6; i++) {
        VirtualReg abi_reg = {
            .id = abi.arg_regs[i],
            .type = args[i].type,
            .size = args[i].size,
            .flags = 0
        };
        
        fc_ir_build_mov(block,
                       fc_ir_operand_vreg(abi_reg),
                       fc_ir_operand_vreg(args[i]));
    }
    
    return true;
}

// ============================================================================
// Register Allocation Hints
// ============================================================================

void fc_ir_abi_get_param_hints(const FcIRFunction* function, uint32_t* reg_hints, uint8_t* hint_count) {
    if (!function || !reg_hints || !hint_count) return;
    
    SysVAMD64ABI abi;
    fc_ir_abi_init_sysv_amd64(&abi);
    
    *hint_count = 0;
    
    for (uint8_t i = 0; i < function->parameter_count && i < abi.int_arg_count; i++) {
        reg_hints[*hint_count] = abi.int_arg_regs[i];
        (*hint_count)++;
    }
}

uint64_t fc_ir_abi_get_callee_saved_mask(const FcIRFunction* function) {
    if (!function) return 0;
    
    SysVAMD64ABI abi;
    fc_ir_abi_init_sysv_amd64(&abi);
    
    return abi.callee_saved_mask;
}

// ============================================================================
// Multiple Calling Convention Support
// ============================================================================

bool fc_ir_abi_generate_prologue_for_convention(FcIRFunction* function, FcIRBasicBlock* entry_block) {
    if (!function || !entry_block) return false;
    
    switch (function->calling_convention) {
        case CALLING_CONV_SYSV_AMD64:
            return fc_ir_abi_generate_prologue(function, entry_block);
            
        case CALLING_CONV_FASTCALL:
            // Fastcall uses same prologue as System V for now
            return fc_ir_abi_generate_prologue(function, entry_block);
            
        case CALLING_CONV_SYSCALL:
            // Syscalls don't need prologue/epilogue
            return true;
            
        case CALLING_CONV_VECTORCALL:
            // Vectorcall uses same prologue as System V for now
            return fc_ir_abi_generate_prologue(function, entry_block);
            
        default:
            return false;
    }
}

bool fc_ir_abi_generate_epilogue_for_convention(FcIRFunction* function, FcIRBasicBlock* exit_block) {
    if (!function || !exit_block) return false;
    
    switch (function->calling_convention) {
        case CALLING_CONV_SYSV_AMD64:
            return fc_ir_abi_generate_epilogue(function, exit_block);
            
        case CALLING_CONV_FASTCALL:
            return fc_ir_abi_generate_epilogue(function, exit_block);
            
        case CALLING_CONV_SYSCALL:
            // Syscalls don't need prologue/epilogue
            return true;
            
        case CALLING_CONV_VECTORCALL:
            return fc_ir_abi_generate_epilogue(function, exit_block);
            
        default:
            return false;
    }
}

bool fc_ir_abi_setup_call_for_convention(FcIRBasicBlock* block, const char* function_name,
                                         VirtualReg* args, uint8_t arg_count) {
    if (!block || !function_name) return false;
    
    // Determine calling convention based on function name prefix
    // _fcx_ prefix: FCx internal functions use System V AMD64
    // sys_ prefix: System calls use syscall convention
    // Otherwise: Default to System V AMD64 (standard for Linux x86_64)
    
    if (strncmp(function_name, "sys_", 4) == 0) {
        // Syscall convention - use syscall ABI
        return fc_ir_abi_setup_syscall_args(block, args, arg_count);
    }
    
    // Default: System V AMD64 calling convention
    return fc_ir_abi_setup_call_args(block, args, arg_count);
}
