#ifndef FCX_IR_H
#define FCX_IR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// FCx IR (High-Level "FCx" Intermediate Representation)
// This IR desugars the 275+ operators into a regular, LLVM-like IR for optimization and analysis

// ============================================================================
// Virtual Register System
// ============================================================================

typedef enum {
    VREG_TYPE_I8 = 0,
    VREG_TYPE_I16,
    VREG_TYPE_I32,
    VREG_TYPE_I64,
    VREG_TYPE_I128,     // 128-bit signed integer (bigint)
    VREG_TYPE_I256,     // 256-bit signed integer (bigint)
    VREG_TYPE_I512,     // 512-bit signed integer (bigint)
    VREG_TYPE_I1024,    // 1024-bit signed integer (bigint)
    VREG_TYPE_U8,
    VREG_TYPE_U16,
    VREG_TYPE_U32,
    VREG_TYPE_U64,
    VREG_TYPE_U128,     // 128-bit unsigned integer (bigint)
    VREG_TYPE_U256,     // 256-bit unsigned integer (bigint)
    VREG_TYPE_U512,     // 512-bit unsigned integer (bigint)
    VREG_TYPE_U1024,    // 1024-bit unsigned integer (bigint)
    VREG_TYPE_F32,
    VREG_TYPE_F64,
    VREG_TYPE_PTR,      // ptr<T> - typed pointer
    VREG_TYPE_RAWPTR,   // rawptr - opaque pointer
    VREG_TYPE_BYTEPTR,  // byteptr - byte pointer
    VREG_TYPE_BOOL,
    VREG_TYPE_VOID,
    VREG_TYPE_COUNT
} VRegType;

typedef struct {
    uint32_t id;           // Virtual register ID (%v1, %v2, etc.)
    VRegType type;         // Register type
    uint8_t size;          // Size in bytes
    uint16_t flags;        // Additional flags for optimization
} VirtualReg;

// ============================================================================
// FCx IR Opcodes - High-Level Operator-Centric Instructions
// ============================================================================

typedef enum {
    // Constants and loads/stores
    FCXIR_CONST = 0,
    FCXIR_CONST_BIGINT,  // Bigint constant (> 64 bits, up to 1024 bits)
    FCXIR_LOAD,
    FCXIR_STORE,
    FCXIR_LOAD_VOLATILE,
    FCXIR_STORE_VOLATILE,
    FCXIR_MOV,  // Register-to-register move (not memory)
    FCXIR_LOAD_GLOBAL,   // Load from global variable
    FCXIR_STORE_GLOBAL,  // Store to global variable
    
    // Arithmetic operations
    FCXIR_ADD,
    FCXIR_SUB,
    FCXIR_MUL,
    FCXIR_DIV,
    FCXIR_MOD,
    FCXIR_NEG,
    
    // Bitwise operations
    FCXIR_AND,
    FCXIR_OR,
    FCXIR_XOR,
    FCXIR_NOT,
    
    // Shift and rotate operations
    FCXIR_LSHIFT,
    FCXIR_RSHIFT,
    FCXIR_LOGICAL_RSHIFT,
    FCXIR_ROTATE_LEFT,
    FCXIR_ROTATE_RIGHT,
    
    // Bitfield operations
    FCXIR_BITFIELD_EXTRACT,
    FCXIR_BITFIELD_INSERT,
    
    // Comparison operations
    FCXIR_CMP_EQ,
    FCXIR_CMP_NE,
    FCXIR_CMP_LT,
    FCXIR_CMP_LE,
    FCXIR_CMP_GT,
    FCXIR_CMP_GE,
    
    // Memory allocation operations
    FCXIR_ALLOC,
    FCXIR_DEALLOC,
    FCXIR_STACK_ALLOC,
    FCXIR_STACK_DEALLOC,
    FCXIR_ARENA_ALLOC,
    FCXIR_ARENA_RESET,
    FCXIR_SLAB_ALLOC,
    FCXIR_SLAB_FREE,
    FCXIR_POOL_ALLOC,
    
    // Alignment operations
    FCXIR_ALIGN_UP,
    FCXIR_ALIGN_DOWN,
    FCXIR_IS_ALIGNED,
    
    // Cache operations
    FCXIR_PREFETCH,
    FCXIR_PREFETCH_WRITE,
    
    // Atomic operations
    FCXIR_ATOMIC_LOAD,
    FCXIR_ATOMIC_STORE,
    FCXIR_ATOMIC_SWAP,
    FCXIR_ATOMIC_CAS,
    FCXIR_ATOMIC_ADD,
    FCXIR_ATOMIC_SUB,
    FCXIR_ATOMIC_AND,
    FCXIR_ATOMIC_OR,
    FCXIR_ATOMIC_XOR,
    
    // Memory barriers
    FCXIR_FENCE_FULL,
    FCXIR_FENCE_ACQUIRE,
    FCXIR_FENCE_RELEASE,
    
    // Syscall operations
    FCXIR_SYSCALL,
    
    // MMIO operations
    FCXIR_MMIO_READ,
    FCXIR_MMIO_WRITE,
    
    // Pointer operations
    FCXIR_PTR_ADD,
    FCXIR_PTR_SUB,
    FCXIR_PTR_DIFF,
    FCXIR_PTR_CAST,
    FCXIR_PTR_TO_INT,
    FCXIR_INT_TO_PTR,
    
    // Field access
    FCXIR_FIELD_ACCESS,
    FCXIR_FIELD_OFFSET,
    
    // Control flow
    FCXIR_BRANCH,
    FCXIR_JUMP,
    FCXIR_CALL,
    FCXIR_RETURN,
    FCXIR_PHI,
    
    // Labels and basic blocks
    FCXIR_LABEL,
    FCXIR_BASIC_BLOCK,
    
    // SIMD operations
    FCXIR_SIMD_ADD,
    FCXIR_SIMD_SUB,
    FCXIR_SIMD_MUL,
    FCXIR_SIMD_DIV,
    
    // Inline assembly
    FCXIR_INLINE_ASM,
    
    FCXIR_OPCODE_COUNT
} FcxIROpcode;

