#define _POSIX_C_SOURCE 200809L
#include "fc_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Module Management
// ============================================================================

FcIRModule *fc_ir_module_create(const char *name) {
  FcIRModule *module = (FcIRModule *)malloc(sizeof(FcIRModule));
  if (!module)
    return NULL;

  module->name = strdup(name);
  if (!module->name) {
    free(module);
    return NULL;
  }
  
  module->functions = NULL;
  module->function_count = 0;
  module->function_capacity = 0;
  
  // Initialize string literal storage
  module->string_literals = NULL;
  module->string_count = 0;
  module->string_capacity = 0;
  
  // Initialize external function storage
  module->external_functions = NULL;
  module->external_func_count = 0;
  module->external_func_capacity = 0;

  // Initialize with default CPU features
  module->cpu_features.features = CPU_FEATURE_SSE2; // Baseline x86_64
  module->cpu_features.vector_width = 128;
  module->cpu_features.cache_line_size = 64;
  module->cpu_features.red_zone_size = 128;
  module->cpu_features.alignment_pref = 16;

  return module;
}

void fc_ir_module_destroy(FcIRModule *module) {
  if (!module)
    return;

  // Free functions
  if (module->functions && module->function_count > 0) {
    for (uint32_t i = 0; i < module->function_count; i++) {
      fc_ir_function_destroy(&module->functions[i]);
    }
  }
  
  // Free string literals
  if (module->string_literals && module->string_count > 0) {
    for (uint32_t i = 0; i < module->string_count; i++) {
      if (module->string_literals[i].data) {
        free((void*)module->string_literals[i].data);
      }
    }
  }
  free(module->string_literals);
  
  // Free external function names
  if (module->external_functions && module->external_func_count > 0) {
    for (uint32_t i = 0; i < module->external_func_count; i++) {
      if (module->external_functions[i]) {
        free((void*)module->external_functions[i]);
      }
    }
  }
  free(module->external_functions);

  free(module->functions);
  free((void *)module->name);
  free(module);
}

void fc_ir_module_set_cpu_features(FcIRModule *module, CpuFeatures features) {
  if (module) {
    module->cpu_features = features;
  }
}

bool fc_ir_module_add_function(FcIRModule *module, FcIRFunction *function) {
  if (!module || !function)
    return false;
  
  // Validate function has required fields
  if (!function->name)
    return false;

  if (module->function_count >= module->function_capacity) {
    uint32_t new_capacity =
        module->function_capacity == 0 ? 8 : module->function_capacity * 2;
    FcIRFunction *new_functions = (FcIRFunction *)realloc(
        module->functions, new_capacity * sizeof(FcIRFunction));

    if (!new_functions)
      return false;

    module->functions = new_functions;
    module->function_capacity = new_capacity;
  }

  // Shallow copy - caller transfers ownership of function contents
  module->functions[module->function_count++] = *function;
  return true;
}

uint32_t fc_ir_module_add_external_func(FcIRModule* module, const char* func_name) {
  if (!module || !func_name) return UINT32_MAX;
  
  // Check if function already exists
  for (uint32_t i = 0; i < module->external_func_count; i++) {
    if (strcmp(module->external_functions[i], func_name) == 0) {
      return i; // Return existing index
    }
  }
  
  // Add new external function
  if (module->external_func_count >= module->external_func_capacity) {
    uint32_t new_capacity = module->external_func_capacity == 0 ? 8 : module->external_func_capacity * 2;
    const char** new_funcs = (const char**)realloc(module->external_functions, 
                                                   new_capacity * sizeof(const char*));
    if (!new_funcs) return UINT32_MAX;
    
    module->external_functions = new_funcs;
    module->external_func_capacity = new_capacity;
  }
  
  module->external_functions[module->external_func_count] = strdup(func_name);
  return module->external_func_count++;
}

// ============================================================================
// Function Management
// ============================================================================

FcIRFunction *fc_ir_function_create(const char *name, VRegType return_type) {
  FcIRFunction *function = (FcIRFunction *)malloc(sizeof(FcIRFunction));
  if (!function)
    return NULL;

  function->name = strdup(name);
  function->parameters = NULL;
  function->parameter_count = 0;
  function->return_type = return_type;

  function->blocks = NULL;
  function->block_count = 0;
  function->block_capacity = 0;

  fc_ir_init_stack_frame(&function->stack_frame);

  function->next_vreg_id = 1;
  function->next_label_id = 1;
  function->next_block_id = 1;

  function->calling_convention = CALLING_CONV_SYSV_AMD64;

  return function;
}

