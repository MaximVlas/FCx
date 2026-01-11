#ifndef FC_IR_H
#define FC_IR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fcx_ir.h"

// FC IR (Low-Level "FishyComplexion" Intermediate Representation)
// This IR is close to x86_64 assembly but uses virtual registers
// It's the final step before register allocation and assembly generation

// ============================================================================
// CPU Feature Detection
// ============================================================================

typedef struct {
    uint64_t features;          // Bit-packed feature flags
    uint16_t vector_width;      // Preferred vector width (128, 256, 512)
    uint8_t cache_line_size;    // L1 cache line size
    uint8_t red_zone_size;      // Available red zone (0-128 bytes)
    uint8_t alignment_pref;     // Preferred stack alignment
} __attribute__((packed)) CpuFeatures;

// Feature flags using bit positions for O(1) testing
#define CPU_FEATURE_SSE2        (1ULL << 0)
#define CPU_FEATURE_SSE3        (1ULL << 1)
#define CPU_FEATURE_SSSE3       (1ULL << 2)
#define CPU_FEATURE_SSE4_1      (1ULL << 3)
#define CPU_FEATURE_SSE4_2      (1ULL << 4)
#define CPU_FEATURE_AVX         (1ULL << 5)
#define CPU_FEATURE_AVX2        (1ULL << 6)
#define CPU_FEATURE_AVX512F     (1ULL << 7)
#define CPU_FEATURE_BMI1        (1ULL << 8)
#define CPU_FEATURE_BMI2        (1ULL << 9)
#define CPU_FEATURE_POPCNT      (1ULL << 10)
#define CPU_FEATURE_LZCNT       (1ULL << 11)

// ============================================================================
// FC IR Opcodes - Low-Level x86_64-like Instructions
// ============================================================================

typedef enum {
    // Data movement
    FCIR_MOV = 0,
    FCIR_MOVZX,
    FCIR_MOVSX,
    FCIR_LEA,
    FCIR_PUSH,
    FCIR_POP,
    
    // Arithmetic
    FCIR_ADD,
    FCIR_SUB,
    FCIR_IMUL,
    FCIR_IDIV,
    FCIR_NEG,
    FCIR_INC,
    FCIR_DEC,
    
    // Bitwise
    FCIR_AND,
    FCIR_OR,
    FCIR_XOR,
    FCIR_NOT,
    FCIR_TEST,
    
    // Shift and rotate
    FCIR_SHL,
    FCIR_SHR,
    FCIR_SAR,
    FCIR_ROL,
    FCIR_ROR,
    
    // Comparison
    FCIR_CMP,
    
    // Memory barriers and fencing
    FCIR_MFENCE,
    FCIR_LFENCE,
    FCIR_SFENCE,
    
    // Cache operations
    FCIR_PREFETCHT0,
    FCIR_PREFETCHT1,
    FCIR_PREFETCHT2,
    FCIR_PREFETCHNTA,
    FCIR_PREFETCHW,
    
    // Atomic operations (with LOCK prefix)
    FCIR_LOCK,
    FCIR_CMPXCHG,
    FCIR_XCHG,
    FCIR_XADD,
    
    // Bitfield operations
    FCIR_BTS,
    FCIR_BTR,
    FCIR_BTC,
    FCIR_BSF,
    FCIR_BSR,
    
    // Control flow
    FCIR_JMP,
    FCIR_JE,
    FCIR_JNE,
    FCIR_JL,
    FCIR_JLE,
    FCIR_JG,
    FCIR_JGE,
    FCIR_JA,
    FCIR_JB,
    FCIR_JAE,
    FCIR_JBE,
    
    // Function calls
    FCIR_CALL,
    FCIR_RET,
    FCIR_SYSCALL,
    
    // Labels and directives
    FCIR_LABEL,
    FCIR_ALIGN,
    
    // Stack frame management
    FCIR_ENTER,
    FCIR_LEAVE,
    
    // Inline assembly
    FCIR_INLINE_ASM,
    
    FCIR_OPCODE_COUNT
} FcIROpcode;

// ============================================================================
// Operand Types
// ============================================================================

typedef enum {
    FC_OPERAND_VREG = 0,       // Virtual register
    FC_OPERAND_IMMEDIATE,      // Immediate value
    FC_OPERAND_BIGINT,         // Bigint immediate (> 64 bits, up to 1024 bits)
    FC_OPERAND_MEMORY,         // Memory operand [base + index*scale + disp]
    FC_OPERAND_LABEL,          // Label reference
    FC_OPERAND_STACK_SLOT,     // Stack slot [rbp - offset]
    FC_OPERAND_EXTERNAL_FUNC,  // External function name
} FcOperandType;

