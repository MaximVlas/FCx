#include "llvm_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static void set_error(LLVMBackend* backend, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(backend->error_message, sizeof(backend->error_message), fmt, args);
    va_end(args);
    backend->has_error = true;
}

static const char* build_target_features(const CpuFeatures* features) {
    static char feature_str[256];
    feature_str[0] = '\0';
    if (features->features & CPU_FEATURE_SSE2) strcat(feature_str, "+sse2,");
    if (features->features & CPU_FEATURE_SSE3) strcat(feature_str, "+sse3,");
    if (features->features & CPU_FEATURE_SSSE3) strcat(feature_str, "+ssse3,");
    if (features->features & CPU_FEATURE_SSE4_1) strcat(feature_str, "+sse4.1,");
    if (features->features & CPU_FEATURE_SSE4_2) strcat(feature_str, "+sse4.2,");
    if (features->features & CPU_FEATURE_AVX) strcat(feature_str, "+avx,");
    if (features->features & CPU_FEATURE_AVX2) strcat(feature_str, "+avx2,");
    if (features->features & CPU_FEATURE_AVX512F) strcat(feature_str, "+avx512f,");
    if (features->features & CPU_FEATURE_BMI1) strcat(feature_str, "+bmi,");
    if (features->features & CPU_FEATURE_BMI2) strcat(feature_str, "+bmi2,");
    if (features->features & CPU_FEATURE_POPCNT) strcat(feature_str, "+popcnt,");
    if (features->features & CPU_FEATURE_LZCNT) strcat(feature_str, "+lzcnt,");
    size_t len = strlen(feature_str);
    if (len > 0 && feature_str[len - 1] == ',') feature_str[len - 1] = '\0';
    return feature_str;
}

LLVMBackendConfig llvm_default_config(void) {
    return (LLVMBackendConfig){
        .opt_level = LLVM_OPT_DEFAULT, .size_level = LLVM_SIZE_DEFAULT,
        .debug_info = false, .verify_module = true,
        .target_triple = "x86_64-pc-linux-gnu", .cpu = "x86-64", .features = ""
    };
}

LLVMBackendConfig llvm_debug_config(void) {
    LLVMBackendConfig c = llvm_default_config();
    c.opt_level = LLVM_OPT_NONE; c.debug_info = true;
    return c;
}

LLVMBackendConfig llvm_release_config(void) {
    LLVMBackendConfig c = llvm_default_config();
    c.opt_level = LLVM_OPT_AGGRESSIVE; c.verify_module = false;
    return c;
}

LLVMBackendConfig llvm_size_config(void) {
    LLVMBackendConfig c = llvm_default_config();
    c.size_level = LLVM_SIZE_SMALL;
    return c;
}

LLVMBackend* llvm_backend_create(const CpuFeatures* features, const LLVMBackendConfig* config) {
    LLVMBackend* b = calloc(1, sizeof(LLVMBackend));
    if (!b) return NULL;
    
    b->config = config ? *config : llvm_default_config();
    if (features) {
        b->cpu_features = *features;
        if (b->config.features[0] == '\0') b->config.features = build_target_features(features);
    }
    
    b->context = LLVMContextCreate();
    if (!b->context) { free(b); return NULL; }
    
    b->builder = LLVMCreateBuilderInContext(b->context);
    if (!b->builder) { LLVMContextDispose(b->context); free(b); return NULL; }
    
    if (!llvm_backend_init_target(b)) {
        LLVMDisposeBuilder(b->builder);
        LLVMContextDispose(b->context);
        free(b);
        return NULL;
    }
    return b;
}

bool llvm_backend_init_target(LLVMBackend* b) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
    
    char* error = NULL;
    if (LLVMGetTargetFromTriple(b->config.target_triple, &b->target, &error)) {
        set_error(b, "Target error: %s", error ? error : "unknown");
        LLVMDisposeMessage(error);
        return false;
    }
    
    LLVMCodeGenOptLevel opt = LLVMCodeGenLevelDefault;
    if (b->config.opt_level == LLVM_OPT_NONE) opt = LLVMCodeGenLevelNone;
    else if (b->config.opt_level == LLVM_OPT_AGGRESSIVE) opt = LLVMCodeGenLevelAggressive;
    
    b->target_machine = LLVMCreateTargetMachine(b->target, b->config.target_triple,
        b->config.cpu, b->config.features, opt, LLVMRelocPIC, LLVMCodeModelDefault);
    if (!b->target_machine) { set_error(b, "Failed to create target machine"); return false; }
    
    b->target_data = LLVMCreateTargetDataLayout(b->target_machine);
    return true;
}

void llvm_backend_destroy(LLVMBackend* b) {
    if (!b) return;
    
    // Clean up current function context first
    if (b->current_func_ctx) {
        // Free all function context resources safely
        free(b->current_func_ctx->blocks);
        free(b->current_func_ctx->vreg_values);
        free(b->current_func_ctx->vreg_allocas);
        free(b->current_func_ctx->vreg_is_mutable);
        free(b->current_func_ctx->vreg_types);
        free(b->current_func_ctx->label_blocks);
        free(b->current_func_ctx);
        b->current_func_ctx = NULL;
    }
    
    // Free global resources
    free(b->global_strings);
    b->global_strings = NULL;
    b->global_string_count = 0;
    
    free(b->external_funcs);
    b->external_funcs = NULL;
    b->external_func_count = 0;
    
    // LLVM objects must be disposed in reverse creation order
    // Modules depend on builders and contexts
    if (b->module) {
        LLVMDisposeModule(b->module);
        b->module = NULL;
    }
    
    // Builders depend on contexts
    if (b->builder) {
        LLVMDisposeBuilder(b->builder);
        b->builder = NULL;
    }
    
    // Target machine and data depend on context
    if (b->target_machine) {
        LLVMDisposeTargetMachine(b->target_machine);
        b->target_machine = NULL;
    }
    
    if (b->target_data) {
        LLVMDisposeTargetData(b->target_data);
        b->target_data = NULL;
    }
    
    // Context must be disposed last as other objects depend on it
    if (b->context) {
        LLVMContextDispose(b->context);
        b->context = NULL;
    }
    
    // Reset all pointers and state
    b->fc_module = NULL;
    b->has_error = false;
    b->error_message[0] = '\0';
    b->instruction_count = b->function_count = b->block_count = 0;
    
    // Finally free the backend structure itself
    free(b);
}

void llvm_backend_reset(LLVMBackend* b) {
    if (!b) return;
    
    // Clean up current function context
    if (b->current_func_ctx) {
        // Free all function context resources with proper null checks
        free(b->current_func_ctx->vreg_values);
        free(b->current_func_ctx->vreg_allocas);
        free(b->current_func_ctx->vreg_is_mutable);
        free(b->current_func_ctx->vreg_types);
        free(b->current_func_ctx->label_blocks);
        // Note: current_func_ctx->blocks is never allocated per the cleanup comment in emit_function
        free(b->current_func_ctx);
        b->current_func_ctx = NULL;
    }
    
    // Free global strings
    free(b->global_strings);
    b->global_strings = NULL;
    b->global_string_count = 0;
    
    // Free external functions
    free(b->external_funcs);
    b->external_funcs = NULL;
    b->external_func_count = 0;
    
    // Dispose module if it exists
    if (b->module) {
        LLVMDisposeModule(b->module);
        b->module = NULL;
    }
    
    // Reset module reference
    b->fc_module = NULL;
    
    // Reset statistics
    b->instruction_count = 0;
    b->function_count = 0;
    b->block_count = 0;
    
    // Reset error state
    b->has_error = false;
    b->error_message[0] = '\0';
    
    // Note: Do NOT dispose/reset LLVM infrastructure that should persist:
    // - b->context (LLVM context)
    // - b->builder (LLVM builder)
    // - b->target_machine (target machine)
    // - b->target_data (target data layout)
    // - b->target (LLVM target)
    // - b->config and b->cpu_features (backend configuration)
}

const char* llvm_backend_get_error(const LLVMBackend* b) {
    return (b && b->has_error) ? b->error_message : NULL;
}

// Get LLVM integer type for a given byte size
static LLVMTypeRef llvm_int_type(LLVMBackend* b, uint8_t size) {
    switch (size) {
        case 1: return LLVMInt8TypeInContext(b->context);
        case 2: return LLVMInt16TypeInContext(b->context);
        case 4: return LLVMInt32TypeInContext(b->context);
        case 8: return LLVMInt64TypeInContext(b->context);
        case 16: return LLVMInt128TypeInContext(b->context);
        case 32: return LLVMIntTypeInContext(b->context, 256);  // i256
        case 64: return LLVMIntTypeInContext(b->context, 512);  // i512
        case 128: return LLVMIntTypeInContext(b->context, 1024); // i1024
        default: return LLVMInt64TypeInContext(b->context);
    }
}

// Get LLVM type for a VRegType
static LLVMTypeRef llvm_type_for_vreg(LLVMBackend* b, VRegType type) {
    switch (type) {
        case VREG_TYPE_I8:
        case VREG_TYPE_U8:
            return LLVMInt8TypeInContext(b->context);
        case VREG_TYPE_I16:
        case VREG_TYPE_U16:
            return LLVMInt16TypeInContext(b->context);
        case VREG_TYPE_I32:
        case VREG_TYPE_U32:
            return LLVMInt32TypeInContext(b->context);
        case VREG_TYPE_I64:
        case VREG_TYPE_U64:
            return LLVMInt64TypeInContext(b->context);
        case VREG_TYPE_I128:
        case VREG_TYPE_U128:
            return LLVMInt128TypeInContext(b->context);
        case VREG_TYPE_I256:
        case VREG_TYPE_U256:
            return LLVMIntTypeInContext(b->context, 256);
        case VREG_TYPE_I512:
        case VREG_TYPE_U512:
            return LLVMIntTypeInContext(b->context, 512);
        case VREG_TYPE_I1024:
        case VREG_TYPE_U1024:
            return LLVMIntTypeInContext(b->context, 1024);
        case VREG_TYPE_F32:
            return LLVMFloatTypeInContext(b->context);
        case VREG_TYPE_F64:
            return LLVMDoubleTypeInContext(b->context);
        case VREG_TYPE_PTR:
        case VREG_TYPE_RAWPTR:
        case VREG_TYPE_BYTEPTR:
            return LLVMPointerTypeInContext(b->context, 0);
        case VREG_TYPE_BOOL:
            return LLVMInt1TypeInContext(b->context);
        case VREG_TYPE_VOID:
        default:
            return LLVMVoidTypeInContext(b->context);
    }
}

// Get bit width for a VRegType
static unsigned llvm_bitwidth_for_vreg(VRegType type) {
    switch (type) {
        case VREG_TYPE_I8:
        case VREG_TYPE_U8:
            return 8;
        case VREG_TYPE_I16:
        case VREG_TYPE_U16:
            return 16;
        case VREG_TYPE_I32:
        case VREG_TYPE_U32:
            return 32;
        case VREG_TYPE_I64:
        case VREG_TYPE_U64:
            return 64;
        case VREG_TYPE_I128:
        case VREG_TYPE_U128:
            return 128;
        case VREG_TYPE_I256:
        case VREG_TYPE_U256:
            return 256;
        case VREG_TYPE_I512:
        case VREG_TYPE_U512:
            return 512;
        case VREG_TYPE_I1024:
        case VREG_TYPE_U1024:
            return 1024;
        case VREG_TYPE_BOOL:
            return 1;
        default:
            return 64;
    }
}

static LLVMTypeRef llvm_ptr_type(LLVMBackend* b) {
    return LLVMPointerType(LLVMInt8TypeInContext(b->context), 0);
}

static LLVMValueRef get_vreg(LLVMBackend* b, VirtualReg vreg) {
    LLVMFunctionContext* ctx = b->current_func_ctx;
    if (!ctx || vreg.id >= ctx->vreg_capacity) return NULL;
    
    // If this vreg uses alloca (mutable), load from memory
    if (ctx->vreg_is_mutable && ctx->vreg_is_mutable[vreg.id] && ctx->vreg_allocas[vreg.id]) {
        char name[32];
        snprintf(name, sizeof(name), "v%u.load", vreg.id);
        // Use the stored vreg type, or fall back to i64
        LLVMTypeRef load_type;
        if (ctx->vreg_types && vreg.id < ctx->vreg_capacity) {
            load_type = llvm_type_for_vreg(b, ctx->vreg_types[vreg.id]);
        } else {
            load_type = LLVMInt64TypeInContext(b->context);
        }
        return LLVMBuildLoad2(b->builder, load_type, ctx->vreg_allocas[vreg.id], name);
    }
    
    return ctx->vreg_values[vreg.id];
}