// ============================================================================
// FCx IR Instruction Structure
// ============================================================================

typedef struct {
    FcxIROpcode opcode;
    uint8_t operand_count;
    uint16_t flags;
    uint32_t line_number;      // Source line for debugging
    
    union {
        // Constant operation
        struct {
            VirtualReg dest;
            int64_t value;
        } const_op;
        
        // Bigint constant operation (for values > 64 bits)
        struct {
            VirtualReg dest;
            uint64_t limbs[16];  // Up to 1024 bits (16 x 64-bit limbs), little-endian
            uint8_t num_limbs;   // Number of limbs used (1-16)
        } const_bigint_op;
        
        // Load/Store operations
        struct {
            VirtualReg dest;
            VirtualReg src;
            int32_t offset;
        } load_store;
        
        // Global variable operations
        struct {
            VirtualReg vreg;       // Destination (load) or source (store)
            uint32_t global_index; // Index into module's globals array
        } global_op;
        
        // Binary operations (arithmetic, bitwise, comparison)
        struct {
            VirtualReg dest;
            VirtualReg left;
            VirtualReg right;
        } binary_op;
        
        // Unary operations
        struct {
            VirtualReg dest;
            VirtualReg src;
        } unary_op;
        
        // Bitfield operations
        struct {
            VirtualReg dest;
            VirtualReg src;
            VirtualReg start;
            VirtualReg len;
        } bitfield_op;
        
        // Allocation operations
        struct {
            VirtualReg dest;
            VirtualReg size;
            VirtualReg align;
            uint32_t scope_id;     // For arena allocation
        } alloc_op;
        
        // Arena operations
        struct {
            uint32_t scope_id;
        } arena_op;
        
        // Slab operations
        struct {
            VirtualReg ptr;
            uint32_t type_hash;
        } slab_op;
        
        // Atomic CAS operation
        struct {
            VirtualReg dest;
            VirtualReg ptr;
            VirtualReg expected;
            VirtualReg new_val;
        } atomic_cas;
        
        // Syscall operation
        struct {
            VirtualReg dest;
            VirtualReg syscall_num;
            VirtualReg* args;
            uint8_t arg_count;
        } syscall_op;
        
        // MMIO operations
        struct {
            VirtualReg dest;
            uint64_t address;
            VirtualReg value;
        } mmio_op;
        
        // Pointer operations
        struct {
            VirtualReg dest;
            VirtualReg ptr;
            VirtualReg offset;
            VRegType target_type;
        } ptr_op;
        
        // Field access
        struct {
            VirtualReg dest;
            VirtualReg base;
            uint32_t field_offset;
            const char* field_name;
        } field_op;
        
        // Branch operation
        struct {
            VirtualReg cond;
            uint32_t true_label;
            uint32_t false_label;
        } branch_op;
        
        // Jump operation
        struct {
            uint32_t label_id;
        } jump_op;
        
        // Call operation
        struct {
            VirtualReg dest;
            const char* function;
            VirtualReg* args;
            uint8_t arg_count;
        } call_op;
        
        // Return operation
        struct {
            VirtualReg value;
            bool has_value;
        } return_op;
        
        // Phi operation (for SSA form)
        struct {
            VirtualReg dest;
            VirtualReg* incoming;
            uint32_t* blocks;
            uint8_t incoming_count;
        } phi_op;
        
        // Label
        struct {
            uint32_t label_id;
            const char* label_name;
        } label;
        
        // Inline assembly
        struct {
            const char* asm_template;
            const char** output_constraints;
            const char** input_constraints;
            VirtualReg* outputs;
            VirtualReg* inputs;
            const char** clobbers;
            uint8_t output_count;
            uint8_t input_count;
            uint8_t clobber_count;
            bool is_volatile;
        } inline_asm;
    } u;
} FcxIRInstruction;