// Bigint operand structure (for values > 64 bits)
typedef struct {
    uint64_t limbs[16];        // Up to 1024 bits (16 x 64-bit limbs), little-endian
    uint8_t num_limbs;         // Number of limbs used (1-16)
} FcBigintOperand;

// Memory operand structure
typedef struct {
    VirtualReg base;           // Base register (0 if none)
    VirtualReg index;          // Index register (0 if none)
    int32_t displacement;      // Memory displacement
    uint8_t scale;             // Scale factor (1, 2, 4, 8)
    bool is_rip_relative;      // RIP-relative addressing
} FcMemoryOperand;

// Stack slot structure
typedef struct {
    int32_t offset;            // Offset from frame pointer (negative)
    uint8_t size;              // Size in bytes
    uint8_t alignment;         // Alignment requirement
} StackSlot;

// Generic operand structure
typedef struct {
    FcOperandType type;
    
    union {
        VirtualReg vreg;
        int64_t immediate;
        FcBigintOperand bigint;
        FcMemoryOperand memory;
        uint32_t label_id;
        StackSlot stack_slot;
        uint32_t external_func_id;  // Index into external function table
    } u;
} FcOperand;

// ============================================================================
// FC IR Instruction Structure
// ============================================================================

typedef struct {
    FcIROpcode opcode;
    uint8_t operand_count;
    uint8_t flags;             // Instruction flags (lock prefix, etc.)
    uint32_t line_number;      // Source line for debugging
    
    // Flexible operand array (up to 3 operands for most instructions)
    FcOperand operands[3];
    
    // Optional: CPU feature requirements
    uint64_t required_features;
} FcIRInstruction;

// Instruction flags
#define FCIR_FLAG_LOCK          (1 << 0)  // LOCK prefix for atomic operations
#define FCIR_FLAG_REP           (1 << 1)  // REP prefix
#define FCIR_FLAG_VOLATILE      (1 << 2)  // Volatile memory access
#define FCIR_FLAG_RED_ZONE      (1 << 3)  // Uses red zone

// ============================================================================
// Stack Frame Management
// ============================================================================

typedef struct {
    int32_t frame_size;        // Total frame size in bytes
    int32_t local_area_size;   // Size of local variables
    int32_t spill_area_size;   // Size of register spill area
    int32_t param_area_size;   // Size of parameter area for calls
    
    uint8_t alignment;         // Stack alignment (16 bytes for x86_64)
    bool uses_red_zone;        // Whether function uses red zone
    bool is_leaf;              // Leaf function (no calls)
    bool needs_frame_pointer;  // Whether frame pointer is needed
    
    // Red zone optimization
    int32_t red_zone_used;     // Bytes used in red zone (0-128)
    
    // Saved registers
    uint64_t saved_regs_mask;  // Bitmask of saved callee-saved registers
    int32_t saved_regs_size;   // Size of saved registers area
} StackFrame;

// ============================================================================
// Basic Block Structure
// ============================================================================

typedef struct FcIRBasicBlock {
    uint32_t id;
    const char* name;
    
    FcIRInstruction* instructions;
    uint32_t instruction_count;
    uint32_t instruction_capacity;
    
    uint32_t* successors;
    uint8_t successor_count;
    
    uint32_t* predecessors;
    uint8_t predecessor_count;
    
    bool is_entry;
    bool is_exit;
} FcIRBasicBlock;

// ============================================================================
// Function Structure
// ============================================================================

typedef struct {
    const char* name;
    VirtualReg* parameters;
    uint8_t parameter_count;
    VRegType return_type;
    
    FcIRBasicBlock* blocks;
    uint32_t block_count;
    uint32_t block_capacity;
    
    StackFrame stack_frame;
    
    uint32_t next_vreg_id;
    uint32_t next_label_id;
    uint32_t next_block_id;
    
    // Calling convention
    enum {
        CALLING_CONV_SYSV_AMD64,
        CALLING_CONV_FASTCALL,
        CALLING_CONV_SYSCALL,
        CALLING_CONV_VECTORCALL
    } calling_convention;
} FcIRFunction;

// ============================================================================
// Module Structure
// ============================================================================

// String literal for data section
typedef struct {
    uint32_t id;
    const char* data;
    size_t length;
} FcIRStringLiteral;

typedef struct {
    const char* name;
    FcIRFunction* functions;
    uint32_t function_count;
    uint32_t function_capacity;
    
    // String literals for data section
    FcIRStringLiteral* string_literals;
    uint32_t string_count;
    uint32_t string_capacity;
    
    // External function names for calls
    const char** external_functions;
    uint32_t external_func_count;
    uint32_t external_func_capacity;
    
    CpuFeatures cpu_features;
} FcIRModule;

// ============================================================================
// FC IR Builder Interface
// ============================================================================