static void set_vreg(LLVMBackend* b, VirtualReg vreg, LLVMValueRef val) {
    LLVMFunctionContext* ctx = b->current_func_ctx;
    if (!ctx) return;
    
    if (vreg.id >= ctx->vreg_capacity) {
        uint32_t new_cap = vreg.id + 64;
        ctx->vreg_values = realloc(ctx->vreg_values, new_cap * sizeof(LLVMValueRef));
        memset(ctx->vreg_values + ctx->vreg_capacity, 0, (new_cap - ctx->vreg_capacity) * sizeof(LLVMValueRef));
        if (ctx->vreg_allocas) {
            ctx->vreg_allocas = realloc(ctx->vreg_allocas, new_cap * sizeof(LLVMValueRef));
            memset(ctx->vreg_allocas + ctx->vreg_capacity, 0, (new_cap - ctx->vreg_capacity) * sizeof(LLVMValueRef));
        }
        if (ctx->vreg_is_mutable) {
            ctx->vreg_is_mutable = realloc(ctx->vreg_is_mutable, new_cap * sizeof(bool));
            memset(ctx->vreg_is_mutable + ctx->vreg_capacity, 0, (new_cap - ctx->vreg_capacity) * sizeof(bool));
        }
        if (ctx->vreg_types) {
            ctx->vreg_types = realloc(ctx->vreg_types, new_cap * sizeof(VRegType));
            memset(ctx->vreg_types + ctx->vreg_capacity, 0, (new_cap - ctx->vreg_capacity) * sizeof(VRegType));
        }
        ctx->vreg_capacity = new_cap;
    }
    
    // Store the vreg type
    if (ctx->vreg_types) {
        ctx->vreg_types[vreg.id] = vreg.type;
    }
    
    // If this vreg uses alloca (mutable), store to memory
    if (ctx->vreg_is_mutable && ctx->vreg_is_mutable[vreg.id] && ctx->vreg_allocas[vreg.id]) {
        LLVMTypeRef val_type = LLVMTypeOf(val);
        LLVMTypeKind kind = LLVMGetTypeKind(val_type);
        
        // Skip storing void values
        if (kind != LLVMVoidTypeKind) {
            // Get the target type for this vreg
            LLVMTypeRef target_ty = llvm_type_for_vreg(b, vreg.type);
            unsigned target_bits = llvm_bitwidth_for_vreg(vreg.type);
            
            if (val_type != target_ty) {
                if (kind == LLVMIntegerTypeKind) {
                    unsigned bits = LLVMGetIntTypeWidth(val_type);
                    if (bits < target_bits) {
                        val = LLVMBuildZExt(b->builder, val, target_ty, "");
                    } else if (bits > target_bits) {
                        val = LLVMBuildTrunc(b->builder, val, target_ty, "");
                    }
                }
            }
            LLVMBuildStore(b->builder, val, ctx->vreg_allocas[vreg.id]);
        }
    }
    
    if (vreg.id >= ctx->vreg_count) ctx->vreg_count = vreg.id + 1;
    ctx->vreg_values[vreg.id] = val;
}

static LLVMBasicBlockRef get_label(LLVMBackend* b, uint32_t id) {
    LLVMFunctionContext* ctx = b->current_func_ctx;
    if (!ctx || id >= ctx->label_count) return NULL;
    return ctx->label_blocks[id];
}

static void ensure_label(LLVMBackend* b, uint32_t id) {
    LLVMFunctionContext* ctx = b->current_func_ctx;
    if (!ctx) return;
    if (id >= ctx->label_count) {
        uint32_t new_count = id + 32;
        ctx->label_blocks = realloc(ctx->label_blocks, new_count * sizeof(LLVMBasicBlockRef));
        memset(ctx->label_blocks + ctx->label_count, 0, (new_count - ctx->label_count) * sizeof(LLVMBasicBlockRef));
        ctx->label_count = new_count;
    }
    if (!ctx->label_blocks[id]) {
        char name[32];
        snprintf(name, sizeof(name), "L%u", id);
        ctx->label_blocks[id] = LLVMAppendBasicBlockInContext(b->context, ctx->function, name);
    }
}

static LLVMValueRef get_operand(LLVMBackend* b, const FcOperand* op) {
    switch (op->type) {
        case FC_OPERAND_VREG: {
            LLVMValueRef val = get_vreg(b, op->u.vreg);
            // If vreg not set, return a default zero value
            if (!val) {
                uint8_t sz = op->u.vreg.size > 0 ? op->u.vreg.size : 8;
                return LLVMConstInt(llvm_int_type(b, sz), 0, 0);
            }
            return val;
        }
        case FC_OPERAND_IMMEDIATE: {
            int64_t imm = op->u.immediate;
            if (imm < 0 && b->global_strings) {
                uint32_t sid = (uint32_t)(-imm);
                if (sid < b->global_string_count) return b->global_strings[sid];
            }
            return LLVMConstInt(LLVMInt64TypeInContext(b->context), imm, true);
        }
        case FC_OPERAND_BIGINT: {
            // Bigint constant - use LLVMConstIntOfArbitraryPrecision
            uint8_t num_limbs = op->u.bigint.num_limbs;
            unsigned num_bits = num_limbs * 64;
            LLVMTypeRef bigint_type = LLVMIntTypeInContext(b->context, num_bits);
            return LLVMConstIntOfArbitraryPrecision(bigint_type, num_limbs, op->u.bigint.limbs);
        }
        case FC_OPERAND_EXTERNAL_FUNC:
            if (op->u.external_func_id < b->external_func_count)
                return b->external_funcs[op->u.external_func_id];
            return NULL;
        case FC_OPERAND_LABEL:
            // Labels are handled specially in control flow instructions
            return NULL;
        case FC_OPERAND_MEMORY:
        case FC_OPERAND_STACK_SLOT:
            // Memory operands need special handling
            return NULL;
        default:
            return NULL;
    }
}

static LLVMValueRef cast_to(LLVMBackend* b, LLVMValueRef val, LLVMTypeRef target) {
    LLVMTypeRef src = LLVMTypeOf(val);
    if (src == target) return val;
    unsigned src_bits = LLVMGetIntTypeWidth(src);
    unsigned dst_bits = LLVMGetIntTypeWidth(target);
    if (src_bits < dst_bits) return LLVMBuildZExt(b->builder, val, target, "");
    if (src_bits > dst_bits) return LLVMBuildTrunc(b->builder, val, target, "");
    return val;
}

static bool emit_mov(LLVMBackend* b, const FcIRInstruction* i) {
    const FcOperand* dst = &i->operands[0];
    const FcOperand* src = &i->operands[1];
    
    // Check for comparison result pattern: MOV dest, -(condition_code + 1000)
    // This is generated by fc_ir_lower_comparison
    if (src->type == FC_OPERAND_IMMEDIATE && src->u.immediate < -1000) {
        // This is a comparison result - extract the condition code
        int64_t condition_code = -(src->u.immediate + 1000);
        
        // Check if we have valid comparison operands
        if (!b->current_func_ctx->last_cmp_lhs || !b->current_func_ctx->last_cmp_rhs) {
            // No previous comparison - just store 0
            LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
            if (dst->type == FC_OPERAND_VREG) {
                set_vreg(b, dst->u.vreg, LLVMConstInt(i64, 0, false));
            }
            b->instruction_count++;
            return true;
        }
        
        // Map condition code to LLVM predicate
        LLVMIntPredicate pred;
        switch ((FcIROpcode)condition_code) {
            case FCIR_JE: pred = LLVMIntEQ; break;
            case FCIR_JNE: pred = LLVMIntNE; break;
            case FCIR_JL: pred = LLVMIntSLT; break;
            case FCIR_JLE: pred = LLVMIntSLE; break;
            case FCIR_JG: pred = LLVMIntSGT; break;
            case FCIR_JGE: pred = LLVMIntSGE; break;
            case FCIR_JA: pred = LLVMIntUGT; break;
            case FCIR_JB: pred = LLVMIntULT; break;
            case FCIR_JAE: pred = LLVMIntUGE; break;
            case FCIR_JBE: pred = LLVMIntULE; break;
            default: pred = LLVMIntEQ; break;
        }
        
        // Use the last comparison operands
        LLVMValueRef cmp_result = LLVMBuildICmp(b->builder, pred,
            b->current_func_ctx->last_cmp_lhs, b->current_func_ctx->last_cmp_rhs, "");
        
        // Zero-extend the i1 result to i64
        LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
        LLVMValueRef result = LLVMBuildZExt(b->builder, cmp_result, i64, "");
        
        if (dst->type == FC_OPERAND_VREG) {
            set_vreg(b, dst->u.vreg, result);
            // Track this vreg as holding a comparison result
            b->current_func_ctx->last_cmp_result_vreg = dst->u.vreg.id;
        }
        b->instruction_count++;
        return true;
    }
    
    // Handle memory source operand (load)
    if (src->type == FC_OPERAND_MEMORY) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(b->context);
        LLVMTypeRef ptr_ty = llvm_ptr_type(b);
        
        // Get base address as pointer
        LLVMValueRef base = get_vreg(b, src->u.memory.base);
        if (!base) {
            base = LLVMConstInt(i64, 0, 0);
        }
        LLVMValueRef base_ptr = LLVMBuildIntToPtr(b->builder, base, ptr_ty, "");
        
        // Calculate total byte offset
        int64_t total_offset = src->u.memory.displacement;
        LLVMValueRef offset_val = LLVMConstInt(i64, total_offset, true);
        
        // Add scaled index if present
        if (src->u.memory.index.id != 0) {
            LLVMValueRef index = get_vreg(b, src->u.memory.index);
            if (index) {
                LLVMValueRef scale = LLVMConstInt(i64, src->u.memory.scale, 0);
                LLVMValueRef scaled = LLVMBuildMul(b->builder, index, scale, "");
                offset_val = LLVMBuildAdd(b->builder, offset_val, scaled, "");
            }
        }
        
        // Use GEP for pointer arithmetic (treating as i8* for byte-level addressing)
        LLVMValueRef indices[] = { offset_val };
        LLVMValueRef ptr = LLVMBuildGEP2(b->builder, i8, base_ptr, indices, 1, "");
        LLVMValueRef loaded = LLVMBuildLoad2(b->builder, i64, ptr, "");
        
        if (dst->type == FC_OPERAND_VREG) {
            set_vreg(b, dst->u.vreg, loaded);
        }
        b->instruction_count++;
        return true;
    }
    
    // Handle memory destination operand (store)
    if (dst->type == FC_OPERAND_MEMORY) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(b->context);
        LLVMTypeRef ptr_ty = llvm_ptr_type(b);
        
        // Get value to store
        LLVMValueRef src_val = get_operand(b, src);
        if (!src_val) { set_error(b, "MOV store: null source"); return false; }
        
        // Get base address as pointer
        LLVMValueRef base = get_vreg(b, dst->u.memory.base);
        if (!base) {
            base = LLVMConstInt(i64, 0, 0);
        }
        LLVMValueRef base_ptr = LLVMBuildIntToPtr(b->builder, base, ptr_ty, "");
        
        // Calculate total byte offset
        int64_t total_offset = dst->u.memory.displacement;
        LLVMValueRef offset_val = LLVMConstInt(i64, total_offset, true);
        
        // Add scaled index if present
        if (dst->u.memory.index.id != 0) {
            LLVMValueRef index = get_vreg(b, dst->u.memory.index);
            if (index) {
                LLVMValueRef scale = LLVMConstInt(i64, dst->u.memory.scale, 0);
                LLVMValueRef scaled = LLVMBuildMul(b->builder, index, scale, "");
                offset_val = LLVMBuildAdd(b->builder, offset_val, scaled, "");
            }
        }
        
        // Use GEP for pointer arithmetic (treating as i8* for byte-level addressing)
        LLVMValueRef indices[] = { offset_val };
        LLVMValueRef ptr = LLVMBuildGEP2(b->builder, i8, base_ptr, indices, 1, "");
        LLVMBuildStore(b->builder, src_val, ptr);
        
        b->instruction_count++;
        return true;
    }
    
    LLVMValueRef src_val = get_operand(b, src);
    if (!src_val) { set_error(b, "MOV: null source"); return false; }
    
    if (dst->type == FC_OPERAND_VREG) {
        uint8_t sz = dst->u.vreg.size > 0 ? dst->u.vreg.size : 8;
        src_val = cast_to(b, src_val, llvm_int_type(b, sz));
        set_vreg(b, dst->u.vreg, src_val);
    }
    b->instruction_count++;
    return true;
}

static bool emit_binary(LLVMBackend* b, const FcIRInstruction* i) {
    const FcOperand* dst = &i->operands[0];
    const FcOperand* src = &i->operands[1];
    
    LLVMValueRef lhs = get_operand(b, dst);
    LLVMValueRef rhs = get_operand(b, src);
    if (!lhs || !rhs) { set_error(b, "Binary: null operand"); return false; }
    
    LLVMTypeRef ty = LLVMTypeOf(lhs);
    rhs = cast_to(b, rhs, ty);
    
    LLVMValueRef res = NULL;
    switch (i->opcode) {
        case FCIR_ADD: res = LLVMBuildAdd(b->builder, lhs, rhs, ""); break;
        case FCIR_SUB: res = LLVMBuildSub(b->builder, lhs, rhs, ""); break;
        case FCIR_IMUL: res = LLVMBuildMul(b->builder, lhs, rhs, ""); break;
        case FCIR_AND: res = LLVMBuildAnd(b->builder, lhs, rhs, ""); break;
        case FCIR_OR: res = LLVMBuildOr(b->builder, lhs, rhs, ""); break;
        case FCIR_XOR: res = LLVMBuildXor(b->builder, lhs, rhs, ""); break;
        default: return false;
    }
    if (dst->type == FC_OPERAND_VREG) set_vreg(b, dst->u.vreg, res);
    b->instruction_count++;
    return true;
}

static bool emit_div(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef lhs = get_operand(b, &i->operands[0]);
    LLVMValueRef rhs = get_operand(b, &i->operands[1]);
    if (!lhs || !rhs) return false;
    rhs = cast_to(b, rhs, LLVMTypeOf(lhs));
    LLVMValueRef res = LLVMBuildSDiv(b->builder, lhs, rhs, "");
    if (i->operands[0].type == FC_OPERAND_VREG) set_vreg(b, i->operands[0].u.vreg, res);
    b->instruction_count++;
    return true;
}

static bool emit_unary(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef val = get_operand(b, &i->operands[0]);
    if (!val) return false;
    LLVMTypeRef ty = LLVMTypeOf(val);
    LLVMValueRef res = NULL;
    switch (i->opcode) {
        case FCIR_NEG: res = LLVMBuildNeg(b->builder, val, ""); break;
        case FCIR_NOT: res = LLVMBuildNot(b->builder, val, ""); break;
        case FCIR_INC: res = LLVMBuildAdd(b->builder, val, LLVMConstInt(ty, 1, 0), ""); break;
        case FCIR_DEC: res = LLVMBuildSub(b->builder, val, LLVMConstInt(ty, 1, 0), ""); break;
        default: return false;
    }
    if (i->operands[0].type == FC_OPERAND_VREG) set_vreg(b, i->operands[0].u.vreg, res);
    b->instruction_count++;
    return true;
}

