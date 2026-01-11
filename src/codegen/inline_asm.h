#ifndef FCX_INLINE_ASM_H
#define FCX_INLINE_ASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Inline assembly constraint types
typedef enum {
  ASM_CONSTRAINT_REGISTER,    // Register constraint (e.g., "r", "a", "b")
  ASM_CONSTRAINT_MEMORY,      // Memory constraint (e.g., "m")
  ASM_CONSTRAINT_IMMEDIATE,   // Immediate value (e.g., "i")
  ASM_CONSTRAINT_SPECIFIC_REG // Specific register (e.g., "rax", "rbx")
} AsmConstraintType;

// Register allocation hints for inline assembly
typedef enum {
  ASM_REG_ANY,      // Any general-purpose register
  ASM_REG_RAX,      // RAX register
  ASM_REG_RBX,      // RBX register
  ASM_REG_RCX,      // RCX register
  ASM_REG_RDX,      // RDX register
  ASM_REG_RSI,      // RSI register
  ASM_REG_RDI,      // RDI register
  ASM_REG_R8,       // R8 register
  ASM_REG_R9,       // R9 register
  ASM_REG_R10,      // R10 register
  ASM_REG_R11,      // R11 register
  ASM_REG_XMM0,     // XMM0 (SSE)
  ASM_REG_XMM1,     // XMM1 (SSE)
  ASM_REG_YMM0,     // YMM0 (AVX)
  ASM_REG_ZMM0,     // ZMM0 (AVX-512)
} AsmRegisterHint;

// Inline assembly operand
typedef struct {
  AsmConstraintType constraint_type;
  AsmRegisterHint register_hint;
  const char *constraint_string; // GCC-style constraint string
  uint32_t operand_id;           // Operand number (%0, %1, etc.)
  bool is_input;                 // Input or output operand
  bool is_clobbered;             // Register is clobbered
  const char *symbolic_name;     // Optional symbolic name
} AsmOperand;

// Inline assembly block
typedef struct {
  const char *asm_template;      // Assembly template string
  AsmOperand *operands;          // Array of operands
  uint32_t operand_count;        // Number of operands
  const char **clobbers;         // Clobbered registers
  uint32_t clobber_count;        // Number of clobbers
  bool is_volatile;              // Volatile assembly (no optimization)
  bool goto_labels;              // Has goto labels
  uint64_t required_cpu_features; // Required CPU features
} InlineAsmBlock;

// CPU feature flags for inline assembly
#define ASM_FEATURE_SSE (1ULL << 0)
#define ASM_FEATURE_SSE2 (1ULL << 1)
#define ASM_FEATURE_SSE3 (1ULL << 2)
#define ASM_FEATURE_SSSE3 (1ULL << 3)
#define ASM_FEATURE_SSE4_1 (1ULL << 4)
#define ASM_FEATURE_SSE4_2 (1ULL << 5)
#define ASM_FEATURE_AVX (1ULL << 6)
#define ASM_FEATURE_AVX2 (1ULL << 7)
#define ASM_FEATURE_AVX512F (1ULL << 8)
#define ASM_FEATURE_BMI1 (1ULL << 9)
#define ASM_FEATURE_BMI2 (1ULL << 10)
#define ASM_FEATURE_POPCNT (1ULL << 11)
#define ASM_FEATURE_LZCNT (1ULL << 12)
#define ASM_FEATURE_AES (1ULL << 13)
#define ASM_FEATURE_RDRAND (1ULL << 14)
#define ASM_FEATURE_RDSEED (1ULL << 15)

// Create inline assembly block
InlineAsmBlock *inline_asm_create(const char *asm_template);

// Add operand to inline assembly
void inline_asm_add_operand(InlineAsmBlock *block, const char *constraint,
                            bool is_input, const char *symbolic_name);

// Add clobber to inline assembly
void inline_asm_add_clobber(InlineAsmBlock *block, const char *clobber);

// Set CPU feature requirements
void inline_asm_set_features(InlineAsmBlock *block, uint64_t features);

// Validate inline assembly block
bool inline_asm_validate(const InlineAsmBlock *block);

// Generate assembly code from inline assembly block
bool inline_asm_emit(const InlineAsmBlock *block, FILE *output);

// Parse constraint string (GCC-style)
AsmConstraintType inline_asm_parse_constraint(const char *constraint,
                                               AsmRegisterHint *hint);

// Check if CPU features are available
bool inline_asm_check_features(uint64_t required_features);

// Destroy inline assembly block
void inline_asm_destroy(InlineAsmBlock *block);

// Detect available CPU features at runtime
uint64_t inline_asm_detect_cpu_features(void);

// Get register name from hint
const char *inline_asm_get_register_name(AsmRegisterHint hint);

#endif // FCX_INLINE_ASM_H
