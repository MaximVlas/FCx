#define _POSIX_C_SOURCE 200809L
#include "fcx_ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Module Management
// ============================================================================

FcxIRModule* fcx_ir_module_create(const char* name) {
    FcxIRModule* module = (FcxIRModule*)malloc(sizeof(FcxIRModule));
    if (!module) return NULL;
    
    module->name = strdup(name);
    module->functions = NULL;
    module->function_count = 0;
    module->function_capacity = 0;
    
    // Initialize string literal storage
    module->string_literals = NULL;
    module->string_count = 0;
    module->string_capacity = 0;
    module->next_string_id = 1;
    
    return module;
}

void fcx_ir_module_destroy(FcxIRModule* module) {
    if (!module) return;
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        fcx_ir_function_destroy(&module->functions[i]);
    }
    
    // Free string literals
    for (uint32_t i = 0; i < module->string_count; i++) {
        free((void*)module->string_literals[i].data);
    }
    free(module->string_literals);
    
    free(module->functions);
    free((void*)module->name);
    free(module);
}

// Add a string literal to the module and return its ID
uint32_t fcx_ir_module_add_string(FcxIRModule* module, const char* str, size_t length) {
    if (!module || !str) return 0;
    
    // Expand capacity if needed
    if (module->string_count >= module->string_capacity) {
        uint32_t new_capacity = module->string_capacity == 0 ? 16 : module->string_capacity * 2;
        FcxStringLiteral* new_strings = (FcxStringLiteral*)realloc(
            module->string_literals, new_capacity * sizeof(FcxStringLiteral));
        if (!new_strings) return 0;
        module->string_literals = new_strings;
        module->string_capacity = new_capacity;
    }
    
    // Add the string
    uint32_t id = module->next_string_id++;
    FcxStringLiteral* lit = &module->string_literals[module->string_count++];
    lit->id = id;
    lit->data = strndup(str, length);
    lit->length = length;
    
    return id;
}

void fcx_ir_module_add_function(FcxIRModule* module, FcxIRFunction* function) {
    if (!module || !function) return;
    
    if (module->function_count >= module->function_capacity) {
        uint32_t new_capacity = module->function_capacity == 0 ? 8 : module->function_capacity * 2;
        FcxIRFunction* new_functions = (FcxIRFunction*)realloc(
            module->functions, new_capacity * sizeof(FcxIRFunction));
        
        if (!new_functions) return;
        
        module->functions = new_functions;
        module->function_capacity = new_capacity;
    }
    
    module->functions[module->function_count++] = *function;
}

// ============================================================================
// Function Management
// ============================================================================

FcxIRFunction* fcx_ir_function_create(const char* name, VRegType return_type) {
    FcxIRFunction* function = (FcxIRFunction*)malloc(sizeof(FcxIRFunction));
    if (!function) return NULL;
    
    function->name = strdup(name);
    function->parameters = NULL;
    function->parameter_count = 0;
    function->return_type = return_type;
    
    function->blocks = NULL;
    function->block_count = 0;
    function->block_capacity = 0;
    
    function->next_vreg_id = 1;
    function->next_label_id = 1;
    function->next_block_id = 1;
    
    return function;
}

void fcx_ir_function_destroy(FcxIRFunction* function) {
    if (!function) return;
    
    for (uint32_t i = 0; i < function->block_count; i++) {
        FcxIRBasicBlock* block = &function->blocks[i];
        
        // Free instructions and their dynamic data
        for (uint32_t j = 0; j < block->instruction_count; j++) {
            FcxIRInstruction* instr = &block->instructions[j];
            
            // Free dynamic arrays in instructions
            if (instr->opcode == FCXIR_SYSCALL && instr->u.syscall_op.args) {
                free(instr->u.syscall_op.args);
            } else if (instr->opcode == FCXIR_CALL && instr->u.call_op.args) {
                free(instr->u.call_op.args);
            } else if (instr->opcode == FCXIR_PHI) {
                free(instr->u.phi_op.incoming);
                free(instr->u.phi_op.blocks);
            }
        }
        
        free(block->instructions);
        free(block->successors);
        free(block->predecessors);
        free((void*)block->name);
    }
    
    free(function->blocks);
    free(function->parameters);
    free((void*)function->name);
}