static bool emit_shift(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef val = get_operand(b, &i->operands[0]);
    LLVMValueRef amt = get_operand(b, &i->operands[1]);
    if (!val || !amt) return false;
    LLVMTypeRef ty = LLVMTypeOf(val);
    amt = cast_to(b, amt, ty);
    LLVMValueRef res = NULL;
    switch (i->opcode) {
        case FCIR_SHL: res = LLVMBuildShl(b->builder, val, amt, ""); break;
        case FCIR_SHR: res = LLVMBuildLShr(b->builder, val, amt, ""); break;
        case FCIR_SAR: res = LLVMBuildAShr(b->builder, val, amt, ""); break;
        case FCIR_ROL: case FCIR_ROR: {
            unsigned bits = LLVMGetIntTypeWidth(ty);
            LLVMValueRef bw = LLVMConstInt(ty, bits, 0);
            LLVMValueRef mask = LLVMConstInt(ty, bits - 1, 0);
            LLVMValueRef m = LLVMBuildAnd(b->builder, amt, mask, "");
            LLVMValueRef inv = LLVMBuildSub(b->builder, bw, m, "");
            if (i->opcode == FCIR_ROL) {
                LLVMValueRef l = LLVMBuildShl(b->builder, val, m, "");
                LLVMValueRef r = LLVMBuildLShr(b->builder, val, inv, "");
                res = LLVMBuildOr(b->builder, l, r, "");
            } else {
                LLVMValueRef r = LLVMBuildLShr(b->builder, val, m, "");
                LLVMValueRef l = LLVMBuildShl(b->builder, val, inv, "");
                res = LLVMBuildOr(b->builder, r, l, "");
            }
            break;
        }
        default: return false;
    }
    if (i->operands[0].type == FC_OPERAND_VREG) set_vreg(b, i->operands[0].u.vreg, res);
    b->instruction_count++;
    return true;
}

static bool emit_cmp(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef lhs = get_operand(b, &i->operands[0]);
    LLVMValueRef rhs = get_operand(b, &i->operands[1]);
    if (!lhs || !rhs) return false;
    
    // Check if this is a comparison of a comparison result with 0
    // This pattern is generated by fc_ir_lower_branch for boolean conditions
    if (i->operands[1].type == FC_OPERAND_IMMEDIATE && i->operands[1].u.immediate == 0) {
        // Check if lhs is the tracked comparison result vreg
        if (i->operands[0].type == FC_OPERAND_VREG &&
            i->operands[0].u.vreg.id == b->current_func_ctx->last_cmp_result_vreg) {
            // This is comparing a comparison result with 0
            // The lhs value is a zext of an i1, truncate it back
            LLVMValueRef bool_val = LLVMBuildTrunc(b->builder, lhs, 
                LLVMInt1TypeInContext(b->context), "");
            b->current_func_ctx->last_cmp_lhs = bool_val;
            b->current_func_ctx->last_cmp_rhs = LLVMConstInt(LLVMInt1TypeInContext(b->context), 0, 0);
            b->current_func_ctx->last_cmp_is_bool = true;
            b->instruction_count++;
            return true;
        }
        
        // Check if lhs is i1 type directly
        LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
        if (LLVMGetTypeKind(lhs_type) == LLVMIntegerTypeKind && 
            LLVMGetIntTypeWidth(lhs_type) == 1) {
            b->current_func_ctx->last_cmp_lhs = lhs;
            b->current_func_ctx->last_cmp_rhs = LLVMConstInt(LLVMInt1TypeInContext(b->context), 0, 0);
            b->current_func_ctx->last_cmp_is_bool = true;
            b->instruction_count++;
            return true;
        }
    }
    
    rhs = cast_to(b, rhs, LLVMTypeOf(lhs));
    b->current_func_ctx->last_cmp_lhs = lhs;
    b->current_func_ctx->last_cmp_rhs = rhs;
    b->current_func_ctx->last_cmp_is_bool = false;
    b->instruction_count++;
    return true;
}

static bool emit_jmp(LLVMBackend* b, const FcIRInstruction* i) {
    uint32_t label = i->operands[0].u.label_id;
    ensure_label(b, label);
    LLVMBuildBr(b->builder, get_label(b, label));
    b->instruction_count++;
    return true;
}

static bool emit_jcc(LLVMBackend* b, const FcIRInstruction* i) {
    uint32_t label = i->operands[0].u.label_id;
    ensure_label(b, label);
    
    LLVMValueRef cond;
    
    // Check if the last comparison was a boolean comparison (cmp bool, 0)
    if (b->current_func_ctx->last_cmp_is_bool) {
        // For boolean comparisons, use the boolean value directly
        // JNE means "jump if not zero" which is "jump if true"
        if (i->opcode == FCIR_JNE) {
            cond = b->current_func_ctx->last_cmp_lhs;
        } else if (i->opcode == FCIR_JE) {
            // JE means "jump if zero" which is "jump if false"
            cond = LLVMBuildNot(b->builder, b->current_func_ctx->last_cmp_lhs, "");
        } else {
            // For other conditions, fall back to regular comparison
            LLVMIntPredicate pred;
            switch (i->opcode) {
                case FCIR_JL: pred = LLVMIntSLT; break;
                case FCIR_JLE: pred = LLVMIntSLE; break;
                case FCIR_JG: pred = LLVMIntSGT; break;
                case FCIR_JGE: pred = LLVMIntSGE; break;
                default: pred = LLVMIntNE; break;
            }
            cond = LLVMBuildICmp(b->builder, pred,
                b->current_func_ctx->last_cmp_lhs, b->current_func_ctx->last_cmp_rhs, "");
        }
    } else {
        // Regular comparison
        LLVMIntPredicate pred;
        switch (i->opcode) {
            case FCIR_JE: pred = LLVMIntEQ; break;
            case FCIR_JNE: pred = LLVMIntNE; break;
            case FCIR_JL: pred = LLVMIntSLT; break;
            case FCIR_JLE: pred = LLVMIntSLE; break;
            case FCIR_JG: pred = LLVMIntSGT; break;
            case FCIR_JGE: pred = LLVMIntSGE; break;
            case FCIR_JA: pred = LLVMIntUGT; break;
            case FCIR_JB: pred = LLVMIntULT; break;
            case FCIR_JAE: pred = LLVMIntUGE; break;
            case FCIR_JBE: pred = LLVMIntULE; break;
            default: return false;
        }
        
        cond = LLVMBuildICmp(b->builder, pred,
            b->current_func_ctx->last_cmp_lhs, b->current_func_ctx->last_cmp_rhs, "");
    }
    
    LLVMBasicBlockRef fall = LLVMAppendBasicBlockInContext(b->context, b->current_func_ctx->function, "");
    LLVMBuildCondBr(b->builder, cond, get_label(b, label), fall);
    LLVMPositionBuilderAtEnd(b->builder, fall);
    b->current_func_ctx->current_block = fall;
    b->instruction_count++;
    return true;
}

static bool emit_call(LLVMBackend* b, const FcIRInstruction* i) {
    const FcOperand* op = &i->operands[0];
    LLVMValueRef fn = NULL;
    const char* fn_name = NULL;
    
    // Handle different operand types for call target
    if (op->type == FC_OPERAND_EXTERNAL_FUNC) {
        // External function - look up by index
        if (op->u.external_func_id < b->external_func_count) {
            fn = b->external_funcs[op->u.external_func_id];
            // Get function name for special handling
            if (b->fc_module && op->u.external_func_id < b->fc_module->external_func_count) {
                fn_name = b->fc_module->external_functions[op->u.external_func_id];
            }
        }
    } else if (op->type == FC_OPERAND_LABEL) {
        // Internal function call - the label ID maps to a function
        // Search through all functions in the FC module to find matching label
        uint32_t target_label = op->u.label_id;
        
        // First, try to find function by iterating through module functions
        // The label ID should correspond to a function's entry block label
        for (uint32_t fi = 0; fi < b->fc_module->function_count; fi++) {
            const FcIRFunction* fc_fn = &b->fc_module->functions[fi];
            // Check if this function's first block has the target label
            if (fc_fn->block_count > 0 && fc_fn->blocks[0].id == target_label) {
                fn = LLVMGetNamedFunction(b->module, fc_fn->name);
                break;
            }
            // Also check if the function name hash matches the label
            // (some call instructions use function name hash as label)
            uint32_t name_hash = 0;
            for (const char* p = fc_fn->name; *p; p++) {
                name_hash = name_hash * 31 + (uint8_t)*p;
            }
            if (name_hash == target_label) {
                fn = LLVMGetNamedFunction(b->module, fc_fn->name);
                break;
            }
        }
        
        // If not found by label, try direct function name lookup
        if (!fn) {
            char name[64];
            snprintf(name, sizeof(name), ".L%u", target_label);
            fn = LLVMGetNamedFunction(b->module, name);
        }
    } else if (op->type == FC_OPERAND_VREG) {
        // Indirect call through register
        fn = get_vreg(b, op->u.vreg);
    }
    
    if (!fn) {
        // Function not found - this is an error but we'll return 0 for now
        LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
        set_vreg(b, (VirtualReg){.id = 1000, .size = 8}, LLVMConstInt(i64, 0, 0));
        b->instruction_count++;
        return true;
    }
    
    // Get function type and build call
    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    unsigned param_count = LLVMCountParamTypes(fn_ty);
    
    // Check if this is a bigint print function that needs pointer passing
    bool is_bigint_print = fn_name && (
        strcmp(fn_name, "_fcx_println_i256") == 0 ||
        strcmp(fn_name, "_fcx_println_u256") == 0 ||
        strcmp(fn_name, "_fcx_println_i512") == 0 ||
        strcmp(fn_name, "_fcx_println_u512") == 0 ||
        strcmp(fn_name, "_fcx_println_i1024") == 0 ||
        strcmp(fn_name, "_fcx_println_u1024") == 0
    );
    
    // Check if this is an i128 print function (passed by value, not pointer)
    bool is_i128_print = fn_name && (
        strcmp(fn_name, "_fcx_println_i128") == 0 ||
        strcmp(fn_name, "_fcx_println_u128") == 0
    );
    
    // Collect arguments from calling convention registers (System V AMD64)
    // Arguments are in v1001 (rdi), v1002 (rsi), v1003 (rdx), v1007 (rcx), v1005 (r8), v1006 (r9)
    uint32_t arg_vreg_ids[] = {1001, 1002, 1003, 1007, 1005, 1006};
    LLVMValueRef* args = NULL;
    
    if (param_count > 0) {
        args = malloc(param_count * sizeof(LLVMValueRef));
        LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
        
        if (is_bigint_print && param_count == 1) {
            // For bigint print functions, we need to pass a pointer to the value
            // Determine the bigint size based on function name
            unsigned bigint_bits = 256;
            uint8_t bigint_size = 32;
            if (fn_name) {
                if (strstr(fn_name, "512")) { bigint_bits = 512; bigint_size = 64; }
                else if (strstr(fn_name, "1024")) { bigint_bits = 1024; bigint_size = 128; }
            }
            
            // Get the argument with the correct size
            LLVMValueRef arg = get_vreg(b, (VirtualReg){.id = arg_vreg_ids[0], .size = bigint_size, .type = VREG_TYPE_I256});
            LLVMTypeRef bigint_type = LLVMIntTypeInContext(b->context, bigint_bits);
            
            if (arg) {
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeKind kind = LLVMGetTypeKind(arg_type);
                
                // If the argument is an integer, we need to store it to stack and pass pointer
                if (kind == LLVMIntegerTypeKind) {
                    unsigned arg_bits = LLVMGetIntTypeWidth(arg_type);
                    
                    // Extend or use the value as-is
                    LLVMValueRef val_to_store = arg;
                    if (arg_bits < bigint_bits) {
                        val_to_store = LLVMBuildZExt(b->builder, arg, bigint_type, "");
                    } else if (arg_bits > bigint_bits) {
                        val_to_store = LLVMBuildTrunc(b->builder, arg, bigint_type, "");
                    }
                    
                    // Allocate stack space for the bigint
                    LLVMValueRef alloca = LLVMBuildAlloca(b->builder, bigint_type, "bigint_tmp");
                    LLVMSetAlignment(alloca, 16);
                    
                    // Store the value
                    LLVMBuildStore(b->builder, val_to_store, alloca);
                    
                    // Pass the pointer
                    args[0] = alloca;
                } else if (kind == LLVMPointerTypeKind) {
                    // Already a pointer, use it directly
                    args[0] = arg;
                } else {
                    // Fallback: create a zero value
                    LLVMValueRef alloca = LLVMBuildAlloca(b->builder, bigint_type, "bigint_tmp");
                    LLVMBuildStore(b->builder, LLVMConstInt(bigint_type, 0, 0), alloca);
                    args[0] = alloca;
                }
            } else {
                // No argument found, pass null pointer
                args[0] = LLVMConstNull(llvm_ptr_type(b));
            }
        } else if (is_i128_print && param_count == 1) {
            // For i128 print functions, we need to pass the value directly (not as pointer)
            // Get the argument with the correct size (16 bytes for i128)
            LLVMValueRef arg = get_vreg(b, (VirtualReg){.id = arg_vreg_ids[0], .size = 16, .type = VREG_TYPE_I128});
            LLVMTypeRef i128_type = LLVMInt128TypeInContext(b->context);
            
            if (arg) {
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                unsigned arg_bits = LLVMGetIntTypeWidth(arg_type);
                
                // Extend to i128 if needed
                if (arg_bits < 128) {
                    arg = LLVMBuildZExt(b->builder, arg, i128_type, "");
                } else if (arg_bits > 128) {
                    arg = LLVMBuildTrunc(b->builder, arg, i128_type, "");
                }
                args[0] = arg;
            } else {
                // No argument found, pass zero
                args[0] = LLVMConstInt(i128_type, 0, 0);
            }
        } else {
            // Normal argument handling
            for (unsigned j = 0; j < param_count && j < 6; j++) {
                LLVMValueRef arg = get_vreg(b, (VirtualReg){.id = arg_vreg_ids[j], .size = 8});
                args[j] = arg ? arg : LLVMConstInt(i64, 0, 0);
            }
        }
    }
    
    LLVMValueRef ret = LLVMBuildCall2(b->builder, fn_ty, fn, args, param_count, "");
    free(args);
    
    // Store return value in v1000 (rax - FCx convention for return values)
    // Only store if the function returns a non-void type
    LLVMTypeRef ret_ty = LLVMGetReturnType(fn_ty);
    if (LLVMGetTypeKind(ret_ty) != LLVMVoidTypeKind) {
        set_vreg(b, (VirtualReg){.id = 1000, .size = 8}, ret);
    }
    b->instruction_count++;
    return true;
}