void fc_ir_function_destroy(FcIRFunction *function) {
  if (!function)
    return;

  for (uint32_t i = 0; i < function->block_count; i++) {
    FcIRBasicBlock *block = &function->blocks[i];
    free(block->instructions);
    free(block->successors);
    free(block->predecessors);
    free((void *)block->name);
  }

  free(function->blocks);
  free(function->parameters);
  free((void *)function->name);
}

// ============================================================================
// Basic Block Management
// ============================================================================

FcIRBasicBlock *fc_ir_block_create(FcIRFunction *function, const char *name) {
  if (!function)
    return NULL;

  if (function->block_count >= function->block_capacity) {
    uint32_t new_capacity =
        function->block_capacity == 0 ? 8 : function->block_capacity * 2;
    FcIRBasicBlock *new_blocks = (FcIRBasicBlock *)realloc(
        function->blocks, new_capacity * sizeof(FcIRBasicBlock));

    if (!new_blocks)
      return NULL;

    function->blocks = new_blocks;
    function->block_capacity = new_capacity;
  }

  FcIRBasicBlock *block = &function->blocks[function->block_count++];
  memset(block, 0, sizeof(FcIRBasicBlock));

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

void fc_ir_block_add_successor(FcIRBasicBlock *block, uint32_t successor_id) {
  if (!block)
    return;

  // Check if already exists
  for (uint8_t i = 0; i < block->successor_count; i++) {
    if (block->successors[i] == successor_id)
      return;
  }

  uint32_t *new_successors = (uint32_t *)realloc(
      block->successors, (block->successor_count + 1) * sizeof(uint32_t));

  if (!new_successors)
    return;

  block->successors = new_successors;
  block->successors[block->successor_count++] = successor_id;
}

// ============================================================================
// Stack Frame Management
// ============================================================================

void fc_ir_init_stack_frame(StackFrame *frame) {
  if (!frame)
    return;

  frame->frame_size = 0;
  frame->local_area_size = 0;
  frame->spill_area_size = 0;
  frame->param_area_size = 0;
  frame->alignment = 16; // x86_64 requires 16-byte alignment
  frame->uses_red_zone = false;
  frame->is_leaf = true;
  frame->needs_frame_pointer = false;
  frame->red_zone_used = 0;
  frame->saved_regs_mask = 0;
  frame->saved_regs_size = 0;
}

int32_t fc_ir_allocate_stack_slot(StackFrame *frame, uint8_t size,
                                  uint8_t alignment) {
  // Validate frame pointer
  if (frame == NULL)
    return -1;

  // Validate size parameter (uint8_t max is 255, which is reasonable)
  if (size == 0)
    return -1;

  // Alignment must be a power of 2 and non-zero
  if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    return -1;

  // Try to use red zone for small leaf functions
  if (frame->is_leaf && frame->red_zone_used + size <= 128) {
    // Safe alignment calculation (alignment is uint8_t, so max 255)
    int32_t align_mask = (int32_t)alignment - 1;
    int32_t aligned_red_zone = (frame->red_zone_used + align_mask) & ~align_mask;
    
    // Check for overflow in aligned_red_zone + size
    if (aligned_red_zone <= 128 - size) {
      int32_t new_red_zone = aligned_red_zone + size;
      int32_t offset = -new_red_zone;
      frame->red_zone_used = new_red_zone;
      frame->uses_red_zone = true;
      return offset;
    }
    // Fall through to regular allocation if doesn't fit in red zone
  }

  // Safe alignment calculation
  int32_t align_mask = (int32_t)alignment - 1;
  int32_t aligned_size = (frame->local_area_size + align_mask) & ~align_mask;
  
  // Check for overflow in aligned_size + size
  if (aligned_size > INT32_MAX - size)
    return -1;
    
  int32_t new_local_size = aligned_size + size;
  int32_t offset = -new_local_size;

  frame->local_area_size = new_local_size;

  return offset;
}

bool fc_ir_can_use_red_zone(const FcIRFunction *function) {
  if (!function)
    return false;

  // Check if function is leaf (no calls)
  for (uint32_t i = 0; i < function->block_count; i++) {
    const FcIRBasicBlock *block = &function->blocks[i];
    for (uint32_t j = 0; j < block->instruction_count; j++) {
      const FcIRInstruction *instr = &block->instructions[j];
      if (instr->opcode == FCIR_CALL || instr->opcode == FCIR_SYSCALL) {
        return false;
      }
    }
  }

  // Check if local variables fit in red zone
  return function->stack_frame.local_area_size <= 128;
}

void fc_ir_compute_frame_layout(FcIRFunction *function) {
  if (!function)
    return;

  StackFrame *frame = &function->stack_frame;

  // Determine if we can use red zone
  frame->is_leaf = fc_ir_can_use_red_zone(function);

  if (frame->is_leaf && frame->local_area_size <= 128) {
    frame->uses_red_zone = true;
    frame->red_zone_used = frame->local_area_size;
    frame->frame_size = 0; // No frame needed
  } else {
    frame->uses_red_zone = false;

    // Compute total frame size
    int32_t total = 0;

    // Saved registers
    total += frame->saved_regs_size;

    // Local variables
    total += frame->local_area_size;

    // Spill area
    total += frame->spill_area_size;

    // Parameter area for calls
    total += frame->param_area_size;

    // Align to 16 bytes
    total = (total + 15) & ~15;

    frame->frame_size = total;
    frame->needs_frame_pointer = (total > 0);
  }
}

// ============================================================================
// FcOperand Creation Helpers
// ============================================================================

FcOperand fc_ir_operand_vreg(VirtualReg vreg) {
  FcOperand op;
  op.type = FC_OPERAND_VREG;
  op.u.vreg = vreg;
  return op;
}

FcOperand fc_ir_operand_imm(int64_t value) {
  FcOperand op;
  op.type = FC_OPERAND_IMMEDIATE;
  op.u.immediate = value;
  return op;
}

FcOperand fc_ir_operand_bigint(const uint64_t* limbs, uint8_t num_limbs) {
  FcOperand op;
  op.type = FC_OPERAND_BIGINT;
  op.u.bigint.num_limbs = num_limbs;
  for (uint8_t i = 0; i < 16; i++) {
    op.u.bigint.limbs[i] = (i < num_limbs) ? limbs[i] : 0;
  }
  return op;
}

FcOperand fc_ir_operand_mem(VirtualReg base, VirtualReg index, int32_t disp,
                            uint8_t scale) {
  FcOperand op;
  op.type = FC_OPERAND_MEMORY;
  op.u.memory.base = base;
  op.u.memory.index = index;
  op.u.memory.displacement = disp;
  op.u.memory.scale = scale;
  op.u.memory.is_rip_relative = false;
  return op;
}

FcOperand fc_ir_operand_label(uint32_t label_id) {
  FcOperand op;
  op.type = FC_OPERAND_LABEL;
  op.u.label_id = label_id;
  return op;
}

FcOperand fc_ir_operand_stack_slot(int32_t offset, uint8_t size) {
  FcOperand op;
  op.type = FC_OPERAND_STACK_SLOT;
  op.u.stack_slot.offset = offset;
  op.u.stack_slot.size = size;
  op.u.stack_slot.alignment = (size >= 8) ? 8 : size;
  return op;
}

FcOperand fc_ir_operand_external_func(uint32_t func_id) {
  FcOperand op;
  op.type = FC_OPERAND_EXTERNAL_FUNC;
  op.u.external_func_id = func_id;
  return op;
}

// ============================================================================
// Helper: Add instruction to basic block
// ============================================================================

static void add_instruction(FcIRBasicBlock *block, FcIRInstruction instr) {
  if (!block)
    return;

  if (block->instruction_count >= block->instruction_capacity) {
    uint32_t new_capacity =
        block->instruction_capacity == 0 ? 16 : block->instruction_capacity * 2;
    FcIRInstruction *new_instructions = (FcIRInstruction *)realloc(
        block->instructions, new_capacity * sizeof(FcIRInstruction));

    if (!new_instructions)
      return;

    block->instructions = new_instructions;
    block->instruction_capacity = new_capacity;
  }

  block->instructions[block->instruction_count++] = instr;
}

// ============================================================================
// Instruction Building Functions
// ============================================================================

void fc_ir_build_mov(FcIRBasicBlock *block, FcOperand dest, FcOperand src) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_MOV;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  add_instruction(block, instr);
}