// ============================================================================
// Basic Block Management
// ============================================================================

FcxIRBasicBlock* fcx_ir_block_create(FcxIRFunction* function, const char* name) {
    if (!function) return NULL;
    
    if (function->block_count >= function->block_capacity) {
        uint32_t new_capacity = function->block_capacity == 0 ? 8 : function->block_capacity * 2;
        FcxIRBasicBlock* new_blocks = (FcxIRBasicBlock*)realloc(
            function->blocks, new_capacity * sizeof(FcxIRBasicBlock));
        
        if (!new_blocks) return NULL;
        
        function->blocks = new_blocks;
        function->block_capacity = new_capacity;
    }
    
    FcxIRBasicBlock* block = &function->blocks[function->block_count++];
    memset(block, 0, sizeof(FcxIRBasicBlock));
    
    block->id = function->next_block_id++;
    block->name = name ? strdup(name) : NULL;
    block->instructions = NULL;
    block->instruction_count = 0;
    block->instruction_capacity = 0;
    block->successors = NULL;
    block->successor_count = 0;
    block->predecessors = NULL;
    block->predecessor_count = 0;
    block->is_entry = (function->block_count == 1);
    block->is_exit = false;
    
    return block;
}

FcxIRBasicBlock* fcx_ir_block_get_by_id(FcxIRFunction* function, uint32_t id) {
    if (!function) return NULL;
    
    for (uint32_t i = 0; i < function->block_count; i++) {
        if (function->blocks[i].id == id) {
            return &function->blocks[i];
        }
    }
    
    return NULL;
}

void fcx_ir_block_add_successor(FcxIRBasicBlock* block, uint32_t successor_id) {
    if (!block) return;
    
    // Check if already exists
    for (uint8_t i = 0; i < block->successor_count; i++) {
        if (block->successors[i] == successor_id) return;
    }
    
    uint32_t* new_successors = (uint32_t*)realloc(
        block->successors, (block->successor_count + 1) * sizeof(uint32_t));
    
    if (!new_successors) return;
    
    block->successors = new_successors;
    block->successors[block->successor_count++] = successor_id;
}

void fcx_ir_block_add_predecessor(FcxIRBasicBlock* block, uint32_t predecessor_id) {
    if (!block) return;
    
    // Check if already exists
    for (uint8_t i = 0; i < block->predecessor_count; i++) {
        if (block->predecessors[i] == predecessor_id) return;
    }
    
    uint32_t* new_predecessors = (uint32_t*)realloc(
        block->predecessors, (block->predecessor_count + 1) * sizeof(uint32_t));
    
    if (!new_predecessors) return;
    
    block->predecessors = new_predecessors;
    block->predecessors[block->predecessor_count++] = predecessor_id;
}

// ============================================================================
// Virtual Register Allocation
// ============================================================================

VirtualReg fcx_ir_alloc_vreg(FcxIRFunction* function, VRegType type) {
    VirtualReg vreg;
    vreg.id = function->next_vreg_id++;
    vreg.type = type;
    vreg.flags = 0;
    
    // Set size based on type
    switch (type) {
        case VREG_TYPE_I8:
        case VREG_TYPE_U8:
        case VREG_TYPE_BOOL:
            vreg.size = 1;
            break;
        case VREG_TYPE_I16:
        case VREG_TYPE_U16:
            vreg.size = 2;
            break;
        case VREG_TYPE_I32:
        case VREG_TYPE_U32:
        case VREG_TYPE_F32:
            vreg.size = 4;
            break;
        case VREG_TYPE_I64:
        case VREG_TYPE_U64:
        case VREG_TYPE_F64:
        case VREG_TYPE_PTR:
        case VREG_TYPE_RAWPTR:
        case VREG_TYPE_BYTEPTR:
            vreg.size = 8;
            break;
        case VREG_TYPE_I128:
        case VREG_TYPE_U128:
            vreg.size = 16;
            break;
        case VREG_TYPE_I256:
        case VREG_TYPE_U256:
            vreg.size = 32;
            break;
        case VREG_TYPE_I512:
        case VREG_TYPE_U512:
            vreg.size = 64;
            break;
        case VREG_TYPE_I1024:
        case VREG_TYPE_U1024:
            vreg.size = 128;
            break;
        case VREG_TYPE_VOID:
            vreg.size = 0;
            break;
        default:
            vreg.size = 8;
            break;
    }
    
    return vreg;
}