static bool emit_ret(LLVMBackend* b, const FcIRInstruction* i) {
    (void)i;
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(b->current_func_ctx->function));
    if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(b->builder);
    } else {
        // FC IR convention: return value is in v1000
        LLVMValueRef ret_val = get_vreg(b, (VirtualReg){.id = 1000, .size = 8});
        if (ret_val) {
            LLVMBuildRet(b->builder, cast_to(b, ret_val, ret_ty));
        } else {
            LLVMBuildRet(b->builder, LLVMConstInt(ret_ty, 0, 0));
        }
    }
    b->instruction_count++;
    return true;
}

static bool emit_syscall(LLVMBackend* b, const FcIRInstruction* i) {
    (void)i;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
    LLVMTypeRef params[] = {i64, i64, i64, i64, i64, i64, i64};
    LLVMTypeRef fn_ty = LLVMFunctionType(i64, params, 7, false);
    
    // System V AMD64 syscall convention:
    // rax = syscall number (v1000)
    // rdi = arg1 (v1001)
    // rsi = arg2 (v1002)
    // rdx = arg3 (v1003)
    // r10 = arg4 (v1004)
    // r8  = arg5 (v1005)
    // r9  = arg6 (v1006)
    uint32_t syscall_vreg_ids[] = {1000, 1001, 1002, 1003, 1004, 1005, 1006};
    
    LLVMValueRef args[7];
    for (int j = 0; j < 7; j++) {
        LLVMValueRef v = get_vreg(b, (VirtualReg){.id = syscall_vreg_ids[j], .size = 8});
        args[j] = v ? v : LLVMConstInt(i64, 0, 0);
    }
    
    const char* asm_str = "syscall";
    const char* cons = "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8},{r9},~{rcx},~{r11},~{memory}";
    LLVMValueRef ia = LLVMGetInlineAsm(fn_ty, (char*)asm_str, strlen(asm_str),
        (char*)cons, strlen(cons), true, false, LLVMInlineAsmDialectATT, false);
    LLVMValueRef res = LLVMBuildCall2(b->builder, fn_ty, ia, args, 7, "");
    
    // Store result in v1000 (rax)
    set_vreg(b, (VirtualReg){.id = 1000, .size = 8}, res);
    b->instruction_count++;
    return true;
}

// Include fcx_ir.h for inline_asm struct definition
#include "../ir/fcx_ir.h"

// ============================================================================
// Inline Assembly Clobber Detection
// ============================================================================

// x86_64 register names for clobber detection
typedef struct {
    const char* name;       // Register name (without %)
    const char* llvm_name;  // LLVM clobber name
    uint8_t size;           // Register size in bytes (1, 2, 4, 8)
    uint8_t family;         // Register family (0=rax, 1=rbx, etc.)
} RegInfo;

static const RegInfo x86_64_regs[] = {
    // 64-bit
    {"rax", "rax", 8, 0}, {"rbx", "rbx", 8, 1}, {"rcx", "rcx", 8, 2}, {"rdx", "rdx", 8, 3},
    {"rsi", "rsi", 8, 4}, {"rdi", "rdi", 8, 5}, {"rbp", "rbp", 8, 6}, {"rsp", "rsp", 8, 7},
    {"r8", "r8", 8, 8}, {"r9", "r9", 8, 9}, {"r10", "r10", 8, 10}, {"r11", "r11", 8, 11},
    {"r12", "r12", 8, 12}, {"r13", "r13", 8, 13}, {"r14", "r14", 8, 14}, {"r15", "r15", 8, 15},
    // 32-bit (same family as 64-bit)
    {"eax", "rax", 4, 0}, {"ebx", "rbx", 4, 1}, {"ecx", "rcx", 4, 2}, {"edx", "rdx", 4, 3},
    {"esi", "rsi", 4, 4}, {"edi", "rdi", 4, 5}, {"ebp", "rbp", 4, 6}, {"esp", "rsp", 4, 7},
    {"r8d", "r8", 4, 8}, {"r9d", "r9", 4, 9}, {"r10d", "r10", 4, 10}, {"r11d", "r11", 4, 11},
    {"r12d", "r12", 4, 12}, {"r13d", "r13", 4, 13}, {"r14d", "r14", 4, 14}, {"r15d", "r15", 4, 15},
    // 16-bit
    {"ax", "rax", 2, 0}, {"bx", "rbx", 2, 1}, {"cx", "rcx", 2, 2}, {"dx", "rdx", 2, 3},
    {"si", "rsi", 2, 4}, {"di", "rdi", 2, 5}, {"bp", "rbp", 2, 6}, {"sp", "rsp", 2, 7},
    // 8-bit low
    {"al", "rax", 1, 0}, {"bl", "rbx", 1, 1}, {"cl", "rcx", 1, 2}, {"dl", "rdx", 1, 3},
    {"sil", "rsi", 1, 4}, {"dil", "rdi", 1, 5}, {"bpl", "rbp", 1, 6}, {"spl", "rsp", 1, 7},
    // 8-bit high
    {"ah", "rax", 1, 0}, {"bh", "rbx", 1, 1}, {"ch", "rcx", 1, 2}, {"dh", "rdx", 1, 3},
    {NULL, NULL, 0, 0}
};

// Check if a character can be part of a register name
static bool is_reg_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Find register info by name (case-insensitive)
static const RegInfo* find_register(const char* name, size_t len) {
    for (const RegInfo* r = x86_64_regs; r->name; r++) {
        if (strlen(r->name) == len) {
            bool match = true;
            for (size_t i = 0; i < len; i++) {
                char c1 = name[i];
                char c2 = r->name[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c1 != c2) { match = false; break; }
            }
            if (match) return r;
        }
    }
    return NULL;
}

// Detect clobbered registers by scanning the asm template
// Returns a bitmask of clobbered register families (bit N = family N is clobbered)
// Also sets has_syscall if syscall instruction is found
static uint32_t detect_asm_clobbers(const char* asm_template, bool* has_syscall, bool* has_memory_write) {
    if (!asm_template) return 0;
    
    uint32_t clobber_mask = 0;
    *has_syscall = false;
    *has_memory_write = false;
    
    const char* p = asm_template;
    while (*p) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (!*p) break;
        
        // Check for syscall instruction
        if (strncmp(p, "syscall", 7) == 0 && !is_reg_char(p[7])) {
            *has_syscall = true;
            // syscall clobbers rcx and r11
            clobber_mask |= (1 << 2) | (1 << 11);  // rcx, r11
            p += 7;
            continue;
        }
        
        // Look for instructions that write to registers
        // In AT&T syntax, destination is the LAST operand
        // Common patterns:
        //   movq src, %dst
        //   addq src, %dst
        //   xorq %reg, %reg
        //   pushq %reg (doesn't clobber reg, but modifies rsp)
        //   popq %reg (clobbers reg)
        
        // Find instruction mnemonic
        const char* instr_start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != ',') p++;
        size_t instr_len = p - instr_start;
        
        // Check for memory-writing instructions
        if ((instr_len >= 3 && strncmp(instr_start, "mov", 3) == 0) ||
            (instr_len >= 3 && strncmp(instr_start, "sto", 3) == 0)) {
            // Could be writing to memory, check later
        }
        
        // Skip to find operands
        while (*p && *p != '\n') {
            // Look for %register patterns
            if (*p == '%') {
                p++;
                const char* reg_start = p;
                while (*p && is_reg_char(*p)) p++;
                size_t reg_len = p - reg_start;
                
                if (reg_len > 0) {
                    const RegInfo* reg = find_register(reg_start, reg_len);
                    if (reg) {
                        // Check if this is a destination operand
                        // In AT&T syntax, skip whitespace and check what follows
                        const char* after = p;
                        while (*after == ' ' || *after == '\t') after++;
                        
                        // If followed by newline, end of line, or nothing more on this line,
                        // this is likely the destination
                        if (*after == '\n' || *after == '\0' || *after == '#') {
                            clobber_mask |= (1 << reg->family);
                        }
                        // If followed by comma, there's another operand (this is source)
                        // If followed by ')', this is inside a memory operand
                        else if (*after == ')') {
                            // Memory operand like (%rdi) - check if it's a store destination
                            // Look back to see if this is the last operand
                            const char* check = after + 1;
                            while (*check == ' ' || *check == '\t') check++;
                            if (*check == '\n' || *check == '\0' || *check == '#') {
                                *has_memory_write = true;
                            }
                        }
                    }
                }
                continue;
            }
            
            // Check for memory operand as destination: something like (%reg) at end of line
            if (*p == '(') {
                int paren_depth = 1;
                p++;
                while (*p && paren_depth > 0) {
                    if (*p == '(') paren_depth++;
                    else if (*p == ')') paren_depth--;
                    p++;
                }
                // Check if this memory operand is at end of line (destination)
                const char* after = p;
                while (*after == ' ' || *after == '\t') after++;
                if (*after == '\n' || *after == '\0' || *after == '#') {
                    *has_memory_write = true;
                }
                continue;
            }
            
            p++;
        }
        
        if (*p == '\n') p++;
    }
    
    return clobber_mask;
}

