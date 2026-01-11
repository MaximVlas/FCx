#include "inline_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Create inline assembly block
InlineAsmBlock *inline_asm_create(const char *asm_template) {
  InlineAsmBlock *block = malloc(sizeof(InlineAsmBlock));
  if (!block) {
    return NULL;
  }

  block->asm_template = asm_template;
  block->operands = NULL;
  block->operand_count = 0;
  block->clobbers = NULL;
  block->clobber_count = 0;
  block->is_volatile = false;
  block->goto_labels = false;
  block->required_cpu_features = 0;

  return block;
}

// Add operand to inline assembly
void inline_asm_add_operand(InlineAsmBlock *block, const char *constraint,
                            bool is_input, const char *symbolic_name) {
  if (!block) {
    return;
  }

  // Reallocate operands array
  AsmOperand *new_operands =
      realloc(block->operands, sizeof(AsmOperand) * (block->operand_count + 1));
  if (!new_operands) {
    return;
  }

  block->operands = new_operands;

  // Initialize new operand
  AsmOperand *operand = &block->operands[block->operand_count];
  operand->constraint_string = constraint;
  operand->operand_id = block->operand_count;
  operand->is_input = is_input;
  operand->is_clobbered = false;
  operand->symbolic_name = symbolic_name;

  // Parse constraint
  operand->constraint_type =
      inline_asm_parse_constraint(constraint, &operand->register_hint);

  block->operand_count++;
}

// Add clobber to inline assembly
void inline_asm_add_clobber(InlineAsmBlock *block, const char *clobber) {
  if (!block) {
    return;
  }

  // Reallocate clobbers array
  const char **new_clobbers =
      realloc(block->clobbers, sizeof(const char *) * (block->clobber_count + 1));
  if (!new_clobbers) {
    return;
  }

  block->clobbers = new_clobbers;
  block->clobbers[block->clobber_count] = clobber;
  block->clobber_count++;
}

// Set CPU feature requirements
void inline_asm_set_features(InlineAsmBlock *block, uint64_t features) {
  if (block) {
    block->required_cpu_features = features;
  }
}

// Parse constraint string (GCC-style)
AsmConstraintType inline_asm_parse_constraint(const char *constraint,
                                               AsmRegisterHint *hint) {
  if (!constraint || !hint) {
    *hint = ASM_REG_ANY;
    return ASM_CONSTRAINT_REGISTER;
  }

  // Skip modifiers (=, +, &)
  while (*constraint == '=' || *constraint == '+' || *constraint == '&') {
    constraint++;
  }

  // Parse constraint type
  switch (*constraint) {
  case 'r': // General register
    *hint = ASM_REG_ANY;
    return ASM_CONSTRAINT_REGISTER;

  case 'a': // RAX
    *hint = ASM_REG_RAX;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'b': // RBX
    *hint = ASM_REG_RBX;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'c': // RCX
    *hint = ASM_REG_RCX;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'd': // RDX
    *hint = ASM_REG_RDX;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'S': // RSI
    *hint = ASM_REG_RSI;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'D': // RDI
    *hint = ASM_REG_RDI;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  case 'm': // Memory
    *hint = ASM_REG_ANY;
    return ASM_CONSTRAINT_MEMORY;

  case 'i': // Immediate
    *hint = ASM_REG_ANY;
    return ASM_CONSTRAINT_IMMEDIATE;

  case 'x': // XMM register
    *hint = ASM_REG_XMM0;
    return ASM_CONSTRAINT_SPECIFIC_REG;

  default:
    *hint = ASM_REG_ANY;
    return ASM_CONSTRAINT_REGISTER;
  }
}

// Get register name from hint
const char *inline_asm_get_register_name(AsmRegisterHint hint) {
  switch (hint) {
  case ASM_REG_RAX:
    return "rax";
  case ASM_REG_RBX:
    return "rbx";
  case ASM_REG_RCX:
    return "rcx";
  case ASM_REG_RDX:
    return "rdx";
  case ASM_REG_RSI:
    return "rsi";
  case ASM_REG_RDI:
    return "rdi";
  case ASM_REG_R8:
    return "r8";
  case ASM_REG_R9:
    return "r9";
  case ASM_REG_R10:
    return "r10";
  case ASM_REG_R11:
    return "r11";
  case ASM_REG_XMM0:
    return "xmm0";
  case ASM_REG_XMM1:
    return "xmm1";
  case ASM_REG_YMM0:
    return "ymm0";
  case ASM_REG_ZMM0:
    return "zmm0";
  default:
    return "rax"; // Default to rax
  }
}