// ============================================================================
// Helper: Add instruction to basic block
// ============================================================================

static void add_instruction(FcxIRBasicBlock* block, FcxIRInstruction instr) {
    if (!block) return;
    
    if (block->instruction_count >= block->instruction_capacity) {
        uint32_t new_capacity = block->instruction_capacity == 0 ? 16 : block->instruction_capacity * 2;
        FcxIRInstruction* new_instructions = (FcxIRInstruction*)realloc(
            block->instructions, new_capacity * sizeof(FcxIRInstruction));
        
        if (!new_instructions) return;
        
        block->instructions = new_instructions;
        block->instruction_capacity = new_capacity;
    }
    
    block->instructions[block->instruction_count++] = instr;
}

// ============================================================================
// Instruction Building Functions
// ============================================================================

void fcx_ir_build_const(FcxIRBasicBlock* block, VirtualReg dest, int64_t value) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_CONST;
    instr.operand_count = 1;
    instr.u.const_op.dest = dest;
    instr.u.const_op.value = value;
    add_instruction(block, instr);
}

void fcx_ir_build_const_bigint(FcxIRBasicBlock* block, VirtualReg dest, const uint64_t* limbs, uint8_t num_limbs) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_CONST_BIGINT;
    instr.operand_count = 1;
    instr.u.const_bigint_op.dest = dest;
    instr.u.const_bigint_op.num_limbs = num_limbs;
    for (uint8_t i = 0; i < 16; i++) {
        instr.u.const_bigint_op.limbs[i] = (i < num_limbs) ? limbs[i] : 0;
    }
    add_instruction(block, instr);
}

void fcx_ir_build_load(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src, int32_t offset) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_LOAD;
    instr.operand_count = 2;
    instr.u.load_store.dest = dest;
    instr.u.load_store.src = src;
    instr.u.load_store.offset = offset;
    add_instruction(block, instr);
}

void fcx_ir_build_store(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src, int32_t offset) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_STORE;
    instr.operand_count = 2;
    instr.u.load_store.dest = dest;
    instr.u.load_store.src = src;
    instr.u.load_store.offset = offset;
    add_instruction(block, instr);
}

void fcx_ir_build_mov(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg src) {
    // MOV is a register-to-register copy (not a memory load)
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_MOV;
    instr.operand_count = 2;
    instr.u.load_store.dest = dest;
    instr.u.load_store.src = src;
    instr.u.load_store.offset = 0;
    add_instruction(block, instr);
}

void fcx_ir_build_binary_op(FcxIRBasicBlock* block, FcxIROpcode opcode, 
                            VirtualReg dest, VirtualReg left, VirtualReg right) {
    FcxIRInstruction instr = {0};
    instr.opcode = opcode;
    instr.operand_count = 3;
    instr.u.binary_op.dest = dest;
    instr.u.binary_op.left = left;
    instr.u.binary_op.right = right;
    add_instruction(block, instr);
}

void fcx_ir_build_unary_op(FcxIRBasicBlock* block, FcxIROpcode opcode, 
                           VirtualReg dest, VirtualReg src) {
    FcxIRInstruction instr = {0};
    instr.opcode = opcode;
    instr.operand_count = 2;
    instr.u.unary_op.dest = dest;
    instr.u.unary_op.src = src;
    add_instruction(block, instr);
}

void fcx_ir_build_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, VirtualReg align) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ALLOC;
    instr.operand_count = 3;
    instr.u.alloc_op.dest = dest;
    instr.u.alloc_op.size = size;
    instr.u.alloc_op.align = align;
    instr.u.alloc_op.scope_id = 0;
    add_instruction(block, instr);
}