// Build clobber string from detected clobbers
// Returns newly allocated string (caller must free)
static char* build_clobber_string(uint32_t clobber_mask, bool has_syscall, bool has_memory_write,
                                   const char** existing_clobbers, uint8_t existing_count) {
    // Map family index to LLVM register name
    static const char* family_names[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    
    char buffer[512] = {0};
    size_t pos = 0;
    
    // Add existing clobbers first
    for (uint8_t i = 0; i < existing_count; i++) {
        if (pos > 0) buffer[pos++] = ',';
        buffer[pos++] = '~';
        buffer[pos++] = '{';
        const char* c = existing_clobbers[i];
        while (*c && pos < sizeof(buffer) - 10) buffer[pos++] = *c++;
        buffer[pos++] = '}';
    }
    
    // Add detected clobbers (skip if already in existing)
    for (int i = 0; i < 16; i++) {
        if (clobber_mask & (1 << i)) {
            // Check if already in existing clobbers
            bool already_exists = false;
            for (uint8_t j = 0; j < existing_count; j++) {
                if (strcmp(existing_clobbers[j], family_names[i]) == 0) {
                    already_exists = true;
                    break;
                }
            }
            if (already_exists) continue;
            
            // Skip rsp - we don't want to clobber stack pointer
            if (i == 7) continue;
            
            if (pos > 0) buffer[pos++] = ',';
            buffer[pos++] = '~';
            buffer[pos++] = '{';
            const char* name = family_names[i];
            while (*name && pos < sizeof(buffer) - 10) buffer[pos++] = *name++;
            buffer[pos++] = '}';
        }
    }
    
    // Add memory clobber if we write to memory
    if (has_memory_write) {
        bool has_memory = false;
        for (uint8_t i = 0; i < existing_count; i++) {
            if (strcmp(existing_clobbers[i], "memory") == 0) {
                has_memory = true;
                break;
            }
        }
        if (!has_memory) {
            if (pos > 0) buffer[pos++] = ',';
            const char* mem = "~{memory}";
            while (*mem && pos < sizeof(buffer) - 5) buffer[pos++] = *mem++;
        }
    }
    
    // Add cc (condition codes) clobber for most instructions
    if (clobber_mask != 0 || has_syscall) {
        bool has_cc = false;
        for (uint8_t i = 0; i < existing_count; i++) {
            if (strcmp(existing_clobbers[i], "cc") == 0) {
                has_cc = true;
                break;
            }
        }
        if (!has_cc) {
            if (pos > 0) buffer[pos++] = ',';
            const char* cc = "~{cc}";
            while (*cc && pos < sizeof(buffer) - 5) buffer[pos++] = *cc++;
        }
    }
    
    buffer[pos] = '\0';
    return pos > 0 ? strdup(buffer) : NULL;
}

// ============================================================================
// Inline Assembly Template Processing
// ============================================================================

// Preprocess inline asm template for LLVM:
// - Escape $ followed by digits to $$ (AT&T immediate syntax)
// - BUT keep $0, $1, ... $N as-is if they are operand references (N < num_operands)
// - Keep existing $$ as-is
// Returns newly allocated string (caller must free)
static char* preprocess_asm_template(const char* input, int num_operands) {
    if (!input) return NULL;
    
    size_t input_len = strlen(input);
    // Worst case: every char is $ followed by digit, so 2x expansion
    char* output = malloc(input_len * 2 + 1);
    if (!output) return NULL;
    
    const char* src = input;
    char* dst = output;
    
    while (*src) {
        if (*src == '$') {
            // Check if already escaped ($$)
            if (src[1] == '$') {
                // Already escaped, copy both
                *dst++ = *src++;
                *dst++ = *src++;
            }
            // Check if this is an operand reference ($0, $1, etc.)
            else if (src[1] >= '0' && src[1] <= '9') {
                // Parse the number to see if it's an operand reference
                const char* num_start = src + 1;
                const char* num_end = num_start;
                while (*num_end >= '0' && *num_end <= '9') num_end++;
                
                // Check if it's a small number (operand reference)
                int num_len = (int)(num_end - num_start);
                int operand_num = 0;
                bool is_operand_ref = false;
                
                if (num_len <= 2) { // Max 99 operands
                    for (const char* p = num_start; p < num_end; p++) {
                        operand_num = operand_num * 10 + (*p - '0');
                    }
                    // It's an operand reference if the number is less than num_operands
                    // AND it's not followed by 'x' (like $0x10 which is hex)
                    if (operand_num < num_operands && *num_end != 'x' && *num_end != 'X') {
                        is_operand_ref = true;
                    }
                }
                
                if (is_operand_ref) {
                    // Keep as-is (operand reference)
                    *dst++ = *src++;
                } else {
                    // Escape: $ -> $$
                    *dst++ = '$';
                    *dst++ = '$';
                    src++;
                }
            }
            // Check if followed by - and digit (negative immediate like $-1)
            else if (src[1] == '-' && src[2] >= '0' && src[2] <= '9') {
                // Escape: $ -> $$
                *dst++ = '$';
                *dst++ = '$';
                src++;
            }
            else {
                // Some other use of $, copy as-is
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    return output;
}

static bool emit_inline_asm(LLVMBackend* b, const FcIRInstruction* i) {
    // The inline asm data is stored as a pointer in operands[0].value.imm
    // This is a bit of a hack but works for passing through the data
    
    if (i->operand_count < 1) {
        b->instruction_count++;
        return true;
    }
    
    // Cast the immediate value back to the inline_asm struct pointer
    // Note: This relies on the struct still being valid in memory
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
    } *asm_data = (void*)(uintptr_t)i->operands[0].u.immediate;
    
    if (!asm_data || !asm_data->asm_template) {
        b->instruction_count++;
        return true;
    }
    
    LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
    
    // Detect clobbers from the asm template
    bool has_syscall = false;
    bool has_memory_write = false;
    uint32_t detected_clobbers = detect_asm_clobbers(asm_data->asm_template, &has_syscall, &has_memory_write);
    
    // Build constraint string
    // Format: outputs,inputs,clobbers
    // e.g., "=r,=a,r,m,~{memory},~{cc}"
    char constraint_str[1024] = {0};
    size_t pos = 0;
    
    // Add output constraints
    for (uint8_t j = 0; j < asm_data->output_count && pos < sizeof(constraint_str) - 10; j++) {
        if (j > 0) constraint_str[pos++] = ',';
        const char* c = asm_data->output_constraints[j];
        while (*c && pos < sizeof(constraint_str) - 10) {
            constraint_str[pos++] = *c++;
        }
    }
    
    // Add input constraints
    for (uint8_t j = 0; j < asm_data->input_count && pos < sizeof(constraint_str) - 10; j++) {
        if (pos > 0) constraint_str[pos++] = ',';
        const char* c = asm_data->input_constraints[j];
        while (*c && pos < sizeof(constraint_str) - 10) {
            constraint_str[pos++] = *c++;
        }
    }
    
    // Build and add detected clobbers
    char* auto_clobbers = build_clobber_string(detected_clobbers, has_syscall, has_memory_write,
                                                asm_data->clobbers, asm_data->clobber_count);
    if (auto_clobbers && pos < sizeof(constraint_str) - strlen(auto_clobbers) - 2) {
        if (pos > 0) constraint_str[pos++] = ',';
        strcpy(constraint_str + pos, auto_clobbers);
        pos += strlen(auto_clobbers);
        free(auto_clobbers);
    } else if (auto_clobbers) {
        free(auto_clobbers);
    }
    
    constraint_str[pos] = '\0';
    
    // Build function type based on inputs/outputs
    uint8_t total_inputs = asm_data->input_count;
    LLVMTypeRef* param_types = NULL;
    if (total_inputs > 0) {
        param_types = malloc(total_inputs * sizeof(LLVMTypeRef));
        for (uint8_t j = 0; j < total_inputs; j++) {
            param_types[j] = i64;
        }
    }
    
    // Return type: void if no outputs, i64 if one output, struct if multiple
    LLVMTypeRef ret_type;
    if (asm_data->output_count == 0) {
        ret_type = LLVMVoidTypeInContext(b->context);
    } else if (asm_data->output_count == 1) {
        ret_type = i64;
    } else {
        // Multiple outputs - use struct type
        LLVMTypeRef* out_types = malloc(asm_data->output_count * sizeof(LLVMTypeRef));
        for (uint8_t j = 0; j < asm_data->output_count; j++) {
            out_types[j] = i64;
        }
        ret_type = LLVMStructTypeInContext(b->context, out_types, asm_data->output_count, false);
        free(out_types);
    }
    
    LLVMTypeRef fn_ty = LLVMFunctionType(ret_type, param_types, total_inputs, false);
    free(param_types);
    
    // Get input values
    LLVMValueRef* args = NULL;
    if (total_inputs > 0) {
        args = malloc(total_inputs * sizeof(LLVMValueRef));
        for (uint8_t j = 0; j < asm_data->input_count; j++) {
            if (asm_data->inputs) {
                LLVMValueRef v = get_vreg(b, asm_data->inputs[j]);
                args[j] = v ? v : LLVMConstInt(i64, 0, false);
            } else {
                args[j] = LLVMConstInt(i64, 0, false);
            }
        }
    }
    
    // Create inline asm with preprocessed template
    // Preprocess to auto-escape $ for AT&T immediates (but keep operand refs)
    int num_operands = asm_data->output_count + asm_data->input_count;
    char* processed_template = preprocess_asm_template(asm_data->asm_template, num_operands);
    const char* final_template = processed_template ? processed_template : asm_data->asm_template;
    
    LLVMValueRef ia = LLVMGetInlineAsm(fn_ty, 
        (char*)final_template, strlen(final_template),
        constraint_str, strlen(constraint_str),
        asm_data->is_volatile, false, LLVMInlineAsmDialectATT, false);
    
    free(processed_template);  // Safe to free NULL
    
    // Call the inline asm
    LLVMValueRef res = LLVMBuildCall2(b->builder, fn_ty, ia, args, total_inputs, 
                                       asm_data->output_count > 0 ? "asm_result" : "");
    
    // Store outputs
    if (asm_data->output_count == 1 && asm_data->outputs) {
        set_vreg(b, asm_data->outputs[0], res);
    } else if (asm_data->output_count > 1 && asm_data->outputs) {
        for (uint8_t j = 0; j < asm_data->output_count; j++) {
            LLVMValueRef out_val = LLVMBuildExtractValue(b->builder, res, j, "asm_out");
            set_vreg(b, asm_data->outputs[j], out_val);
        }
    }
    
    free(args);
    b->instruction_count++;
    return true;
}

static bool emit_fence(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMAtomicOrdering ord = LLVMAtomicOrderingSequentiallyConsistent;
    if (i->opcode == FCIR_LFENCE) ord = LLVMAtomicOrderingAcquire;
    else if (i->opcode == FCIR_SFENCE) ord = LLVMAtomicOrderingRelease;
    LLVMBuildFence(b->builder, ord, false, "");
    b->instruction_count++;
    return true;
}

static bool emit_prefetch(LLVMBackend* b, const FcIRInstruction* i) {
    // Get the address to prefetch - handle memory operand specially
    LLVMValueRef addr = NULL;
    
    if (i->operands[0].type == FC_OPERAND_MEMORY) {
        // Memory operand: get the base register
        LLVMValueRef base = get_vreg(b, i->operands[0].u.memory.base);
        if (!base) {
            // If base not set, use a zero value
            base = LLVMConstInt(LLVMInt64TypeInContext(b->context), 0, false);
        }
        
        // Add displacement if present
        if (i->operands[0].u.memory.displacement != 0) {
            LLVMValueRef offset = LLVMConstInt(LLVMInt64TypeInContext(b->context), 
                                               i->operands[0].u.memory.displacement, true);
            base = LLVMBuildAdd(b->builder, base, offset, "prefetch_addr");
        }
        
        // Convert to pointer
        LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(b->context), 0);
        addr = LLVMBuildIntToPtr(b->builder, base, ptr_type, "prefetch_ptr");
    } else {
        addr = get_operand(b, &i->operands[0]);
        if (!addr) return false;
        
        // Ensure addr is a pointer type
        LLVMTypeRef addr_type = LLVMTypeOf(addr);
        if (LLVMGetTypeKind(addr_type) != LLVMPointerTypeKind) {
            // Convert integer to pointer
            LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(b->context), 0);
            addr = LLVMBuildIntToPtr(b->builder, addr, ptr_type, "prefetch_ptr");
        }
    }
    
    // Get the prefetch intrinsic
    // llvm.prefetch(ptr, rw, locality, cache_type)
    // rw: 0 = read, 1 = write
    // locality: 0-3 (3 = keep in all caches, 0 = non-temporal)
    // cache_type: 0 = instruction, 1 = data
    
    int rw = 0;  // read
    int locality = 3;  // keep in all caches (T0)
    
    switch (i->opcode) {
        case FCIR_PREFETCHT0: rw = 0; locality = 3; break;
        case FCIR_PREFETCHT1: rw = 0; locality = 2; break;
        case FCIR_PREFETCHT2: rw = 0; locality = 1; break;
        case FCIR_PREFETCHNTA: rw = 0; locality = 0; break;
        case FCIR_PREFETCHW: rw = 1; locality = 3; break;
        default: break;
    }
    
    // Build the prefetch intrinsic call using modern API (LLVM 12+)
    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(b->context), 0);
    LLVMTypeRef overload_types[] = { ptr_type };
    unsigned intrinsic_id = LLVMLookupIntrinsicID("llvm.prefetch", 13);
    LLVMValueRef prefetch_fn = LLVMGetIntrinsicDeclaration(b->module, intrinsic_id, overload_types, 1);
    
    LLVMTypeRef param_types[] = {
        ptr_type,
        LLVMInt32TypeInContext(b->context),
        LLVMInt32TypeInContext(b->context),
        LLVMInt32TypeInContext(b->context)
    };
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(b->context), param_types, 4, false);
    
    LLVMValueRef args[] = {
        addr,
        LLVMConstInt(LLVMInt32TypeInContext(b->context), rw, false),
        LLVMConstInt(LLVMInt32TypeInContext(b->context), locality, false),
        LLVMConstInt(LLVMInt32TypeInContext(b->context), 1, false)  // data cache
    };
    
    LLVMBuildCall2(b->builder, fn_type, prefetch_fn, args, 4, "");
    b->instruction_count++;
    return true;
}

static bool emit_atomic_rmw(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef ptr = get_operand(b, &i->operands[0]);
    LLVMValueRef val = get_operand(b, &i->operands[1]);
    if (!ptr || !val) return false;
    
    LLVMAtomicRMWBinOp op;
    
    switch (i->opcode) {
        case FCIR_XADD: op = LLVMAtomicRMWBinOpAdd; break;
        case FCIR_XCHG: op = LLVMAtomicRMWBinOpXchg; break;
        default: return false;
    }
    
    LLVMValueRef res = LLVMBuildAtomicRMW(b->builder, op, ptr, val,
        LLVMAtomicOrderingSequentiallyConsistent, false);
    
    if (i->operands[0].type == FC_OPERAND_VREG) {
        set_vreg(b, i->operands[0].u.vreg, res);
    }
    b->instruction_count++;
    return true;
}

static bool emit_cmpxchg(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef ptr = get_operand(b, &i->operands[0]);
    LLVMValueRef expected = get_vreg(b, (VirtualReg){.id = 1, .size = 8}); // RAX
    LLVMValueRef newval = get_operand(b, &i->operands[1]);
    if (!ptr || !expected || !newval) return false;
    
    LLVMValueRef res = LLVMBuildAtomicCmpXchg(b->builder, ptr, expected, newval,
        LLVMAtomicOrderingSequentiallyConsistent,
        LLVMAtomicOrderingSequentiallyConsistent, false);
    
    LLVMValueRef old_val = LLVMBuildExtractValue(b->builder, res, 0, "");
    set_vreg(b, (VirtualReg){.id = 1, .size = 8}, old_val);
    b->instruction_count++;
    return true;
}

static bool emit_bitfield(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef val = get_operand(b, &i->operands[0]);
    LLVMValueRef bit = get_operand(b, &i->operands[1]);
    if (!val || !bit) return false;
    
    LLVMTypeRef ty = LLVMTypeOf(val);
    LLVMValueRef one = LLVMConstInt(ty, 1, 0);
    LLVMValueRef mask = LLVMBuildShl(b->builder, one, bit, "");
    LLVMValueRef res = NULL;
    
    switch (i->opcode) {
        case FCIR_BTS: // Bit Test and Set
            res = LLVMBuildOr(b->builder, val, mask, "");
            break;
        case FCIR_BTR: // Bit Test and Reset
            res = LLVMBuildAnd(b->builder, val, LLVMBuildNot(b->builder, mask, ""), "");
            break;
        case FCIR_BTC: // Bit Test and Complement
            res = LLVMBuildXor(b->builder, val, mask, "");
            break;
        default:
            return false;
    }
    
    if (i->operands[0].type == FC_OPERAND_VREG) {
        set_vreg(b, i->operands[0].u.vreg, res);
    }
    b->instruction_count++;
    return true;
}

static bool emit_bitscan(LLVMBackend* b, const FcIRInstruction* i) {
    LLVMValueRef val = get_operand(b, &i->operands[1]);
    if (!val) return false;
    
    LLVMTypeRef ty = LLVMTypeOf(val);
    LLVMValueRef res;
    
    // Use LLVM intrinsics for BSF/BSR
    LLVMTypeRef overload_types[] = { ty };
    
    if (i->opcode == FCIR_BSF) {
        // BSF: Bit Scan Forward = count trailing zeros (cttz)
        unsigned intrinsic_id = LLVMLookupIntrinsicID("llvm.cttz", 9);
        LLVMValueRef cttz_fn = LLVMGetIntrinsicDeclaration(b->module, intrinsic_id, overload_types, 1);
        
        // cttz(val, is_zero_poison) - second arg is i1 for undefined behavior on zero
        LLVMTypeRef param_types[] = { ty, LLVMInt1TypeInContext(b->context) };
        LLVMTypeRef fn_ty = LLVMFunctionType(ty, param_types, 2, false);
        LLVMValueRef args[] = { val, LLVMConstInt(LLVMInt1TypeInContext(b->context), 0, false) };
        res = LLVMBuildCall2(b->builder, fn_ty, cttz_fn, args, 2, "");
    } else {
        // BSR: Bit Scan Reverse = (bit_width - 1) - count leading zeros (ctlz)
        unsigned intrinsic_id = LLVMLookupIntrinsicID("llvm.ctlz", 9);
        LLVMValueRef ctlz_fn = LLVMGetIntrinsicDeclaration(b->module, intrinsic_id, overload_types, 1);
        
        // ctlz(val, is_zero_poison) - second arg is i1 for undefined behavior on zero
        LLVMTypeRef param_types[] = { ty, LLVMInt1TypeInContext(b->context) };
        LLVMTypeRef fn_ty = LLVMFunctionType(ty, param_types, 2, false);
        LLVMValueRef args[] = { val, LLVMConstInt(LLVMInt1TypeInContext(b->context), 0, false) };
        LLVMValueRef lz = LLVMBuildCall2(b->builder, fn_ty, ctlz_fn, args, 2, "");
        
        // BSR result = (bit_width - 1) - ctlz
        unsigned bit_width = LLVMGetIntTypeWidth(ty);
        LLVMValueRef max_bit = LLVMConstInt(ty, bit_width - 1, false);
        res = LLVMBuildSub(b->builder, max_bit, lz, "");
    }
    
    if (i->operands[0].type == FC_OPERAND_VREG) {
        set_vreg(b, i->operands[0].u.vreg, res);
    }
    b->instruction_count++;
    return true;
}