// Validate inline assembly block
bool inline_asm_validate(const InlineAsmBlock *block) {
  if (!block || !block->asm_template) {
    return false;
  }

  // Check if required CPU features are available
  if (block->required_cpu_features != 0) {
    if (!inline_asm_check_features(block->required_cpu_features)) {
      return false;
    }
  }

  // Validate operands
  for (uint32_t i = 0; i < block->operand_count; i++) {
    const AsmOperand *operand = &block->operands[i];
    if (!operand->constraint_string) {
      return false;
    }
  }

  return true;
}

// Generate assembly code from inline assembly block
bool inline_asm_emit(const InlineAsmBlock *block, FILE *output) {
  if (!block || !output) {
    return false;
  }

  // Validate block first
  if (!inline_asm_validate(block)) {
    return false;
  }

  // Emit comment
  fprintf(output, "    # Inline assembly block\n");

  // Emit assembly template
  const char *template = block->asm_template;
  const char *ptr = template;

  while (*ptr) {
    if (*ptr == '%' && *(ptr + 1) >= '0' && *(ptr + 1) <= '9') {
      // Operand reference
      uint32_t operand_id = *(ptr + 1) - '0';
      if (operand_id < block->operand_count) {
        const AsmOperand *operand = &block->operands[operand_id];
        if (operand->constraint_type == ASM_CONSTRAINT_SPECIFIC_REG) {
          fprintf(output, "%%%s", inline_asm_get_register_name(operand->register_hint));
        } else {
          fprintf(output, "%%rax"); // Default register
        }
      }
      ptr += 2;
    } else if (*ptr == '\n') {
      fprintf(output, "\n    ");
      ptr++;
    } else {
      fputc(*ptr, output);
      ptr++;
    }
  }

  fprintf(output, "\n");

  return true;
}

// Detect available CPU features at runtime
uint64_t inline_asm_detect_cpu_features(void) {
  uint64_t features = 0;

#ifdef __x86_64__
  // Use CPUID to detect features
  uint32_t eax, ebx, ecx, edx;

  // Check for SSE/SSE2 (standard on x86_64)
  features |= ASM_FEATURE_SSE | ASM_FEATURE_SSE2;

  // CPUID function 1: Feature flags
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(1));

  if (ecx & (1 << 0))
    features |= ASM_FEATURE_SSE3;
  if (ecx & (1 << 9))
    features |= ASM_FEATURE_SSSE3;
  if (ecx & (1 << 19))
    features |= ASM_FEATURE_SSE4_1;
  if (ecx & (1 << 20))
    features |= ASM_FEATURE_SSE4_2;
  if (ecx & (1 << 23))
    features |= ASM_FEATURE_POPCNT;
  if (ecx & (1 << 25))
    features |= ASM_FEATURE_AES;
  if (ecx & (1 << 28))
    features |= ASM_FEATURE_AVX;
  if (ecx & (1 << 30))
    features |= ASM_FEATURE_RDRAND;

  // CPUID function 7: Extended features
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(7), "c"(0));

  if (ebx & (1 << 3))
    features |= ASM_FEATURE_BMI1;
  if (ebx & (1 << 5))
    features |= ASM_FEATURE_AVX2;
  if (ebx & (1 << 8))
    features |= ASM_FEATURE_BMI2;
  if (ebx & (1 << 16))
    features |= ASM_FEATURE_AVX512F;
  if (ebx & (1 << 18))
    features |= ASM_FEATURE_RDSEED;

  // CPUID function 0x80000001: Extended processor info
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000001));

  if (ecx & (1 << 5))
    features |= ASM_FEATURE_LZCNT;
#endif

  return features;
}

// Check if CPU features are available
bool inline_asm_check_features(uint64_t required_features) {
  static uint64_t detected_features = 0;
  static bool features_detected = false;

  if (!features_detected) {
    detected_features = inline_asm_detect_cpu_features();
    features_detected = true;
  }

  return (detected_features & required_features) == required_features;
}

// Destroy inline assembly block
void inline_asm_destroy(InlineAsmBlock *block) {
  if (!block) {
    return;
  }

  if (block->operands) {
    free(block->operands);
  }

  if (block->clobbers) {
    free(block->clobbers);
  }

  free(block);
}