void fcx_ir_build_syscall(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg syscall_num, 
                         VirtualReg* args, uint8_t arg_count) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_SYSCALL;
    instr.operand_count = 2 + arg_count;
    instr.u.syscall_op.dest = dest;
    instr.u.syscall_op.syscall_num = syscall_num;
    instr.u.syscall_op.arg_count = arg_count;
    
    if (arg_count > 0) {
        instr.u.syscall_op.args = (VirtualReg*)malloc(arg_count * sizeof(VirtualReg));
        memcpy(instr.u.syscall_op.args, args, arg_count * sizeof(VirtualReg));
    } else {
        instr.u.syscall_op.args = NULL;
    }
    
    add_instruction(block, instr);
}

void fcx_ir_build_branch(FcxIRBasicBlock* block, VirtualReg cond, uint32_t true_label, uint32_t false_label) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_BRANCH;
    instr.operand_count = 1;
    instr.u.branch_op.cond = cond;
    instr.u.branch_op.true_label = true_label;
    instr.u.branch_op.false_label = false_label;
    add_instruction(block, instr);
}

void fcx_ir_build_jump(FcxIRBasicBlock* block, uint32_t label_id) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_JUMP;
    instr.operand_count = 0;
    instr.u.jump_op.label_id = label_id;
    add_instruction(block, instr);
}

void fcx_ir_build_call(FcxIRBasicBlock* block, VirtualReg dest, const char* function, 
                      VirtualReg* args, uint8_t arg_count) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_CALL;
    instr.operand_count = 1 + arg_count;
    instr.u.call_op.dest = dest;
    instr.u.call_op.function = strdup(function);
    instr.u.call_op.arg_count = arg_count;
    
    if (arg_count > 0) {
        instr.u.call_op.args = (VirtualReg*)malloc(arg_count * sizeof(VirtualReg));
        memcpy(instr.u.call_op.args, args, arg_count * sizeof(VirtualReg));
    } else {
        instr.u.call_op.args = NULL;
    }
    
    add_instruction(block, instr);
}

void fcx_ir_build_return(FcxIRBasicBlock* block, VirtualReg value, bool has_value) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_RETURN;
    instr.operand_count = has_value ? 1 : 0;
    instr.u.return_op.value = value;
    instr.u.return_op.has_value = has_value;
    add_instruction(block, instr);
}

// ============================================================================
// Atomic Operations
// ============================================================================

void fcx_ir_build_atomic_load(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ATOMIC_LOAD;
    instr.operand_count = 2;
    instr.u.unary_op.dest = dest;
    instr.u.unary_op.src = ptr;
    add_instruction(block, instr);
}

void fcx_ir_build_atomic_store(FcxIRBasicBlock* block, VirtualReg ptr, VirtualReg value) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ATOMIC_STORE;
    instr.operand_count = 2;
    instr.u.load_store.dest = ptr;
    instr.u.load_store.src = value;
    instr.u.load_store.offset = 0;
    add_instruction(block, instr);
}

void fcx_ir_build_atomic_swap(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VirtualReg value) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ATOMIC_SWAP;
    instr.operand_count = 3;
    instr.u.binary_op.dest = dest;
    instr.u.binary_op.left = ptr;
    instr.u.binary_op.right = value;
    add_instruction(block, instr);
}

void fcx_ir_build_atomic_cas(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, 
                             VirtualReg expected, VirtualReg new_val) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ATOMIC_CAS;
    instr.operand_count = 4;
    instr.u.atomic_cas.dest = dest;
    instr.u.atomic_cas.ptr = ptr;
    instr.u.atomic_cas.expected = expected;
    instr.u.atomic_cas.new_val = new_val;
    add_instruction(block, instr);
}

// ============================================================================
// Memory Barriers
// ============================================================================

void fcx_ir_build_fence(FcxIRBasicBlock* block, FcxIROpcode fence_type) {
    FcxIRInstruction instr = {0};
    instr.opcode = fence_type;
    instr.operand_count = 0;
    add_instruction(block, instr);
}

// ============================================================================
// Pointer Operations
// ============================================================================