static bool emit_label(LLVMBackend* b, const FcIRInstruction* i) {
    uint32_t id = i->operands[0].u.label_id;
    ensure_label(b, id);
    LLVMBasicBlockRef blk = get_label(b, id);
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(b->builder);
    
    // If current block doesn't have a terminator, add a branch to the label
    if (cur && !LLVMGetBasicBlockTerminator(cur)) {
        LLVMBuildBr(b->builder, blk);
    }
    
    // Check if the label block already has instructions (was already visited)
    // If so, we need to be careful not to add duplicate code
    LLVMValueRef first_instr = LLVMGetFirstInstruction(blk);
    if (first_instr) {
        // Block already has instructions - just position at end
        LLVMPositionBuilderAtEnd(b->builder, blk);
    } else {
        // Empty block - position at end
        LLVMPositionBuilderAtEnd(b->builder, blk);
    }
    
    b->current_func_ctx->current_block = blk;
    b->instruction_count++;
    return true;
}


bool llvm_emit_instruction(LLVMBackend* b, const FcIRInstruction* i) {
    if (!b || !i) return false;
    switch (i->opcode) {
        case FCIR_MOV: case FCIR_MOVZX: case FCIR_MOVSX: case FCIR_LEA:
            return emit_mov(b, i);
        case FCIR_ADD: case FCIR_SUB: case FCIR_IMUL: case FCIR_AND: case FCIR_OR: case FCIR_XOR:
            return emit_binary(b, i);
        case FCIR_IDIV:
            return emit_div(b, i);
        case FCIR_NEG: case FCIR_NOT: case FCIR_INC: case FCIR_DEC:
            return emit_unary(b, i);
        case FCIR_SHL: case FCIR_SHR: case FCIR_SAR: case FCIR_ROL: case FCIR_ROR:
            return emit_shift(b, i);
        case FCIR_CMP: case FCIR_TEST:
            return emit_cmp(b, i);
        case FCIR_JMP:
            return emit_jmp(b, i);
        case FCIR_JE: case FCIR_JNE: case FCIR_JL: case FCIR_JLE: case FCIR_JG: case FCIR_JGE:
        case FCIR_JA: case FCIR_JB: case FCIR_JAE: case FCIR_JBE:
            return emit_jcc(b, i);
        case FCIR_CALL:
            return emit_call(b, i);
        case FCIR_RET:
            return emit_ret(b, i);
        case FCIR_SYSCALL:
            return emit_syscall(b, i);
        case FCIR_MFENCE: case FCIR_LFENCE: case FCIR_SFENCE:
            return emit_fence(b, i);
        case FCIR_PREFETCHT0: case FCIR_PREFETCHT1: case FCIR_PREFETCHT2:
        case FCIR_PREFETCHNTA: case FCIR_PREFETCHW:
            return emit_prefetch(b, i);
        case FCIR_LABEL:
            return emit_label(b, i);
        case FCIR_CMPXCHG:
            return emit_cmpxchg(b, i);
        case FCIR_XCHG: case FCIR_XADD:
            return emit_atomic_rmw(b, i);
        case FCIR_BTS: case FCIR_BTR: case FCIR_BTC:
            return emit_bitfield(b, i);
        case FCIR_BSF: case FCIR_BSR:
            return emit_bitscan(b, i);
        case FCIR_PUSH: case FCIR_POP:
            b->instruction_count++;
            return true;
        case FCIR_ENTER: case FCIR_LEAVE: case FCIR_LOCK: case FCIR_ALIGN:
            b->instruction_count++;
            return true;
        case FCIR_INLINE_ASM:
            return emit_inline_asm(b, i);
        default:
            return true;
    }
}

bool llvm_emit_block(LLVMBackend* b, const FcIRBasicBlock* blk) {
    if (!b || !blk) return false;
    
    for (uint32_t i = 0; i < blk->instruction_count; i++) {
        if (!llvm_emit_instruction(b, &blk->instructions[i])) return false;
    }
    b->block_count++;
    return true;
}