void fc_ir_build_lea(FcIRBasicBlock *block, FcOperand dest, FcOperand src) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_LEA;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  add_instruction(block, instr);
}

void fc_ir_build_push(FcIRBasicBlock *block, FcOperand src) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_PUSH;
  instr.operand_count = 1;
  instr.operands[0] = src;
  add_instruction(block, instr);
}

void fc_ir_build_pop(FcIRBasicBlock *block, FcOperand dest) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_POP;
  instr.operand_count = 1;
  instr.operands[0] = dest;
  add_instruction(block, instr);
}

void fc_ir_build_binary_op(FcIRBasicBlock *block, FcIROpcode opcode,
                           FcOperand dest, FcOperand src) {
  FcIRInstruction instr = {0};
  instr.opcode = opcode;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  add_instruction(block, instr);
}

void fc_ir_build_unary_op(FcIRBasicBlock *block, FcIROpcode opcode,
                          FcOperand operand) {
  FcIRInstruction instr = {0};
  instr.opcode = opcode;
  instr.operand_count = 1;
  instr.operands[0] = operand;
  add_instruction(block, instr);
}

void fc_ir_build_cmp(FcIRBasicBlock *block, FcOperand left, FcOperand right) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_CMP;
  instr.operand_count = 2;
  instr.operands[0] = left;
  instr.operands[1] = right;
  add_instruction(block, instr);
}