// ============================================================================
// Basic Block Structure
// ============================================================================

typedef struct FcxIRBasicBlock {
    uint32_t id;
    const char* name;
    
    FcxIRInstruction* instructions;
    uint32_t instruction_count;
    uint32_t instruction_capacity;
    
    uint32_t* successors;
    uint8_t successor_count;
    
    uint32_t* predecessors;
    uint8_t predecessor_count;
    
    bool is_entry;
    bool is_exit;
} FcxIRBasicBlock;

// ============================================================================
// Function Structure
// ============================================================================

typedef struct {
    const char* name;
    VirtualReg* parameters;
    uint8_t parameter_count;
    VRegType return_type;
    
    FcxIRBasicBlock* blocks;
    uint32_t block_count;
    uint32_t block_capacity;
    
    uint32_t next_vreg_id;
    uint32_t next_label_id;
    uint32_t next_block_id;
} FcxIRFunction;

// ============================================================================
// String Literal Storage
// ============================================================================

typedef struct {
    uint32_t id;           // String ID (used as label)
    const char* data;      // String content
    size_t length;         // String length (including null terminator)
} FcxStringLiteral;

// ============================================================================
// Global Variable Structure
// ============================================================================

typedef struct {
    const char* name;      // Variable name
    VirtualReg vreg;       // Associated virtual register
    VRegType type;         // Variable type
    bool is_const;         // Is this a constant?
    bool has_init;         // Has initializer?
    int64_t init_value;    // Initial value (for simple integer constants)
} FcxIRGlobal;

// ============================================================================
// Module Structure (Collection of Functions)
// ============================================================================

typedef struct {
    const char* name;
    FcxIRFunction* functions;
    uint32_t function_count;
    uint32_t function_capacity;
    
    // Global variables
    FcxIRGlobal* globals;
    uint32_t global_count;
    uint32_t global_capacity;
    
    // String literals storage
    FcxStringLiteral* string_literals;
    uint32_t string_count;
    uint32_t string_capacity;
    uint32_t next_string_id;
} FcxIRModule;

// ============================================================================
// IR Builder Interface
// ============================================================================

// Module management
FcxIRModule* fcx_ir_module_create(const char* name);
void fcx_ir_module_destroy(FcxIRModule* module);

// String literal management
uint32_t fcx_ir_module_add_string(FcxIRModule* module, const char* str, size_t length);

