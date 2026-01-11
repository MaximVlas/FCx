#ifndef FCX_LLVM_BACKEND_H
#define FCX_LLVM_BACKEND_H

#include "../ir/fc_ir.h"
#include "../ir/fcx_ir.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/lto.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LLVMBackend LLVMBackend;
typedef struct LLVMFunctionContext LLVMFunctionContext;

typedef enum {
    LLVM_OPT_NONE = 0,
    LLVM_OPT_LESS = 1,
    LLVM_OPT_DEFAULT = 2,
    LLVM_OPT_AGGRESSIVE = 3
} LLVMOptLevel;

typedef enum {
    LLVM_SIZE_DEFAULT = 0,
    LLVM_SIZE_SMALL = 1,
    LLVM_SIZE_VERY_SMALL = 2
} LLVMSizeLevel;

typedef struct {
    LLVMOptLevel opt_level;
    LLVMSizeLevel size_level;
    bool debug_info;
    bool verify_module;
    const char* target_triple;
    const char* cpu;
    const char* features;
} LLVMBackendConfig;

struct LLVMFunctionContext {
    LLVMValueRef function;
    LLVMBasicBlockRef* blocks;
    uint32_t block_count;
    uint32_t block_capacity;
    LLVMValueRef* vreg_values;
    VRegType* vreg_types;           // track the type of each vreg for bigint support
    uint32_t vreg_count;
    uint32_t vreg_capacity;
    LLVMBasicBlockRef current_block;
    LLVMBasicBlockRef* label_blocks;
    uint32_t label_count;
    LLVMValueRef last_cmp_lhs;
    LLVMValueRef last_cmp_rhs;
    bool last_cmp_is_bool;
    uint32_t last_cmp_result_vreg;  // vreg ID that holds the comparison result
    // For loop variable support - use alloca for vregs that need to be mutable
    LLVMValueRef* vreg_allocas;     // alloca pointers for mutable vregs
    bool* vreg_is_mutable;          // track which vregs need alloca
};

struct LLVMBackend {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetRef target;
    LLVMTargetMachineRef target_machine;
    LLVMTargetDataRef target_data;
    LLVMBackendConfig config;
    CpuFeatures cpu_features;
    const FcIRModule* fc_module;
    LLVMFunctionContext* current_func_ctx;
    LLVMTypeRef* type_cache;
    LLVMValueRef* global_strings;
    uint32_t global_string_count;
    LLVMValueRef* external_funcs;
    uint32_t external_func_count;
    uint32_t instruction_count;
    uint32_t function_count;
    uint32_t block_count;
    char error_message[512];
    bool has_error;
};

LLVMBackend* llvm_backend_create(const CpuFeatures* features, const LLVMBackendConfig* config);
void llvm_backend_destroy(LLVMBackend* backend);
void llvm_backend_reset(LLVMBackend* backend);
bool llvm_backend_init_target(LLVMBackend* backend);
const char* llvm_backend_get_error(const LLVMBackend* backend);
bool llvm_emit_module(LLVMBackend* backend, const FcIRModule* module);
bool llvm_emit_function(LLVMBackend* backend, const FcIRFunction* function);
bool llvm_emit_block(LLVMBackend* backend, const FcIRBasicBlock* block);
bool llvm_emit_instruction(LLVMBackend* backend, const FcIRInstruction* instr);
bool llvm_generate_object_file(LLVMBackend* backend, const char* output_path);
bool llvm_generate_assembly(LLVMBackend* backend, const char* output_path);
bool llvm_generate_bitcode(LLVMBackend* backend, const char* output_path);
bool llvm_verify_module(LLVMBackend* backend);
bool llvm_optimize_module(LLVMBackend* backend);
void llvm_print_module(LLVMBackend* backend, FILE* output);
void llvm_print_statistics(const LLVMBackend* backend);
LLVMBackendConfig llvm_default_config(void);
LLVMBackendConfig llvm_debug_config(void);
LLVMBackendConfig llvm_release_config(void);
LLVMBackendConfig llvm_size_config(void);

bool llvm_link_executable(const char* object_path, const char* output_path);
bool llvm_link_shared_library(const char* object_path, const char* output_path);
bool llvm_compile_and_link(LLVMBackend* backend, const char* output_path);
bool llvm_compile_shared_library(LLVMBackend* backend, const char* output_path);

#endif