// Properly rewritten with defensive programming and O(n) complexity
bool llvm_emit_function(LLVMBackend* b, const FcIRFunction* fn) {
    if (!b || !fn) return false;
    
    LLVMFunctionContext* ctx = NULL;
    uint32_t* vreg_write_count = NULL;
    uint32_t* label_to_block_index = NULL;
    uint32_t* used_vregs = NULL;
    uint32_t used_vreg_count = 0;
    bool success = false;
    
    // Single i64 type reference
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(b->context);
    if (!i64_ty) {
        set_error(b, "Failed to create i64 type");
        return false;
    }
    
    // Build parameter types array
    LLVMTypeRef* param_types = NULL;
    if (fn->parameter_count > 0) {
        param_types = malloc(fn->parameter_count * sizeof(LLVMTypeRef));
        if (!param_types) {
            set_error(b, "Failed to allocate parameter types");
            return false;
        }
        for (uint8_t i = 0; i < fn->parameter_count; i++) {
            param_types[i] = i64_ty;
        }
    }
    
    // Create function
    LLVMTypeRef fn_ty = LLVMFunctionType(i64_ty, param_types, fn->parameter_count, false);
    LLVMValueRef func = LLVMAddFunction(b->module, fn->name, fn_ty);
    free(param_types);
    param_types = NULL;
    
    if (!func) {
        set_error(b, "Failed to add function '%s'", fn->name);
        return false;
    }
    
    // ========================================================================
    // Phase 1: Scan to determine requirements
    // ========================================================================
    
    // Find maximum vreg ID and label ID used
    uint32_t max_vreg_id = 0;
    uint32_t max_label_id = 0;
    
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        
        // Track max label ID
        if (blk->id > max_label_id) {
            max_label_id = blk->id;
        }
        
        for (uint32_t j = 0; j < blk->instruction_count; j++) {
            const FcIRInstruction* instr = &blk->instructions[j];
            
            // Track max vreg ID
            for (uint8_t k = 0; k < instr->operand_count; k++) {
                if (instr->operands[k].type == FC_OPERAND_VREG) {
                    uint32_t vreg_id = instr->operands[k].u.vreg.id;
                    if (vreg_id > max_vreg_id) {
                        max_vreg_id = vreg_id;
                    }
                }
            }
            
            // Track label IDs from jumps
            if (instr->opcode == FCIR_JMP || 
                (instr->opcode >= FCIR_JE && instr->opcode <= FCIR_JBE)) {
                if (instr->operand_count > 0 && instr->operands[0].type == FC_OPERAND_LABEL) {
                    uint32_t label_id = instr->operands[0].u.label_id;
                    if (label_id > max_label_id) {
                        max_label_id = label_id;
                    }
                }
            }
        }
    }
    
    // Allocate context with actual required sizes
    ctx = calloc(1, sizeof(LLVMFunctionContext));
    if (!ctx) {
        set_error(b, "Failed to allocate function context");
        goto cleanup;
    }
    
    ctx->function = func;
    ctx->vreg_capacity = max_vreg_id + 1;
    ctx->label_count = max_label_id + 1;
    
    // Allocate vreg arrays only for what we need
    if (ctx->vreg_capacity > 0) {
        ctx->vreg_values = calloc(ctx->vreg_capacity, sizeof(LLVMValueRef));
        ctx->vreg_allocas = calloc(ctx->vreg_capacity, sizeof(LLVMValueRef));
        ctx->vreg_is_mutable = calloc(ctx->vreg_capacity, sizeof(bool));
        ctx->vreg_types = calloc(ctx->vreg_capacity, sizeof(VRegType));
        
        if (!ctx->vreg_values || !ctx->vreg_allocas || !ctx->vreg_is_mutable || !ctx->vreg_types) {
            set_error(b, "Failed to allocate vreg arrays");
            goto cleanup;
        }
    }
    
    // Allocate label blocks array
    if (ctx->label_count > 0) {
        ctx->label_blocks = calloc(ctx->label_count, sizeof(LLVMBasicBlockRef));
        if (!ctx->label_blocks) {
            set_error(b, "Failed to allocate label blocks");
            goto cleanup;
        }
    }
    
    b->current_func_ctx = ctx;
    
    // ========================================================================
    // Phase 2: Determine which vregs are mutable (O(n) algorithm)
    // ========================================================================
    
    // Count writes to each vreg
    vreg_write_count = calloc(ctx->vreg_capacity, sizeof(uint32_t));
    if (!vreg_write_count) {
        set_error(b, "Failed to allocate vreg write count");
        goto cleanup;
    }
    
    // Build label->block_index mapping for O(1) lookup
    label_to_block_index = calloc(ctx->label_count, sizeof(uint32_t));
    if (!label_to_block_index) {
        set_error(b, "Failed to allocate label mapping");
        goto cleanup;
    }
    
    // Initialize to invalid index
    for (uint32_t i = 0; i < ctx->label_count; i++) {
        label_to_block_index[i] = UINT32_MAX;
    }
    
    // Map label IDs to block indices
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        if (blk->id < ctx->label_count) {
            label_to_block_index[blk->id] = i;
        }
    }
    
    // Count writes to determine multi-assignment vregs
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        for (uint32_t j = 0; j < blk->instruction_count; j++) {
            const FcIRInstruction* instr = &blk->instructions[j];
            
            // First operand is typically the destination
            if (instr->operand_count > 0 && instr->operands[0].type == FC_OPERAND_VREG) {
                uint32_t vreg_id = instr->operands[0].u.vreg.id;
                if (vreg_id < ctx->vreg_capacity) {
                    vreg_write_count[vreg_id]++;
                }
            }
        }
    }
    
    // Mark multi-assigned vregs as mutable
    for (uint32_t i = 0; i < ctx->vreg_capacity; i++) {
        if (vreg_write_count[i] > 1) {
            ctx->vreg_is_mutable[i] = true;
        }
    }
    
    // O(n) loop detection: mark vregs in loops as mutable
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        
        for (uint32_t j = 0; j < blk->instruction_count; j++) {
            const FcIRInstruction* instr = &blk->instructions[j];
            
            // Check for backward jumps (loops)
            if (instr->opcode == FCIR_JMP || 
                (instr->opcode >= FCIR_JE && instr->opcode <= FCIR_JBE)) {
                
                if (instr->operand_count > 0 && instr->operands[0].type == FC_OPERAND_LABEL) {
                    uint32_t target_label = instr->operands[0].u.label_id;
                    
                    // Validate label ID
                    if (target_label >= ctx->label_count) {
                        set_error(b, "Invalid label ID %u (max %u)", target_label, ctx->label_count - 1);
                        goto cleanup;
                    }
                    
                    uint32_t target_index = label_to_block_index[target_label];
                    
                    // Backward jump = loop - only mark loop-carried vregs as mutable
                    // A vreg is loop-carried if it's written in the loop AND read before being written
                    if (target_index != UINT32_MAX && target_index <= i) {
                        // Track which vregs are written in the loop
                        bool* written_in_loop = calloc(ctx->vreg_capacity, sizeof(bool));
                        bool* read_before_write = calloc(ctx->vreg_capacity, sizeof(bool));
                        
                        if (written_in_loop && read_before_write) {
                            // Scan loop body to find loop-carried dependencies
                            for (uint32_t k = target_index; k <= i; k++) {
                                const FcIRBasicBlock* loop_blk = &fn->blocks[k];
                                for (uint32_t m = 0; m < loop_blk->instruction_count; m++) {
                                    const FcIRInstruction* loop_instr = &loop_blk->instructions[m];
                                    
                                    // Check reads (source operands)
                                    for (uint8_t op = 1; op < loop_instr->operand_count; op++) {
                                        if (loop_instr->operands[op].type == FC_OPERAND_VREG) {
                                            uint32_t vreg_id = loop_instr->operands[op].u.vreg.id;
                                            if (vreg_id < ctx->vreg_capacity && !written_in_loop[vreg_id]) {
                                                read_before_write[vreg_id] = true;
                                            }
                                        }
                                    }
                                    
                                    // Check writes (destination operand)
                                    if (loop_instr->operand_count > 0 && 
                                        loop_instr->operands[0].type == FC_OPERAND_VREG) {
                                        uint32_t vreg_id = loop_instr->operands[0].u.vreg.id;
                                        if (vreg_id < ctx->vreg_capacity) {
                                            written_in_loop[vreg_id] = true;
                                        }
                                    }
                                }
                            }
                            
                            // Only mark vregs that are both written AND read-before-write as mutable
                            for (uint32_t v = 0; v < ctx->vreg_capacity; v++) {
                                if (written_in_loop[v] && read_before_write[v]) {
                                    ctx->vreg_is_mutable[v] = true;
                                }
                            }
                        }
                        
                        free(written_in_loop);
                        free(read_before_write);
                    }
                }
            }
        }
    }
    
    // ========================================================================
    // Phase 2.5: Collect vreg types from instructions
    // ========================================================================
    
    // Scan all instructions to determine vreg types
    // Use the LARGEST type when a vreg is used with multiple types
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        for (uint32_t j = 0; j < blk->instruction_count; j++) {
            const FcIRInstruction* instr = &blk->instructions[j];
            
            // Check all operands for vreg types
            for (uint8_t k = 0; k < instr->operand_count; k++) {
                if (instr->operands[k].type == FC_OPERAND_VREG) {
                    uint32_t vreg_id = instr->operands[k].u.vreg.id;
                    VRegType vreg_type = instr->operands[k].u.vreg.type;
                    uint8_t vreg_size = instr->operands[k].u.vreg.size;
                    if (vreg_id < ctx->vreg_capacity && ctx->vreg_types) {
                        // Use the largest type for this vreg
                        if (vreg_type != VREG_TYPE_VOID) {
                            VRegType current_type = ctx->vreg_types[vreg_id];
                            // Get current size
                            uint8_t current_size = 0;
                            switch (current_type) {
                                case VREG_TYPE_I8: case VREG_TYPE_U8: case VREG_TYPE_BOOL: current_size = 1; break;
                                case VREG_TYPE_I16: case VREG_TYPE_U16: current_size = 2; break;
                                case VREG_TYPE_I32: case VREG_TYPE_U32: case VREG_TYPE_F32: current_size = 4; break;
                                case VREG_TYPE_I64: case VREG_TYPE_U64: case VREG_TYPE_F64: 
                                case VREG_TYPE_PTR: case VREG_TYPE_RAWPTR: case VREG_TYPE_BYTEPTR: current_size = 8; break;
                                case VREG_TYPE_I128: case VREG_TYPE_U128: current_size = 16; break;
                                case VREG_TYPE_I256: case VREG_TYPE_U256: current_size = 32; break;
                                case VREG_TYPE_I512: case VREG_TYPE_U512: current_size = 64; break;
                                case VREG_TYPE_I1024: case VREG_TYPE_U1024: current_size = 128; break;
                                default: current_size = 0; break;
                            }
                            // Only update if new type is larger
                            if (vreg_size > current_size) {
                                ctx->vreg_types[vreg_id] = vreg_type;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ========================================================================
    // Phase 3: Create LLVM IR structure
    // ========================================================================
    
    // Create entry block for allocas
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(b->context, func, "entry");
    if (!entry) {
        set_error(b, "Failed to create entry block");
        goto cleanup;
    }
    
    ctx->current_block = entry;
    LLVMPositionBuilderAtEnd(b->builder, entry);
    
    // Create allocas only for mutable vregs (optimization)
    // Build list of used vregs to avoid scanning all 2048+ slots
    used_vregs = malloc(ctx->vreg_capacity * sizeof(uint32_t));
    if (!used_vregs) {
        set_error(b, "Failed to allocate used vregs list");
        goto cleanup;
    }
    
    for (uint32_t i = 0; i < ctx->vreg_capacity; i++) {
        if (ctx->vreg_is_mutable[i]) {
            used_vregs[used_vreg_count++] = i;
        }
    }
    
    // Create allocas only for mutable vregs
    for (uint32_t i = 0; i < used_vreg_count; i++) {
        uint32_t vreg_id = used_vregs[i];
        char name[32];
        snprintf(name, sizeof(name), "v%u.addr", vreg_id);
        
        // Use the vreg's type for the alloca, default to i64 if unknown
        LLVMTypeRef alloca_type;
        if (ctx->vreg_types && ctx->vreg_types[vreg_id] != VREG_TYPE_VOID) {
            alloca_type = llvm_type_for_vreg(b, ctx->vreg_types[vreg_id]);
        } else {
            alloca_type = i64_ty;
        }
        
        ctx->vreg_allocas[vreg_id] = LLVMBuildAlloca(b->builder, alloca_type, name);
        if (!ctx->vreg_allocas[vreg_id]) {
            set_error(b, "Failed to create alloca for v%u", vreg_id);
            goto cleanup;
        }
        
        // Initialize to zero
        LLVMValueRef zero;
        if (LLVMGetTypeKind(alloca_type) == LLVMIntegerTypeKind) {
            zero = LLVMConstInt(alloca_type, 0, false);
        } else if (LLVMGetTypeKind(alloca_type) == LLVMFloatTypeKind || 
                   LLVMGetTypeKind(alloca_type) == LLVMDoubleTypeKind) {
            zero = LLVMConstReal(alloca_type, 0.0);
        } else {
            zero = LLVMConstNull(alloca_type);
        }
        LLVMBuildStore(b->builder, zero, ctx->vreg_allocas[vreg_id]);
    }
    
    // Set function parameters
    for (uint8_t i = 0; i < fn->parameter_count; i++) {
        LLVMValueRef param = LLVMGetParam(func, i);
        if (!param) {
            set_error(b, "Failed to get parameter %u", i);
            goto cleanup;
        }
        
        VirtualReg vreg;
        if (fn->parameters) {
            vreg = fn->parameters[i];
        } else {
            vreg = (VirtualReg){.id = i + 1, .size = 8};
        }
        
        // Validate vreg ID
        if (vreg.id >= ctx->vreg_capacity) {
            set_error(b, "Parameter vreg ID %u out of bounds", vreg.id);
            goto cleanup;
        }
        
        set_vreg(b, vreg, param);
    }
    
    // Create all basic blocks upfront
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        
        // Validate block ID
        if (blk->id >= ctx->label_count) {
            set_error(b, "Block ID %u exceeds max label ID %u", blk->id, ctx->label_count - 1);
            goto cleanup;
        }
        
        char name[32];
        snprintf(name, sizeof(name), "L%u", blk->id);
        
        LLVMBasicBlockRef llvm_blk = LLVMAppendBasicBlockInContext(b->context, func, name);
        if (!llvm_blk) {
            set_error(b, "Failed to create basic block L%u", blk->id);
            goto cleanup;
        }
        
        ctx->label_blocks[blk->id] = llvm_blk;
    }
    
    // Branch from entry to first block (or return if no blocks)
    if (fn->block_count > 0) {
        uint32_t first_label = fn->blocks[0].id;
        if (first_label >= ctx->label_count || !ctx->label_blocks[first_label]) {
            set_error(b, "Invalid first block label %u", first_label);
            goto cleanup;
        }
        LLVMBuildBr(b->builder, ctx->label_blocks[first_label]);
    } else {
        LLVMBuildRet(b->builder, LLVMConstInt(i64_ty, 0, false));
        success = true;
        goto cleanup;
    }
    
    // ========================================================================
    // Phase 4: Emit instructions for each block
    // ========================================================================
    
    for (uint32_t i = 0; i < fn->block_count; i++) {
        const FcIRBasicBlock* blk = &fn->blocks[i];
        
        // Validate block ID
        if (blk->id >= ctx->label_count) {
            set_error(b, "Block ID %u out of bounds", blk->id);
            goto cleanup;
        }
        
        LLVMBasicBlockRef llvm_blk = ctx->label_blocks[blk->id];
        if (!llvm_blk) {
            set_error(b, "Block L%u was not created", blk->id);
            goto cleanup;
        }
        
        LLVMPositionBuilderAtEnd(b->builder, llvm_blk);
        ctx->current_block = llvm_blk;
        
        // Emit all instructions in the block
        if (!llvm_emit_block(b, blk)) {
            // Error already set by llvm_emit_block
            goto cleanup;
        }
        
        // Add fallthrough terminator if block doesn't have one
        LLVMBasicBlockRef cur = LLVMGetInsertBlock(b->builder);
        if (cur && !LLVMGetBasicBlockTerminator(cur)) {
            if (i + 1 < fn->block_count) {
                // Branch to next block
                uint32_t next_label = fn->blocks[i + 1].id;
                if (next_label >= ctx->label_count || !ctx->label_blocks[next_label]) {
                    set_error(b, "Invalid next block label %u", next_label);
                    goto cleanup;
                }
                LLVMBuildBr(b->builder, ctx->label_blocks[next_label]);
            } else {
                // Last block - return 0
                LLVMBuildRet(b->builder, LLVMConstInt(i64_ty, 0, false));
            }
        }
    }
    
    // Success!
    success = true;
    
cleanup:
    // Clean up temporary allocations
    free(vreg_write_count);
    free(label_to_block_index);
    free(used_vregs);
    
    // Clean up context (note: ctx->blocks is never allocated, don't free it)
    if (ctx) {
        free(ctx->vreg_values);
        free(ctx->vreg_allocas);
        free(ctx->vreg_is_mutable);
        free(ctx->label_blocks);
        free(ctx);
    }
    
    b->current_func_ctx = NULL;
    
    if (success) {
        b->function_count++;
    }
    
    return success;
}

static void emit_strings(LLVMBackend* b, const FcIRModule* m) {
    if (!m->string_count) return;
    
    // Find the maximum string ID to allocate enough space
    uint32_t max_id = 0;
    for (uint32_t i = 0; i < m->string_count; i++) {
        if (m->string_literals[i].id > max_id) {
            max_id = m->string_literals[i].id;
        }
    }
    
    // Allocate array large enough to hold all string IDs
    b->global_string_count = max_id + 1;
    b->global_strings = calloc(b->global_string_count, sizeof(LLVMValueRef));
    
    for (uint32_t i = 0; i < m->string_count; i++) {
        const FcIRStringLiteral* s = &m->string_literals[i];
        LLVMValueRef str = LLVMConstStringInContext(b->context, s->data, s->length, false);
        char name[32];
        snprintf(name, sizeof(name), ".LC%u", s->id);
        LLVMValueRef g = LLVMAddGlobal(b->module, LLVMTypeOf(str), name);
        LLVMSetInitializer(g, str);
        LLVMSetGlobalConstant(g, true);
        LLVMSetLinkage(g, LLVMPrivateLinkage);
        b->global_strings[s->id] = g;
    }
}

static void emit_externals(LLVMBackend* b, const FcIRModule* m) {
    if (!m->external_func_count) return;
    b->external_funcs = calloc(m->external_func_count, sizeof(LLVMValueRef));
    b->external_func_count = m->external_func_count;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
    LLVMTypeRef ptr = llvm_ptr_type(b);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(b->context);
    LLVMTypeRef f32 = LLVMFloatTypeInContext(b->context);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(b->context);
    
    for (uint32_t i = 0; i < m->external_func_count; i++) {
        const char* name = m->external_functions[i];
        LLVMTypeRef ft;
        
        // Match runtime function signatures from fcx_runtime.h
        if (strcmp(name, "_fcx_print_int") == 0 || strcmp(name, "_fcx_println_int") == 0 ||
            strcmp(name, "_fcx_println_hex") == 0 || strcmp(name, "_fcx_println_bin") == 0 ||
            strcmp(name, "_fcx_println_bool") == 0 || strcmp(name, "_fcx_println_char") == 0 ||
            strcmp(name, "_fcx_println_u8") == 0) {
            LLVMTypeRef p[] = {i64};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_f32") == 0) {
            LLVMTypeRef p[] = {f32};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_f64") == 0) {
            LLVMTypeRef p[] = {f64};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_ptr") == 0) {
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_i128") == 0) {
            // void _fcx_println_i128(__int128 value)
            LLVMTypeRef i128 = LLVMInt128TypeInContext(b->context);
            LLVMTypeRef p[] = {i128};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_u128") == 0) {
            // void _fcx_println_u128(unsigned __int128 value)
            LLVMTypeRef i128 = LLVMInt128TypeInContext(b->context);
            LLVMTypeRef p[] = {i128};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_i256") == 0 || strcmp(name, "_fcx_println_u256") == 0) {
            // void _fcx_println_i256(i256 value) - passed as pointer due to size
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_i512") == 0 || strcmp(name, "_fcx_println_u512") == 0) {
            // void _fcx_println_i512(i512 value) - passed as pointer due to size
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_println_i1024") == 0 || strcmp(name, "_fcx_println_u1024") == 0) {
            // void _fcx_println_i1024(i1024 value) - passed as pointer due to size
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_print_func") == 0 || strcmp(name, "_fcx_print_str") == 0 ||
                   strcmp(name, "_fcx_println") == 0) {
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_alloc") == 0) {
            // void* _fcx_alloc(size_t size, size_t alignment)
            LLVMTypeRef p[] = {i64, i64};
            ft = LLVMFunctionType(ptr, p, 2, false);
        } else if (strcmp(name, "_fcx_free") == 0) {
            // void _fcx_free(void* ptr)
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        } else if (strcmp(name, "_fcx_arena_alloc") == 0) {
            // void* _fcx_arena_alloc(size_t size, size_t alignment, uint32_t scope_id)
            LLVMTypeRef p[] = {i64, i64, LLVMInt32TypeInContext(b->context)};
            ft = LLVMFunctionType(ptr, p, 3, false);
        } else if (strcmp(name, "_fcx_slab_alloc") == 0) {
            // void* _fcx_slab_alloc(size_t object_size, uint32_t type_hash)
            LLVMTypeRef p[] = {i64, LLVMInt32TypeInContext(b->context)};
            ft = LLVMFunctionType(ptr, p, 2, false);
        } else if (strcmp(name, "_fcx_syscall") == 0) {
            // long _fcx_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6)
            LLVMTypeRef p[] = {i64, i64, i64, i64, i64, i64, i64};
            ft = LLVMFunctionType(i64, p, 7, false);
        } else if (strcmp(name, "_fcx_write") == 0 || strcmp(name, "_fcx_read") == 0) {
            // long _fcx_write/read(int fd, const void* buf, size_t count)
            LLVMTypeRef p[] = {LLVMInt32TypeInContext(b->context), ptr, i64};
            ft = LLVMFunctionType(i64, p, 3, false);
        } else if (strcmp(name, "_fcx_atomic_cas") == 0) {
            // bool _fcx_atomic_cas(volatile uint64_t* ptr, uint64_t expected, uint64_t new_val)
            LLVMTypeRef p[] = {ptr, i64, i64};
            ft = LLVMFunctionType(LLVMInt1TypeInContext(b->context), p, 3, false);
        } else if (strcmp(name, "_fcx_atomic_swap") == 0) {
            // uint64_t _fcx_atomic_swap(volatile uint64_t* ptr, uint64_t val)
            LLVMTypeRef p[] = {ptr, i64};
            ft = LLVMFunctionType(i64, p, 2, false);
        } else if (strcmp(name, "_fcx_memory_barrier") == 0 || 
                   strcmp(name, "_fcx_atomic_fence") == 0) {
            // void _fcx_memory_barrier(void)
            ft = LLVMFunctionType(void_ty, NULL, 0, false);
        } else if (strcmp(name, "_fcx_panic") == 0) {
            // void _fcx_panic(const char* message)
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(void_ty, p, 1, false);
        // String functions
        } else if (strcmp(name, "_fcx_strlen") == 0) {
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(i64, p, 1, false);
        } else if (strcmp(name, "_fcx_strcmp") == 0) {
            LLVMTypeRef p[] = {ptr, ptr};
            ft = LLVMFunctionType(i64, p, 2, false);
        } else if (strcmp(name, "_fcx_strcpy") == 0 || strcmp(name, "_fcx_strcat") == 0) {
            LLVMTypeRef p[] = {ptr, ptr};
            ft = LLVMFunctionType(ptr, p, 2, false);
        } else if (strcmp(name, "_fcx_strchr") == 0) {
            LLVMTypeRef p[] = {ptr, i64};
            ft = LLVMFunctionType(ptr, p, 2, false);
        } else if (strcmp(name, "_fcx_strstr") == 0) {
            LLVMTypeRef p[] = {ptr, ptr};
            ft = LLVMFunctionType(ptr, p, 2, false);
        // Memory functions
        } else if (strcmp(name, "_fcx_memcpy") == 0 || strcmp(name, "_fcx_memmove") == 0) {
            LLVMTypeRef p[] = {ptr, ptr, i64};
            ft = LLVMFunctionType(ptr, p, 3, false);
        } else if (strcmp(name, "_fcx_memset") == 0) {
            LLVMTypeRef p[] = {ptr, i64, i64};
            ft = LLVMFunctionType(ptr, p, 3, false);
        } else if (strcmp(name, "_fcx_memcmp") == 0) {
            LLVMTypeRef p[] = {ptr, ptr, i64};
            ft = LLVMFunctionType(i64, p, 3, false);
        // Conversion functions
        } else if (strcmp(name, "_fcx_atoi") == 0) {
            LLVMTypeRef p[] = {ptr};
            ft = LLVMFunctionType(i64, p, 1, false);
        } else if (strcmp(name, "_fcx_itoa") == 0) {
            LLVMTypeRef p[] = {i64, ptr, i64};
            ft = LLVMFunctionType(i64, p, 3, false);
        } else {
            // Default: generic function with 6 i64 args returning i64
            LLVMTypeRef p[] = {i64, i64, i64, i64, i64, i64};
            ft = LLVMFunctionType(i64, p, 6, false);
        }
        
        b->external_funcs[i] = LLVMAddFunction(b->module, name, ft);
        LLVMSetLinkage(b->external_funcs[i], LLVMExternalLinkage);
    }
}

static void emit_start(LLVMBackend* b) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(b->context);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(b->context);
    LLVMTypeRef start_ty = LLVMFunctionType(void_ty, NULL, 0, false);
    LLVMValueRef start = LLVMAddFunction(b->module, "_start", start_ty);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(b->context, start, "entry");
    LLVMPositionBuilderAtEnd(b->builder, entry);
    
    LLVMValueRef main_fn = LLVMGetNamedFunction(b->module, "main");
    LLVMValueRef ret;
    if (main_fn) {
        ret = LLVMBuildCall2(b->builder, LLVMGlobalGetValueType(main_fn), main_fn, NULL, 0, "");
    } else {
        ret = LLVMConstInt(i64, 0, 0);
    }
    
    const char* asm_str = "movq $$60, %rax\nsyscall";
    const char* cons = "{rdi},~{rax},~{rcx},~{r11}";
    LLVMTypeRef exit_params[] = {i64};
    LLVMTypeRef exit_ty = LLVMFunctionType(void_ty, exit_params, 1, false);
    LLVMValueRef ia = LLVMGetInlineAsm(exit_ty, (char*)asm_str, strlen(asm_str),
        (char*)cons, strlen(cons), true, false, LLVMInlineAsmDialectATT, false);
    LLVMValueRef args[] = {ret};
    LLVMBuildCall2(b->builder, exit_ty, ia, args, 1, "");
    LLVMBuildUnreachable(b->builder);
}

bool llvm_emit_module(LLVMBackend* b, const FcIRModule* m) {
    if (!b || !m) return false;
    llvm_backend_reset(b);
    b->fc_module = m;
    b->module = LLVMModuleCreateWithNameInContext(m->name, b->context);
    LLVMSetTarget(b->module, b->config.target_triple);
    LLVMSetDataLayout(b->module, LLVMCopyStringRepOfTargetData(b->target_data));
    
    emit_strings(b, m);
    emit_externals(b, m);
    
    for (uint32_t i = 0; i < m->function_count; i++) {
        if (!llvm_emit_function(b, &m->functions[i])) return false;
    }
    
    emit_start(b);
    
    if (b->config.verify_module && !llvm_verify_module(b)) return false;
    return true;
}

bool llvm_verify_module(LLVMBackend* b) {
    if (!b || !b->module) return false;
    char* err = NULL;
    if (LLVMVerifyModule(b->module, LLVMReturnStatusAction, &err)) {
        set_error(b, "Verify failed: %s", err ? err : "unknown");
        LLVMDisposeMessage(err);
        return false;
    }
    LLVMDisposeMessage(err);
    return true;
}

bool llvm_optimize_module(LLVMBackend* b) {
    if (!b || !b->module) return false;
    if (b->config.opt_level == LLVM_OPT_NONE) return true;
    
    // Build optimized pass pipeline based on optimization level
    // LLVM 21+ new pass manager with explicit passes for better control
    const char* passes;
    
    switch (b->config.opt_level) {
        case LLVM_OPT_LESS:
            // O1: Basic optimizations with mem2reg for SSA promotion
            passes = "function(mem2reg,sroa,early-cse,simplifycfg,instcombine)";
            break;
            
        case LLVM_OPT_DEFAULT:
            // O2: Standard optimizations - mem2reg + sroa first, then O2 pipeline
            passes = "function(mem2reg,sroa),default<O2>";
            break;
            
        case LLVM_OPT_AGGRESSIVE:
            // O3: Aggressive optimizations
            // 1. mem2reg + sroa to promote allocas to SSA
            // 2. instcombine to clean up
            // 3. Full O3 pipeline
            passes = "function(mem2reg,sroa,instcombine,simplifycfg,reassociate,gvn,dce),default<O3>";
            break;
            
        default:
            passes = "function(mem2reg,sroa),default<O2>";
            break;
    }
    
    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    
    // Configure optimization options for LLVM 21+
    if (b->config.opt_level == LLVM_OPT_AGGRESSIVE) {
        LLVMPassBuilderOptionsSetLoopVectorization(opts, true);
        LLVMPassBuilderOptionsSetSLPVectorization(opts, true);
        LLVMPassBuilderOptionsSetLoopInterleaving(opts, true);
        // Disable aggressive loop unrolling to prevent code bloat
        LLVMPassBuilderOptionsSetLoopUnrolling(opts, false);
        // Enable function merging for code size
        LLVMPassBuilderOptionsSetMergeFunctions(opts, true);
        // Increase inliner threshold for better optimization
        LLVMPassBuilderOptionsSetInlinerThreshold(opts, 250);
        // Enable call graph profiling for better inlining decisions
        LLVMPassBuilderOptionsSetCallGraphProfile(opts, true);
    } else if (b->config.opt_level == LLVM_OPT_DEFAULT) {
        LLVMPassBuilderOptionsSetLoopVectorization(opts, true);
        LLVMPassBuilderOptionsSetSLPVectorization(opts, true);
        LLVMPassBuilderOptionsSetLoopUnrolling(opts, false);
    }
    
    LLVMErrorRef e = LLVMRunPasses(b->module, passes, b->target_machine, opts);
    LLVMDisposePassBuilderOptions(opts);
    
    if (e) {
        char* msg = LLVMGetErrorMessage(e);
        set_error(b, "Opt failed: %s", msg ? msg : "unknown");
        LLVMDisposeErrorMessage(msg);
        return false;
    }
    return true;
}

bool llvm_generate_object_file(LLVMBackend* b, const char* path) {
    if (!b || !b->module || !path) return false;
    if (!llvm_optimize_module(b)) return false;
    char* err = NULL;
    if (LLVMTargetMachineEmitToFile(b->target_machine, b->module, (char*)path, LLVMObjectFile, &err)) {
        set_error(b, "Emit obj failed: %s", err ? err : "unknown");
        LLVMDisposeMessage(err);
        return false;
    }
    return true;
}

bool llvm_generate_assembly(LLVMBackend* b, const char* path) {
    if (!b || !b->module || !path) return false;
    if (!llvm_optimize_module(b)) return false;
    char* err = NULL;
    if (LLVMTargetMachineEmitToFile(b->target_machine, b->module, (char*)path, LLVMAssemblyFile, &err)) {
        set_error(b, "Emit asm failed: %s", err ? err : "unknown");
        LLVMDisposeMessage(err);
        return false;
    }
    return true;
}

bool llvm_generate_bitcode(LLVMBackend* b, const char* path) {
    if (!b || !b->module || !path) return false;
    return LLVMWriteBitcodeToFile(b->module, path) == 0;
}

void llvm_print_module(LLVMBackend* b, FILE* out) {
    if (!b || !b->module) return;
    char* ir = LLVMPrintModuleToString(b->module);
    if (ir) { fprintf(out, "%s", ir); LLVMDisposeMessage(ir); }
}

void llvm_print_statistics(const LLVMBackend* b) {
    if (!b) return;
    printf("\n=== LLVM Backend Statistics ===\n");
    printf("Functions: %u, Blocks: %u, Instructions: %u\n", b->function_count, b->block_count, b->instruction_count);
    printf("Strings: %u, Externals: %u\n", b->global_string_count, b->external_func_count);
    printf("Target: %s, CPU: %s, Opt: O%d\n", b->config.target_triple, b->config.cpu, b->config.opt_level);
}

bool llvm_link_executable(const char* obj, const char* out) {
    if (!obj || !out) return false;
    
    char cmd[2048];
    
    // Check if runtime objects exist
    const char* runtime_paths[] = {
        "obj/runtime/bootstrap.o obj/runtime/fcx_memory.o obj/runtime/fcx_syscall.o "
        "obj/runtime/fcx_atomic.o obj/runtime/fcx_hardware.o obj/runtime/fcx_runtime.o",
        "../obj/runtime/bootstrap.o ../obj/runtime/fcx_memory.o ../obj/runtime/fcx_syscall.o "
        "../obj/runtime/fcx_atomic.o ../obj/runtime/fcx_hardware.o ../obj/runtime/fcx_runtime.o",
        NULL
    };
    
    bool has_runtime = false;
    const char* runtime_objs = NULL;
    
    for (int i = 0; runtime_paths[i] != NULL; i++) {
        char first_obj[256];
        sscanf(runtime_paths[i], "%255s", first_obj);
        if (access(first_obj, F_OK) == 0) {
            has_runtime = true;
            runtime_objs = runtime_paths[i];
            break;
        }
    }
    
    if (has_runtime && runtime_objs) {
        // Link with runtime and libc (for memcpy, strlen, etc.)
        snprintf(cmd, sizeof(cmd), 
            "ld.lld -o %s -e _start --dynamic-linker /lib64/ld-linux-x86-64.so.2 %s %s -lc 2>/dev/null || "
            "lld -flavor gnu -o %s -e _start --dynamic-linker /lib64/ld-linux-x86-64.so.2 %s %s -lc 2>/dev/null || "
            "ld -o %s -e _start --dynamic-linker /lib64/ld-linux-x86-64.so.2 %s %s -lc",
            out, obj, runtime_objs,
            out, obj, runtime_objs,
            out, obj, runtime_objs);
    } else {
        // Link without runtime (standalone executable - no libc)
        snprintf(cmd, sizeof(cmd), 
            "ld.lld -o %s -e _start %s 2>/dev/null || "
            "lld -flavor gnu -o %s -e _start %s 2>/dev/null || "
            "ld -o %s -e _start %s",
            out, obj, out, obj, out, obj);
    }
    
    return system(cmd) == 0;
}

bool llvm_link_shared_library(const char* obj, const char* out) {
    if (!obj || !out) return false;
    
    char cmd[2048];
    
    // For shared libraries, we don't link the FCx runtime by default
    // The runtime contains global state that doesn't work well in shared libs
    // Users can link it separately if needed
    snprintf(cmd, sizeof(cmd), 
        "gcc -shared -fPIC -o %s %s 2>/dev/null || "
        "clang -shared -fPIC -o %s %s 2>/dev/null || "
        "ld -shared -o %s %s",
        out, obj, out, obj, out, obj);
    
    return system(cmd) == 0;
}

bool llvm_compile_and_link(LLVMBackend* b, const char* out) {
    if (!b || !b->module || !out) return false;
    char obj[256];
    snprintf(obj, sizeof(obj), "/tmp/fcx_%d.o", getpid());
    if (!llvm_generate_object_file(b, obj)) return false;
    bool ok = llvm_link_executable(obj, out);
    unlink(obj);
    if (!ok) set_error(b, "Linking failed");
    return ok;
}

bool llvm_compile_shared_library(LLVMBackend* b, const char* out) {
    if (!b || !b->module || !out) return false;
    char obj[256];
    snprintf(obj, sizeof(obj), "/tmp/fcx_%d.o", getpid());
    if (!llvm_generate_object_file(b, obj)) return false;
    bool ok = llvm_link_shared_library(obj, out);
    unlink(obj);
    if (!ok) set_error(b, "Shared library linking failed");
    return ok;
}