void fc_ir_build_test(FcIRBasicBlock *block, FcOperand left, FcOperand right) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_TEST;
  instr.operand_count = 2;
  instr.operands[0] = left;
  instr.operands[1] = right;
  add_instruction(block, instr);
}

// ============================================================================
// Control Flow Instructions
// ============================================================================

void fc_ir_build_jmp(FcIRBasicBlock *block, uint32_t label_id) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_JMP;
  instr.operand_count = 1;
  instr.operands[0] = fc_ir_operand_label(label_id);
  add_instruction(block, instr);
}

void fc_ir_build_jcc(FcIRBasicBlock *block, FcIROpcode condition,
                     uint32_t label_id) {
  FcIRInstruction instr = {0};
  instr.opcode = condition;
  instr.operand_count = 1;
  instr.operands[0] = fc_ir_operand_label(label_id);
  add_instruction(block, instr);
}

void fc_ir_build_call(FcIRBasicBlock *block, const char *function) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_CALL;
  instr.operand_count = 1;
  
  // Store function name hash as the label ID
  // This will be resolved during linking using the symbol table
  // Use a simple hash to identify the function
  uint32_t hash = 0;
  if (function) {
    const char* p = function;
    while (*p) {
      hash = hash * 31 + (uint8_t)*p++;
    }
  }
  
  instr.operands[0].type = FC_OPERAND_LABEL;
  instr.operands[0].u.label_id = hash;
  add_instruction(block, instr);
}

void fc_ir_build_call_external(FcIRBasicBlock* block, FcIRModule* module, const char* function) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_CALL;
  instr.operand_count = 1;
  
  // Add function to external function table and get its ID
  uint32_t func_id = fc_ir_module_add_external_func(module, function);
  
  instr.operands[0] = fc_ir_operand_external_func(func_id);
  add_instruction(block, instr);
}

