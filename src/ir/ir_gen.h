#ifndef IR_GEN_H
#define IR_GEN_H

#include "fcx_ir.h"
#include "../parser/parser.h"

// IR Generator context
typedef struct {
    FcxIRModule* module;
    FcxIRFunction* current_function;
    FcxIRBasicBlock* current_block;
    
    // Symbol table for variable to vreg mapping
    struct {
        char** names;
        VirtualReg* vregs;
        size_t count;
        size_t capacity;
    } symbol_table;
    
    // Label management
    uint32_t next_label_id;
    
    // Scope tracking for arena allocations
    uint32_t current_scope_id;
    
    // Loop context stack for break/continue
    struct {
        uint32_t* break_targets;    // Block IDs to jump to on break
        uint32_t* continue_targets; // Block IDs to jump to on continue
        size_t depth;
        size_t capacity;
    } loop_stack;
    
    // Error tracking
    char* error_message;
    bool has_error;
} IRGenerator;

// IR Generator lifecycle
IRGenerator* ir_gen_create(const char* module_name);
void ir_gen_destroy(IRGenerator* gen);

// Generate IR from AST
bool ir_gen_generate_module(IRGenerator* gen, Stmt** statements, size_t stmt_count);
bool ir_gen_generate_function(IRGenerator* gen, Stmt* func_stmt);
bool ir_gen_generate_statement(IRGenerator* gen, Stmt* stmt);
VirtualReg ir_gen_generate_expression(IRGenerator* gen, Expr* expr);

// Operator desugaring functions
VirtualReg ir_gen_desugar_binary_op(IRGenerator* gen, Expr* expr);
VirtualReg ir_gen_desugar_syscall(IRGenerator* gen, Expr* expr);
VirtualReg ir_gen_desugar_atomic_op(IRGenerator* gen, Expr* expr);
VirtualReg ir_gen_desugar_memory_op(IRGenerator* gen, Expr* expr);

// Symbol table management
void ir_gen_add_symbol(IRGenerator* gen, const char* name, VirtualReg vreg);
VirtualReg ir_gen_lookup_symbol(IRGenerator* gen, const char* name, bool* found);

// Helper functions
uint32_t ir_gen_alloc_label(IRGenerator* gen);
uint32_t ir_gen_enter_scope(IRGenerator* gen);
void ir_gen_exit_scope(IRGenerator* gen);
uint32_t ir_gen_current_scope(IRGenerator* gen);
VirtualReg ir_gen_alloc_temp(IRGenerator* gen, VRegType type);
VRegType ir_gen_infer_literal_type(const LiteralValue* literal);
VRegType ir_gen_map_type_kind(TypeKind kind);

// Loop generation
bool ir_gen_generate_loop(IRGenerator* gen, Stmt* stmt);

// Error handling
void ir_gen_set_error(IRGenerator* gen, const char* message);
const char* ir_gen_get_error(IRGenerator* gen);

#endif // IR_GEN_H