void fcx_ir_build_ptr_add(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VirtualReg offset) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_PTR_ADD;
    instr.operand_count = 3;
    instr.u.binary_op.dest = dest;
    instr.u.binary_op.left = ptr;
    instr.u.binary_op.right = offset;
    add_instruction(block, instr);
}

void fcx_ir_build_ptr_cast(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg ptr, VRegType target_type) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_PTR_CAST;
    instr.operand_count = 2;
    instr.u.ptr_op.dest = dest;
    instr.u.ptr_op.ptr = ptr;
    instr.u.ptr_op.target_type = target_type;
    add_instruction(block, instr);
}

// ============================================================================
// MMIO Operations
// ============================================================================

void fcx_ir_build_mmio_read(FcxIRBasicBlock* block, VirtualReg dest, uint64_t address) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_MMIO_READ;
    instr.operand_count = 1;
    instr.u.mmio_op.dest = dest;
    instr.u.mmio_op.address = address;
    add_instruction(block, instr);
}

void fcx_ir_build_mmio_write(FcxIRBasicBlock* block, uint64_t address, VirtualReg value) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_MMIO_WRITE;
    instr.operand_count = 1;
    instr.u.mmio_op.address = address;
    instr.u.mmio_op.value = value;
    add_instruction(block, instr);
}

// ============================================================================
// Advanced Allocators
// ============================================================================

void fcx_ir_build_arena_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, 
                              VirtualReg align, uint32_t scope_id) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_ARENA_ALLOC;
    instr.operand_count = 3;
    instr.u.alloc_op.dest = dest;
    instr.u.alloc_op.size = size;
    instr.u.alloc_op.align = align;
    instr.u.alloc_op.scope_id = scope_id;
    add_instruction(block, instr);
}

void fcx_ir_build_slab_alloc(FcxIRBasicBlock* block, VirtualReg dest, VirtualReg size, uint32_t type_hash) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_SLAB_ALLOC;
    instr.operand_count = 2;
    instr.u.alloc_op.dest = dest;
    instr.u.alloc_op.size = size;
    instr.u.alloc_op.scope_id = type_hash;  // Reuse scope_id field for type_hash
    add_instruction(block, instr);
}

void fcx_ir_build_pool_alloc(FcxIRBasicBlock* block, VirtualReg dest, uint32_t pool_id) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_POOL_ALLOC;
    instr.operand_count = 1;
    instr.u.alloc_op.dest = dest;
    instr.u.alloc_op.scope_id = pool_id;  // Reuse scope_id field for pool_id
    add_instruction(block, instr);
}

void fcx_ir_build_inline_asm(FcxIRBasicBlock* block, const char* asm_template,
                             const char** output_constraints, VirtualReg* outputs, uint8_t output_count,
                             const char** input_constraints, VirtualReg* inputs, uint8_t input_count,
                             const char** clobbers, uint8_t clobber_count, bool is_volatile) {
    FcxIRInstruction instr = {0};
    instr.opcode = FCXIR_INLINE_ASM;
    instr.operand_count = output_count + input_count;
    instr.u.inline_asm.asm_template = asm_template;
    instr.u.inline_asm.output_constraints = output_constraints;
    instr.u.inline_asm.input_constraints = input_constraints;
    instr.u.inline_asm.outputs = outputs;
    instr.u.inline_asm.inputs = inputs;
    instr.u.inline_asm.clobbers = clobbers;
    instr.u.inline_asm.output_count = output_count;
    instr.u.inline_asm.input_count = input_count;
    instr.u.inline_asm.clobber_count = clobber_count;
    instr.u.inline_asm.is_volatile = is_volatile;
    add_instruction(block, instr);
}

// ============================================================================
// Debugging and Printing Functions
// ============================================================================

