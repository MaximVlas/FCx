#ifndef FC_IR_ABI_H
#define FC_IR_ABI_H

#include "fc_ir.h"

// FC IR ABI Management - Advanced stack frame and calling convention support
// Handles System V AMD64, fastcall, syscall, and vectorcall conventions

// ============================================================================
// Calling Convention Definitions
// ============================================================================

// System V AMD64 ABI (default for Linux x86_64)
typedef struct {
    // Integer/pointer argument registers (in order)
    uint32_t int_arg_regs[6];      // rdi, rsi, rdx, rcx, r8, r9
    uint32_t int_arg_count;
    
    // Return value registers
    uint32_t int_return_reg;       // rax
    uint32_t int_return_reg2;      // rdx (for 128-bit returns)
    
    // Callee-saved registers
    uint64_t callee_saved_mask;    // rbx, rbp, r12-r15
    
    // Stack alignment
    uint8_t stack_alignment;       // 16 bytes
    
    // Red zone
    uint8_t red_zone_size;         // 128 bytes
} SysVAMD64ABI;

// Fastcall convention (register-only parameters)
typedef struct {
    uint32_t int_arg_regs[6];
    uint32_t int_return_reg;
    uint64_t callee_saved_mask;
    uint8_t stack_alignment;
} FastcallABI;

// Syscall convention (Linux syscall ABI)
typedef struct {
    uint32_t syscall_num_reg;      // rax
    uint32_t arg_regs[6];          // rdi, rsi, rdx, r10, r8, r9
    uint32_t return_reg;           // rax
} SyscallABI;

// ============================================================================
// Function Prologue/Epilogue Generation
// ============================================================================

// Generate function prologue with red-zone optimization
bool fc_ir_abi_generate_prologue(FcIRFunction* function, FcIRBasicBlock* entry_block);

// Generate function epilogue
bool fc_ir_abi_generate_epilogue(FcIRFunction* function, FcIRBasicBlock* exit_block);

// Generate optimized prologue for leaf functions using red zone
bool fc_ir_abi_generate_leaf_prologue(FcIRFunction* function, FcIRBasicBlock* entry_block);

// Generate epilogue for leaf functions
bool fc_ir_abi_generate_leaf_epilogue(FcIRFunction* function, FcIRBasicBlock* exit_block);

// ============================================================================
// Calling Convention Setup
// ============================================================================

// Initialize System V AMD64 ABI
void fc_ir_abi_init_sysv_amd64(SysVAMD64ABI* abi);

// Initialize Fastcall ABI
void fc_ir_abi_init_fastcall(FastcallABI* abi);

// Initialize Syscall ABI
void fc_ir_abi_init_syscall(SyscallABI* abi);

// ============================================================================
// Parameter Passing
// ============================================================================

// Setup function parameters according to calling convention
bool fc_ir_abi_setup_parameters(FcIRFunction* function, FcIRBasicBlock* entry_block);

// Setup call arguments according to calling convention
bool fc_ir_abi_setup_call_args(FcIRBasicBlock* block, VirtualReg* args, uint8_t arg_count);

// Setup syscall arguments according to syscall convention
bool fc_ir_abi_setup_syscall_args(FcIRBasicBlock* block, VirtualReg* args, uint8_t arg_count);

// ============================================================================
// Register Allocation Hints
// ============================================================================

// Get register allocation hints for function parameters
void fc_ir_abi_get_param_hints(const FcIRFunction* function, uint32_t* reg_hints, uint8_t* hint_count);

// Get callee-saved registers that need to be preserved
uint64_t fc_ir_abi_get_callee_saved_mask(const FcIRFunction* function);

// ============================================================================
// Stack Frame Layout
// ============================================================================

// Compute optimal stack frame layout with cache-line alignment
void fc_ir_abi_compute_frame_layout(FcIRFunction* function);

// Allocate space for spilled registers
int32_t fc_ir_abi_allocate_spill_slot(StackFrame* frame, uint8_t size);

// Allocate space for local variables
int32_t fc_ir_abi_allocate_local(StackFrame* frame, uint8_t size, uint8_t alignment);

// ============================================================================
// Red Zone Optimization
// ============================================================================

// Check if function can use red zone
bool fc_ir_abi_can_use_red_zone(const FcIRFunction* function);

// Allocate in red zone for leaf functions
int32_t fc_ir_abi_allocate_red_zone(StackFrame* frame, uint8_t size);

// ============================================================================
// Multiple Calling Convention Support
// ============================================================================

// Generate prologue/epilogue based on function's calling convention
bool fc_ir_abi_generate_prologue_for_convention(FcIRFunction* function, FcIRBasicBlock* entry_block);
bool fc_ir_abi_generate_epilogue_for_convention(FcIRFunction* function, FcIRBasicBlock* exit_block);

// Setup call based on target function's calling convention
bool fc_ir_abi_setup_call_for_convention(FcIRBasicBlock* block, const char* function_name, 
                                         VirtualReg* args, uint8_t arg_count);

#endif // FC_IR_ABI_H