void fc_ir_build_ret(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_RET;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

void fc_ir_build_syscall(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_SYSCALL;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

// ============================================================================
// Atomic Operations
// ============================================================================

void fc_ir_build_lock_prefix(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_LOCK;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

void fc_ir_build_cmpxchg(FcIRBasicBlock *block, FcOperand dest, FcOperand src,
                         bool locked) {
  if (locked) {
    fc_ir_build_lock_prefix(block);
  }

  FcIRInstruction instr = {0};
  instr.opcode = FCIR_CMPXCHG;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  if (locked) {
    instr.flags |= FCIR_FLAG_LOCK;
  }
  add_instruction(block, instr);
}

void fc_ir_build_xchg(FcIRBasicBlock *block, FcOperand dest, FcOperand src,
                      bool locked) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_XCHG;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  if (locked) {
    instr.flags |= FCIR_FLAG_LOCK;
  }
  add_instruction(block, instr);
}

void fc_ir_build_xadd(FcIRBasicBlock *block, FcOperand dest, FcOperand src,
                      bool locked) {
  if (locked) {
    fc_ir_build_lock_prefix(block);
  }

  FcIRInstruction instr = {0};
  instr.opcode = FCIR_XADD;
  instr.operand_count = 2;
  instr.operands[0] = dest;
  instr.operands[1] = src;
  if (locked) {
    instr.flags |= FCIR_FLAG_LOCK;
  }
  add_instruction(block, instr);
}

// ============================================================================
// Memory Barriers
// ============================================================================

void fc_ir_build_mfence(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_MFENCE;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

void fc_ir_build_lfence(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_LFENCE;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

void fc_ir_build_sfence(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_SFENCE;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

void fc_ir_build_prefetch(FcIRBasicBlock *block, FcOperand addr, int hint) {
  FcIRInstruction instr = {0};
  // Use PREFETCHT0 for read (hint=0), PREFETCHW for write (hint=1)
  instr.opcode = (hint == 1) ? FCIR_PREFETCHW : FCIR_PREFETCHT0;
  instr.operand_count = 1;
  instr.operands[0] = addr;
  add_instruction(block, instr);
}

void fc_ir_build_inline_asm_raw(FcIRBasicBlock *block, int64_t asm_data_ptr) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_INLINE_ASM;
  instr.operand_count = 1;
  instr.operands[0].type = FC_OPERAND_IMMEDIATE;
  instr.operands[0].u.immediate = asm_data_ptr;
  add_instruction(block, instr);
}

// ============================================================================
// Stack Frame Operations
// ============================================================================

void fc_ir_build_enter(FcIRBasicBlock *block, uint16_t frame_size) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_ENTER;
  instr.operand_count = 1;
  instr.operands[0] = fc_ir_operand_imm(frame_size);
  add_instruction(block, instr);
}

void fc_ir_build_leave(FcIRBasicBlock *block) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_LEAVE;
  instr.operand_count = 0;
  add_instruction(block, instr);
}

// ============================================================================
// Labels
// ============================================================================

void fc_ir_build_label(FcIRBasicBlock *block, uint32_t label_id) {
  FcIRInstruction instr = {0};
  instr.opcode = FCIR_LABEL;
  instr.operand_count = 1;
  instr.operands[0] = fc_ir_operand_label(label_id);
  add_instruction(block, instr);
}

// ============================================================================
// CPU Feature Detection
// ============================================================================

#ifdef __x86_64__
#include <cpuid.h>

CpuFeatures fc_ir_detect_cpu_features(void) {
  CpuFeatures features = {0};

  // Set baseline features
  features.features = CPU_FEATURE_SSE2; // Required for x86_64
  features.vector_width = 128;
  features.cache_line_size = 64;
  features.red_zone_size = 128;
  features.alignment_pref = 16;

  uint32_t eax, ebx, ecx, edx;

  // Check CPUID support
  if (__get_cpuid_max(0, NULL) >= 1) {
    __cpuid(1, eax, ebx, ecx, edx);

    // ECX features
    if (ecx & bit_SSE3)
      features.features |= CPU_FEATURE_SSE3;
    if (ecx & bit_SSSE3)
      features.features |= CPU_FEATURE_SSSE3;
    if (ecx & bit_SSE4_1)
      features.features |= CPU_FEATURE_SSE4_1;
    if (ecx & bit_SSE4_2)
      features.features |= CPU_FEATURE_SSE4_2;
    if (ecx & bit_POPCNT)
      features.features |= CPU_FEATURE_POPCNT;
    if (ecx & bit_AVX)
      features.features |= CPU_FEATURE_AVX;

    // Check extended features
    if (__get_cpuid_max(0, NULL) >= 7) {
      __cpuid_count(7, 0, eax, ebx, ecx, edx);

      if (ebx & bit_AVX2) {
        features.features |= CPU_FEATURE_AVX2;
        features.vector_width = 256;
      }
      if (ebx & bit_BMI)
        features.features |= CPU_FEATURE_BMI1;
      if (ebx & bit_BMI2)
        features.features |= CPU_FEATURE_BMI2;
      if (ebx & bit_AVX512F) {
        features.features |= CPU_FEATURE_AVX512F;
        features.vector_width = 512;
      }
    }

    // Check extended CPUID for LZCNT
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000001) {
      __cpuid(0x80000001, eax, ebx, ecx, edx);
      if (ecx & bit_LZCNT)
        features.features |= CPU_FEATURE_LZCNT;
    }
  }

  return features;
}
#else
// Fallback for non-x86_64 platforms
CpuFeatures fc_ir_detect_cpu_features(void) {
  CpuFeatures features = {0};
  features.features = 0;
  features.vector_width = 0;
  features.cache_line_size = 64;
  features.red_zone_size = 0;
  features.alignment_pref = 8;
  return features;
}
#endif

bool fc_ir_has_feature(const CpuFeatures *features, uint64_t feature_flag) {
  return features && (features->features & feature_flag) != 0;
}

// ============================================================================
// Debugging and Printing Functions
// ============================================================================

static const char *opcode_to_string(FcIROpcode opcode) {
  switch (opcode) {
  case FCIR_MOV:
    return "mov";
  case FCIR_MOVZX:
    return "movzx";
  case FCIR_MOVSX:
    return "movsx";
  case FCIR_LEA:
    return "lea";
  case FCIR_PUSH:
    return "push";
  case FCIR_POP:
    return "pop";
  case FCIR_ADD:
    return "add";
  case FCIR_SUB:
    return "sub";
  case FCIR_IMUL:
    return "imul";
  case FCIR_IDIV:
    return "idiv";
  case FCIR_NEG:
    return "neg";
  case FCIR_INC:
    return "inc";
  case FCIR_DEC:
    return "dec";
  case FCIR_AND:
    return "and";
  case FCIR_OR:
    return "or";
  case FCIR_XOR:
    return "xor";
  case FCIR_NOT:
    return "not";
  case FCIR_TEST:
    return "test";
  case FCIR_SHL:
    return "shl";
  case FCIR_SHR:
    return "shr";
  case FCIR_SAR:
    return "sar";
  case FCIR_ROL:
    return "rol";
  case FCIR_ROR:
    return "ror";
  case FCIR_CMP:
    return "cmp";
  case FCIR_MFENCE:
    return "mfence";
  case FCIR_LFENCE:
    return "lfence";
  case FCIR_SFENCE:
    return "sfence";
  case FCIR_PREFETCHT0:
    return "prefetcht0";
  case FCIR_PREFETCHT1:
    return "prefetcht1";
  case FCIR_PREFETCHT2:
    return "prefetcht2";
  case FCIR_PREFETCHNTA:
    return "prefetchnta";
  case FCIR_PREFETCHW:
    return "prefetchw";
  case FCIR_LOCK:
    return "lock";
  case FCIR_CMPXCHG:
    return "cmpxchg";
  case FCIR_XCHG:
    return "xchg";
  case FCIR_XADD:
    return "xadd";
  case FCIR_BTS:
    return "bts";
  case FCIR_BTR:
    return "btr";
  case FCIR_BTC:
    return "btc";
  case FCIR_BSF:
    return "bsf";
  case FCIR_BSR:
    return "bsr";
  case FCIR_JMP:
    return "jmp";
  case FCIR_JE:
    return "je";
  case FCIR_JNE:
    return "jne";
  case FCIR_JL:
    return "jl";
  case FCIR_JLE:
    return "jle";
  case FCIR_JG:
    return "jg";
  case FCIR_JGE:
    return "jge";
  case FCIR_JA:
    return "ja";
  case FCIR_JB:
    return "jb";
  case FCIR_JAE:
    return "jae";
  case FCIR_JBE:
    return "jbe";
  case FCIR_CALL:
    return "call";
  case FCIR_RET:
    return "ret";
  case FCIR_SYSCALL:
    return "syscall";
  case FCIR_LABEL:
    return "label";
  case FCIR_ALIGN:
    return "align";
  case FCIR_ENTER:
    return "enter";
  case FCIR_LEAVE:
    return "leave";
  case FCIR_INLINE_ASM:
    return "inline_asm";
  default:
    return "unknown";
  }
}

static void print_operand(const FcOperand *op) {
  if (!op)
    return;

  switch (op->type) {
  case FC_OPERAND_VREG:
    printf("%%v%u", op->u.vreg.id);
    break;

  case FC_OPERAND_IMMEDIATE:
    printf("$%ld", op->u.immediate);
    break;

  case FC_OPERAND_BIGINT:
    printf("$0x");
    // Print limbs in big-endian order (most significant first)
    for (int i = op->u.bigint.num_limbs - 1; i >= 0; i--) {
      if (i == op->u.bigint.num_limbs - 1) {
        printf("%lx", (unsigned long)op->u.bigint.limbs[i]);
      } else {
        printf("%016lx", (unsigned long)op->u.bigint.limbs[i]);
      }
    }
    break;

  case FC_OPERAND_MEMORY:
    printf("[");
    if (op->u.memory.base.id != 0) {
      printf("%%v%u", op->u.memory.base.id);
    }
    if (op->u.memory.index.id != 0) {
      printf(" + %%v%u", op->u.memory.index.id);
      if (op->u.memory.scale > 1) {
        printf("*%u", op->u.memory.scale);
      }
    }
    if (op->u.memory.displacement != 0) {
      printf(" %+d", op->u.memory.displacement);
    }
    printf("]");
    break;

  case FC_OPERAND_LABEL:
    printf(".L%u", op->u.label_id);
    break;

  case FC_OPERAND_STACK_SLOT:
    printf("[rbp %+d]", op->u.stack_slot.offset);
    break;

  case FC_OPERAND_EXTERNAL_FUNC:
    printf("@func_%u", op->u.external_func_id);
    break;
  }
}

void fc_ir_print_instruction(const FcIRInstruction *instr) {
  if (!instr)
    return;

  printf("  ");

  if (instr->flags & FCIR_FLAG_LOCK) {
    printf("lock ");
  }

  printf("%s", opcode_to_string(instr->opcode));

  for (uint8_t i = 0; i < instr->operand_count; i++) {
    printf(" ");
    print_operand(&instr->operands[i]);
    if (i < instr->operand_count - 1) {
      printf(",");
    }
  }

  printf("\n");
}

void fc_ir_print_block(const FcIRBasicBlock *block) {
  if (!block)
    return;

  printf("\n.BB%u", block->id);
  if (block->name) {
    printf(" (%s)", block->name);
  }
  printf(":\n");

  for (uint32_t i = 0; i < block->instruction_count; i++) {
    fc_ir_print_instruction(&block->instructions[i]);
  }
}

void fc_ir_print_function(const FcIRFunction *function) {
  if (!function)
    return;

  printf("\nfunction %s:\n", function->name);
  printf("  ; calling convention: %s\n",
         function->calling_convention == CALLING_CONV_SYSV_AMD64
             ? "System V AMD64"
         : function->calling_convention == CALLING_CONV_FASTCALL ? "fastcall"
         : function->calling_convention == CALLING_CONV_SYSCALL  ? "syscall"
                                                                : "vectorcall");

  if (function->stack_frame.uses_red_zone) {
    printf("  ; uses red zone: %d bytes\n",
           function->stack_frame.red_zone_used);
  } else if (function->stack_frame.frame_size > 0) {
    printf("  ; frame size: %d bytes\n", function->stack_frame.frame_size);
  }

  for (uint32_t i = 0; i < function->block_count; i++) {
    fc_ir_print_block(&function->blocks[i]);
  }

  printf("\n");
}

void fc_ir_print_module(const FcIRModule *module) {
  if (!module)
    return;

  printf("=== FC IR Module: %s ===\n", module->name);
  printf("CPU Features: 0x%lx\n", module->cpu_features.features);
  printf("Vector Width: %u bits\n", module->cpu_features.vector_width);
  printf("Red Zone: %u bytes\n", module->cpu_features.red_zone_size);

  for (uint32_t i = 0; i < module->function_count; i++) {
    fc_ir_print_function(&module->functions[i]);
  }

  printf("\n");
}