static const char* opcode_to_string(FcxIROpcode opcode) {
    switch (opcode) {
        case FCXIR_CONST: return "const";
        case FCXIR_CONST_BIGINT: return "const.bigint";
        case FCXIR_LOAD: return "load";
        case FCXIR_STORE: return "store";
        case FCXIR_LOAD_VOLATILE: return "load.volatile";
        case FCXIR_STORE_VOLATILE: return "store.volatile";
        case FCXIR_MOV: return "mov";
        case FCXIR_ADD: return "add";
        case FCXIR_SUB: return "sub";
        case FCXIR_MUL: return "mul";
        case FCXIR_DIV: return "div";
        case FCXIR_MOD: return "mod";
        case FCXIR_NEG: return "neg";
        case FCXIR_AND: return "and";
        case FCXIR_OR: return "or";
        case FCXIR_XOR: return "xor";
        case FCXIR_NOT: return "not";
        case FCXIR_LSHIFT: return "shl";
        case FCXIR_RSHIFT: return "shr";
        case FCXIR_LOGICAL_RSHIFT: return "lshr";
        case FCXIR_ROTATE_LEFT: return "rol";
        case FCXIR_ROTATE_RIGHT: return "ror";
        case FCXIR_BITFIELD_EXTRACT: return "bfextract";
        case FCXIR_BITFIELD_INSERT: return "bfinsert";
        case FCXIR_CMP_EQ: return "cmp.eq";
        case FCXIR_CMP_NE: return "cmp.ne";
        case FCXIR_CMP_LT: return "cmp.lt";
        case FCXIR_CMP_LE: return "cmp.le";
        case FCXIR_CMP_GT: return "cmp.gt";
        case FCXIR_CMP_GE: return "cmp.ge";
        case FCXIR_ALLOC: return "alloc";
        case FCXIR_DEALLOC: return "dealloc";
        case FCXIR_STACK_ALLOC: return "stack_alloc";
        case FCXIR_STACK_DEALLOC: return "stack_dealloc";
        case FCXIR_ARENA_ALLOC: return "arena_alloc";
        case FCXIR_ARENA_RESET: return "arena_reset";
        case FCXIR_SLAB_ALLOC: return "slab_alloc";
        case FCXIR_SLAB_FREE: return "slab_free";
        case FCXIR_POOL_ALLOC: return "pool_alloc";
        case FCXIR_ALIGN_UP: return "align_up";
        case FCXIR_ALIGN_DOWN: return "align_down";
        case FCXIR_IS_ALIGNED: return "is_aligned";
        case FCXIR_PREFETCH: return "prefetch";
        case FCXIR_PREFETCH_WRITE: return "prefetch_write";
        case FCXIR_ATOMIC_LOAD: return "atomic.load";
        case FCXIR_ATOMIC_STORE: return "atomic.store";
        case FCXIR_ATOMIC_SWAP: return "atomic.swap";
        case FCXIR_ATOMIC_CAS: return "atomic.cas";
        case FCXIR_ATOMIC_ADD: return "atomic.add";
        case FCXIR_ATOMIC_SUB: return "atomic.sub";
        case FCXIR_ATOMIC_AND: return "atomic.and";
        case FCXIR_ATOMIC_OR: return "atomic.or";
        case FCXIR_ATOMIC_XOR: return "atomic.xor";
        case FCXIR_FENCE_FULL: return "fence.full";
        case FCXIR_FENCE_ACQUIRE: return "fence.acquire";
        case FCXIR_FENCE_RELEASE: return "fence.release";
        case FCXIR_SYSCALL: return "syscall";
        case FCXIR_MMIO_READ: return "mmio.read";
        case FCXIR_MMIO_WRITE: return "mmio.write";
        case FCXIR_PTR_ADD: return "ptr.add";
        case FCXIR_PTR_SUB: return "ptr.sub";
        case FCXIR_PTR_DIFF: return "ptr.diff";
        case FCXIR_PTR_CAST: return "ptr.cast";
        case FCXIR_PTR_TO_INT: return "ptr.to_int";
        case FCXIR_INT_TO_PTR: return "int.to_ptr";
        case FCXIR_FIELD_ACCESS: return "field.access";
        case FCXIR_FIELD_OFFSET: return "field.offset";
        case FCXIR_BRANCH: return "branch";
        case FCXIR_JUMP: return "jump";
        case FCXIR_CALL: return "call";
        case FCXIR_RETURN: return "return";
        case FCXIR_PHI: return "phi";
        case FCXIR_LABEL: return "label";
        case FCXIR_BASIC_BLOCK: return "block";
        case FCXIR_SIMD_ADD: return "simd.add";
        case FCXIR_SIMD_SUB: return "simd.sub";
        case FCXIR_SIMD_MUL: return "simd.mul";
        case FCXIR_SIMD_DIV: return "simd.div";
        case FCXIR_INLINE_ASM: return "inline_asm";
        default: return "unknown";
    }
}