// Function management
FcxIRFunction* fcx_ir_function_create(const char* name, VRegType return_type);
void fcx_ir_function_destroy(FcxIRFunction* function);
void fcx_ir_module_add_function(FcxIRModule* module, FcxIRFunction* function);

// Basic block management
FcxIRBasicBlock* fcx_ir_block_create(FcxIRFunction* function, const char* name);
FcxIRBasicBlock* fcx_ir_block_get_by_id(FcxIRFunction* function, uint32_t id);
void fcx_ir_block_add_successor(FcxIRBasicBlock* block, uint32_t successor_id);
void fcx_ir_block_add_predecessor(FcxIRBasicBlock* block, uint32_t predecessor_id);

// Virtual register allocation
VirtualReg fcx_ir_alloc_vreg(FcxIRFunction* function, VRegType type);

// Instruction building
void fcx_ir_build_const(FcxIRBasicBlock* block, VirtualReg dest, int64_t value);
void fcx_ir_build_const_bigint(FcxIRBasicBlock* block, VirtualReg dest, const uint64_t* limbs, uint8_t num_limbs);
void fcx_ir_build_load(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src, int32_t offset);
void fcx_ir_build_store(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src, int32_t offset);
void fcx_ir_build_mov(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src);
void fcx_ir_build_binary_op(FcxIRBasicBlock* block, FcxIROpcode opcode, VirtualReg dest, VirtualReg left, VirtualReg right);
void fcx_ir_build_unary_op(FcxIRBasicBlock* block, FcxIROpcode opcode, VirtualReg dest, VirtualReg src);
void fcx_ir_build_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, VirtualReg align);
void fcx_ir_build_syscall(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg syscall_num, VirtualReg* args, uint8_t arg_count);
void fcx_ir_build_branch(FcxIRBasicBlock* block, VirtualReg cond, uint32_t true_label, uint32_t false_label);
void fcx_ir_build_jump(FcxIRBasicBlock* block, uint32_t label_id);
void fcx_ir_build_call(FcxIRBasicBlock* block, VirtualReg dest, const char* function, VirtualReg* args, uint8_t arg_count);
void fcx_ir_build_return(FcxIRBasicBlock* block, VirtualReg value, bool has_value);

// Atomic operations
void fcx_ir_build_atomic_load(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr);
void fcx_ir_build_atomic_store(FcxIRBasicBlock* block, VirtualReg ptr, VirtualReg value);
void fcx_ir_build_atomic_swap(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VirtualReg value);
void fcx_ir_build_atomic_cas(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VirtualReg expected, VirtualReg new_val);

// Memory barriers
void fcx_ir_build_fence(FcxIRBasicBlock* block, FcxIROpcode fence_type);

// Pointer operations
void fcx_ir_build_ptr_add(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VirtualReg offset);
void fcx_ir_build_ptr_cast(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VRegType target_type);

// MMIO operations
void fcx_ir_build_mmio_read(FcxIRBasicBlock* block, VirtualReg dest, uint64_t address);
void fcx_ir_build_mmio_write(FcxIRBasicBlock* block, uint64_t address, VirtualReg value);

// Arena/Slab/Pool allocators
void fcx_ir_build_arena_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, VirtualReg align, uint32_t scope_id);
void fcx_ir_build_slab_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, uint32_t type_hash);
void fcx_ir_build_pool_alloc(FcxIRBasicBlock* block, VirtualReg dest, uint32_t pool_id);

// Inline assembly
void fcx_ir_build_inline_asm(FcxIRBasicBlock* block, const char* asm_template,
                             const char** output_constraints, VirtualReg* outputs, uint8_t output_count,
                             const char** input_constraints, VirtualReg* inputs, uint8_t input_count,
                             const char** clobbers, uint8_t clobber_count, bool is_volatile);

// Debugging and printing
void fcx_ir_print_instruction(const FcxIRInstruction* instr);
void fcx_ir_print_block(const FcxIRBasicBlock* block);
void fcx_ir_print_function(const FcxIRFunction* function);
void fcx_ir_print_module(const FcxIRModule* module);

#endif // FCX_IR_H