// Module management
FcIRModule* fc_ir_module_create(const char* name);
void fc_ir_module_destroy(FcIRModule* module);
void fc_ir_module_set_cpu_features(FcIRModule* module, CpuFeatures features);
uint32_t fc_ir_module_add_external_func(FcIRModule* module, const char* func_name);

// Function management
FcIRFunction* fc_ir_function_create(const char* name, VRegType return_type);
void fc_ir_function_destroy(FcIRFunction* function);
bool fc_ir_module_add_function(FcIRModule* module, FcIRFunction* function);

// Basic block management
FcIRBasicBlock* fc_ir_block_create(FcIRFunction* function, const char* name);
void fc_ir_block_add_successor(FcIRBasicBlock* block, uint32_t successor_id);

// Stack frame management
void fc_ir_init_stack_frame(StackFrame* frame);
int32_t fc_ir_allocate_stack_slot(StackFrame* frame, uint8_t size, uint8_t alignment);
bool fc_ir_can_use_red_zone(const FcIRFunction* function);
void fc_ir_compute_frame_layout(FcIRFunction* function);

// Operand creation helpers
FcOperand fc_ir_operand_vreg(VirtualReg vreg);
FcOperand fc_ir_operand_imm(int64_t value);
FcOperand fc_ir_operand_bigint(const uint64_t* limbs, uint8_t num_limbs);
FcOperand fc_ir_operand_mem(VirtualReg base, VirtualReg index, int32_t disp, uint8_t scale);
FcOperand fc_ir_operand_label(uint32_t label_id);
FcOperand fc_ir_operand_stack_slot(int32_t offset, uint8_t size);
FcOperand fc_ir_operand_external_func(uint32_t func_id);

// Instruction building
void fc_ir_build_mov(FcIRBasicBlock* block, FcOperand dest, FcOperand src);
void fc_ir_build_lea(FcIRBasicBlock* block, FcOperand dest, FcOperand src);
void fc_ir_build_push(FcIRBasicBlock* block, FcOperand src);
void fc_ir_build_pop(FcIRBasicBlock* block, FcOperand dest);
void fc_ir_build_binary_op(FcIRBasicBlock* block, FcIROpcode opcode, FcOperand dest, FcOperand src);
void fc_ir_build_unary_op(FcIRBasicBlock* block, FcIROpcode opcode, FcOperand operand);
void fc_ir_build_cmp(FcIRBasicBlock* block, FcOperand left, FcOperand right);
void fc_ir_build_test(FcIRBasicBlock* block, FcOperand left, FcOperand right);

// Control flow
void fc_ir_build_jmp(FcIRBasicBlock* block, uint32_t label_id);
void fc_ir_build_jcc(FcIRBasicBlock* block, FcIROpcode condition, uint32_t label_id);
void fc_ir_build_call(FcIRBasicBlock* block, const char* function);
void fc_ir_build_call_external(FcIRBasicBlock* block, FcIRModule* module, const char* function);
void fc_ir_build_ret(FcIRBasicBlock* block);
void fc_ir_build_syscall(FcIRBasicBlock* block);

// Atomic operations
void fc_ir_build_lock_prefix(FcIRBasicBlock* block);
void fc_ir_build_cmpxchg(FcIRBasicBlock* block, FcOperand dest, FcOperand src, bool locked);
void fc_ir_build_xchg(FcIRBasicBlock* block, FcOperand dest, FcOperand src, bool locked);
void fc_ir_build_xadd(FcIRBasicBlock* block, FcOperand dest, FcOperand src, bool locked);

// Memory barriers
void fc_ir_build_mfence(FcIRBasicBlock* block);
void fc_ir_build_lfence(FcIRBasicBlock* block);
void fc_ir_build_sfence(FcIRBasicBlock* block);

// Cache operations
void fc_ir_build_prefetch(FcIRBasicBlock* block, FcOperand addr, int hint);

// Inline assembly
void fc_ir_build_inline_asm_raw(FcIRBasicBlock* block, int64_t asm_data_ptr);

// Stack frame operations
void fc_ir_build_enter(FcIRBasicBlock* block, uint16_t frame_size);
void fc_ir_build_leave(FcIRBasicBlock* block);

// Labels
void fc_ir_build_label(FcIRBasicBlock* block, uint32_t label_id);

// CPU feature detection
CpuFeatures fc_ir_detect_cpu_features(void);
bool fc_ir_has_feature(const CpuFeatures* features, uint64_t feature_flag);

// Debugging and printing
void fc_ir_print_instruction(const FcIRInstruction* instr);
void fc_ir_print_block(const FcIRBasicBlock* block);
void fc_ir_print_function(const FcIRFunction* function);
void fc_ir_print_module(const FcIRModule* module);

#endif // FC_IR_H