static const char* vreg_type_to_string(VRegType type) {
    switch (type) {
        case VREG_TYPE_I8: return "i8";
        case VREG_TYPE_I16: return "i16";
        case VREG_TYPE_I32: return "i32";
        case VREG_TYPE_I64: return "i64";
        case VREG_TYPE_U8: return "u8";
        case VREG_TYPE_U16: return "u16";
        case VREG_TYPE_U32: return "u32";
        case VREG_TYPE_U64: return "u64";
        case VREG_TYPE_F32: return "f32";
        case VREG_TYPE_F64: return "f64";
        case VREG_TYPE_PTR: return "ptr";
        case VREG_TYPE_RAWPTR: return "rawptr";
        case VREG_TYPE_BYTEPTR: return "byteptr";
        case VREG_TYPE_BOOL: return "bool";
        case VREG_TYPE_VOID: return "void";
        default: return "unknown";
    }
}

void fcx_ir_print_instruction(const FcxIRInstruction* instr) {
    if (!instr) return;
    
    printf("  %s ", opcode_to_string(instr->opcode));
    
    switch (instr->opcode) {
        case FCXIR_CONST:
            printf("%%v%u = %ld", instr->u.const_op.dest.id, instr->u.const_op.value);
            break;
        
        case FCXIR_CONST_BIGINT: {
            printf("%%v%u = 0x", instr->u.const_bigint_op.dest.id);
            // Print limbs in big-endian order (most significant first)
            for (int i = instr->u.const_bigint_op.num_limbs - 1; i >= 0; i--) {
                if (i == instr->u.const_bigint_op.num_limbs - 1) {
                    printf("%lx", (unsigned long)instr->u.const_bigint_op.limbs[i]);
                } else {
                    printf("%016lx", (unsigned long)instr->u.const_bigint_op.limbs[i]);
                }
            }
            break;
        }
            
        case FCXIR_MOV:
            printf("%%v%u = %%v%u", 
                   instr->u.load_store.dest.id,
                   instr->u.load_store.src.id);
            break;
            
        case FCXIR_LOAD:
        case FCXIR_LOAD_VOLATILE:
            printf("%%v%u = [%%v%u + %d]", 
                   instr->u.load_store.dest.id,
                   instr->u.load_store.src.id,
                   instr->u.load_store.offset);
            break;
            
        case FCXIR_STORE:
        case FCXIR_STORE_VOLATILE:
            printf("[%%v%u + %d] = %%v%u", 
                   instr->u.load_store.dest.id,
                   instr->u.load_store.offset,
                   instr->u.load_store.src.id);
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
            printf("%%v%u = %%v%u, %%v%u", 
                   instr->u.binary_op.dest.id,
                   instr->u.binary_op.left.id,
                   instr->u.binary_op.right.id);
            break;
            
        case FCXIR_NEG:
        case FCXIR_NOT:
        case FCXIR_ATOMIC_LOAD:
            printf("%%v%u = %%v%u", 
                   instr->u.unary_op.dest.id,
                   instr->u.unary_op.src.id);
            break;
        
        case FCXIR_DEALLOC:
        case FCXIR_PREFETCH:
        case FCXIR_PREFETCH_WRITE:
            printf("%%v%u", instr->u.unary_op.src.id);
            break;
        
        case FCXIR_ARENA_RESET:
            printf("scope:%u", instr->u.arena_op.scope_id);
            break;
        
        case FCXIR_SLAB_FREE:
            printf("%%v%u, type_hash:%u", 
                   instr->u.slab_op.ptr.id,
                   instr->u.slab_op.type_hash);
            break;
            
        case FCXIR_ALLOC:
        case FCXIR_ARENA_ALLOC:
            printf("%%v%u = size:%%v%u, align:%%v%u", 
                   instr->u.alloc_op.dest.id,
                   instr->u.alloc_op.size.id,
                   instr->u.alloc_op.align.id);
            if (instr->opcode == FCXIR_ARENA_ALLOC) {
                printf(", scope:%u", instr->u.alloc_op.scope_id);
            }
            break;
            
        case FCXIR_SYSCALL:
            printf("%%v%u = num:%%v%u, args:[", 
                   instr->u.syscall_op.dest.id,
                   instr->u.syscall_op.syscall_num.id);
            for (uint8_t i = 0; i < instr->u.syscall_op.arg_count; i++) {
                printf("%%v%u", instr->u.syscall_op.args[i].id);
                if (i < instr->u.syscall_op.arg_count - 1) printf(", ");
            }
            printf("]");
            break;
            
        case FCXIR_CALL:
            printf("%%v%u = %s(", 
                   instr->u.call_op.dest.id,
                   instr->u.call_op.function);
            for (uint8_t i = 0; i < instr->u.call_op.arg_count; i++) {
                printf("%%v%u", instr->u.call_op.args[i].id);
                if (i < instr->u.call_op.arg_count - 1) printf(", ");
            }
            printf(")");
            break;
            
        case FCXIR_BRANCH:
            printf("%%v%u ? .L%u : .L%u", 
                   instr->u.branch_op.cond.id,
                   instr->u.branch_op.true_label,
                   instr->u.branch_op.false_label);
            break;
            
        case FCXIR_JUMP:
            printf(".L%u", instr->u.jump_op.label_id);
            break;
            
        case FCXIR_RETURN:
            if (instr->u.return_op.has_value) {
                printf("%%v%u", instr->u.return_op.value.id);
            }
            break;
            
        case FCXIR_ATOMIC_CAS:
            printf("%%v%u = [%%v%u], expected:%%v%u, new:%%v%u", 
                   instr->u.atomic_cas.dest.id,
                   instr->u.atomic_cas.ptr.id,
                   instr->u.atomic_cas.expected.id,
                   instr->u.atomic_cas.new_val.id);
            break;
            
        case FCXIR_MMIO_READ:
            printf("%%v%u = [0x%lx]", 
                   instr->u.mmio_op.dest.id,
                   instr->u.mmio_op.address);
            break;
            
        case FCXIR_MMIO_WRITE:
            printf("[0x%lx] = %%v%u", 
                   instr->u.mmio_op.address,
                   instr->u.mmio_op.value.id);
            break;
            
        default:
            break;
    }
    
    printf("\n");
}

void fcx_ir_print_block(const FcxIRBasicBlock* block) {
    if (!block) return;
    
    printf("\n.BB%u", block->id);
    if (block->name) {
        printf(" (%s)", block->name);
    }
    printf(":\n");
    
    if (block->predecessor_count > 0) {
        printf("  ; predecessors: ");
        for (uint8_t i = 0; i < block->predecessor_count; i++) {
            printf(".BB%u", block->predecessors[i]);
            if (i < block->predecessor_count - 1) printf(", ");
        }
        printf("\n");
    }
    
    for (uint32_t i = 0; i < block->instruction_count; i++) {
        fcx_ir_print_instruction(&block->instructions[i]);
    }
    
    if (block->successor_count > 0) {
        printf("  ; successors: ");
        for (uint8_t i = 0; i < block->successor_count; i++) {
            printf(".BB%u", block->successors[i]);
            if (i < block->successor_count - 1) printf(", ");
        }
        printf("\n");
    }
}

void fcx_ir_print_function(const FcxIRFunction* function) {
    if (!function) return;
    
    printf("\nfunction %s() -> %s {\n", 
           function->name, 
           vreg_type_to_string(function->return_type));
    
    for (uint32_t i = 0; i < function->block_count; i++) {
        fcx_ir_print_block(&function->blocks[i]);
    }
    
    printf("}\n");
}

void fcx_ir_print_module(const FcxIRModule* module) {
    if (!module) return;
    
    printf("=== FCx IR Module: %s ===\n", module->name);
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        fcx_ir_print_function(&module->functions[i]);
    }
    
    printf("\n");
}
