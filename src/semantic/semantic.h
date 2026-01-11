#ifndef FCX_SEMANTIC_H
#define FCX_SEMANTIC_H

#include "../parser/parser.h"
#include "../types/pointer_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct SymbolTable SymbolTable;
typedef struct Symbol Symbol;
typedef struct Scope Scope;

// Symbol kinds
typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_CONSTANT,
    SYMBOL_FUNCTION,
    SYMBOL_PARAMETER,
    SYMBOL_TYPE
} SymbolKind;

// Symbol structure
struct Symbol {
    char *name;
    SymbolKind kind;
    Type *type;
    bool is_initialized;
    bool is_mutable;
    size_t scope_depth;
    
    // For functions
    Parameter *params;
    size_t param_count;
    Type *return_type;
    
    // Source location for error reporting
    size_t line;
    size_t column;
};

// Scope structure for nested scopes
struct Scope {
    Symbol **symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    Scope *parent;
    size_t depth;
};

// Symbol table structure
struct SymbolTable {
    Scope *current_scope;
    Scope *global_scope;
    size_t scope_depth;
};

// Type inference context
typedef struct {
    Type *expected_type;  // Expected type from context
    bool allow_inference; // Whether type inference is allowed
    bool in_function;     // Whether we're inside a function
    Type *function_return_type; // Current function's return type
} TypeInferenceContext;

// Semantic analyzer state
typedef struct {
    SymbolTable *symbol_table;
    TypeInferenceContext type_context;
    bool had_error;
    
    // Error reporting
    char **error_messages;
    size_t error_count;
    size_t error_capacity;
} SemanticAnalyzer;

// Pointer arithmetic rules enforcement
typedef enum {
    PTR_ARITH_ALLOWED,          // ptr<T> - scaled arithmetic
    PTR_ARITH_BYTE_WISE,        // byteptr - 1-byte arithmetic
    PTR_ARITH_FORBIDDEN,        // rawptr - no arithmetic
    PTR_ARITH_TYPE_ERROR        // Type mismatch
} PointerArithmeticRule;

// Type conversion rules
typedef enum {
    TYPE_CONV_IDENTICAL,        // Same type, no conversion needed
    TYPE_CONV_IMPLICIT,         // Implicit conversion allowed
    TYPE_CONV_EXPLICIT_CAST,    // Explicit cast required
    TYPE_CONV_INCOMPATIBLE      // Types are incompatible
} TypeConversionRule;

// Function declarations

// Symbol table operations
SymbolTable *create_symbol_table(void);
void free_symbol_table(SymbolTable *table);
void enter_scope(SymbolTable *table);
void exit_scope(SymbolTable *table);

// Symbol operations
Symbol *declare_symbol(SymbolTable *table, const char *name, SymbolKind kind, Type *type, size_t line, size_t column);
Symbol *semantic_lookup_symbol(SymbolTable *table, const char *name);
Symbol *semantic_lookup_symbol_in_scope(Scope *scope, const char *name);
bool symbol_exists_in_current_scope(SymbolTable *table, const char *name);

// Semantic analysis entry points
bool analyze_program(SemanticAnalyzer *analyzer, Stmt **statements, size_t count);
bool analyze_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
Type *analyze_expression(SemanticAnalyzer *analyzer, Expr *expr);

// Statement analysis
bool analyze_let_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
bool analyze_function_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
bool analyze_if_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
bool analyze_loop_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
bool analyze_return_statement(SemanticAnalyzer *analyzer, Stmt *stmt);
bool analyze_expression_statement(SemanticAnalyzer *analyzer, Stmt *stmt);

// Expression analysis
Type *analyze_literal_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_identifier_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_binary_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_unary_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_assignment_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_call_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_memory_op_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_atomic_op_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_syscall_op_expr(SemanticAnalyzer *analyzer, Expr *expr);

// Type inference
Type *infer_type_from_literal(const LiteralValue *literal);
Type *infer_type_from_expression(SemanticAnalyzer *analyzer, Expr *expr);
Type *infer_binary_operation_type(Type *left, Type *right, TokenKind op);
Type *infer_unary_operation_type(Type *operand, TokenKind op);

// Type checking
bool types_equal(const Type *a, const Type *b);
bool types_compatible(const Type *a, const Type *b);
TypeConversionRule get_conversion_rule(const Type *from, const Type *to);
bool can_convert_type(const Type *from, const Type *to);
bool type_requires_explicit_cast(const Type *from, const Type *to);

// Three-pointer system type checking
bool is_pointer_type(const Type *type);
bool is_typed_pointer(const Type *type);
bool is_raw_pointer(const Type *type);
bool is_byte_pointer(const Type *type);
PointerArithmeticRule get_pointer_arithmetic_rule(const Type *ptr_type);
bool can_dereference_pointer_type(const Type *ptr_type);

// Pointer arithmetic enforcement
bool check_pointer_arithmetic(SemanticAnalyzer *analyzer, Type *ptr_type, TokenKind op, Expr *expr);
bool check_pointer_assignment(SemanticAnalyzer *analyzer, Type *target_type, Type *value_type, Expr *expr);
Type *get_pointer_arithmetic_result_type(Type *ptr_type, TokenKind op, Type *operand_type);

// Pointer conversion and casting
bool check_pointer_cast(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr);
Type *apply_pointer_cast(Type *from_type, Type *to_type);
bool inject_pointer_cast_if_needed(SemanticAnalyzer *analyzer, Expr **expr, Type *target_type);

// Advanced pointer casting operators
bool check_cast_to_operator(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr);
bool check_reinterpret_cast_operator(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr);
bool check_ptr_to_int_cast(SemanticAnalyzer *analyzer, Type *ptr_type, Expr *expr);
bool check_int_to_ptr_cast(SemanticAnalyzer *analyzer, Type *int_type, Type *target_ptr_type, Expr *expr);

// Syscall pointer conversion detection and injection
bool detect_syscall_rawptr_requirement(SemanticAnalyzer *analyzer, Expr *syscall_expr);
bool inject_rawptr_cast_for_syscall(SemanticAnalyzer *analyzer, Expr **arg_expr, size_t arg_index);

// MMIO operations validation
bool validate_mmio_operation(SemanticAnalyzer *analyzer, Expr *mmio_expr, bool is_map);
bool check_mmio_volatile_semantics(SemanticAnalyzer *analyzer, Type *ptr_type, Expr *expr);

// Field access operations
Type *analyze_field_access_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_compact_field_access_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_layout_offset_expr(SemanticAnalyzer *analyzer, Expr *expr);
Type *analyze_reverse_layout_copy_expr(SemanticAnalyzer *analyzer, Expr *expr);
size_t calculate_field_offset(Type *struct_type, const char *field_name);

// Operator validation against registry
bool validate_operator_usage(SemanticAnalyzer *analyzer, TokenKind op, Type *left_type, Type *right_type, Type *third_type, Expr *expr);
bool validate_memory_barrier_usage(SemanticAnalyzer *analyzer, TokenKind op, Expr *expr);
bool validate_simd_operation(SemanticAnalyzer *analyzer, TokenKind op, Type *operand_type, Expr *expr);
const OperatorInfo *get_operator_info_by_token(TokenKind token);

// Function parameter and return type checking
bool check_function_call(SemanticAnalyzer *analyzer, Symbol *function, Expr **args, size_t arg_count, Expr *call_expr);
bool check_function_return(SemanticAnalyzer *analyzer, Type *return_type, Expr *return_expr);
bool check_parameter_types(SemanticAnalyzer *analyzer, Parameter *params, size_t param_count, Expr **args, size_t arg_count);

// Register hints (for future optimization)
typedef enum {
    REG_HINT_NONE,
    REG_HINT_GENERAL,
    REG_HINT_FLOATING,
    REG_HINT_VECTOR,
    REG_HINT_SPECIFIC
} RegisterHint;

bool has_register_hint(const Symbol *symbol);
RegisterHint get_register_hint(const Symbol *symbol);

// Error reporting
void semantic_error(SemanticAnalyzer *analyzer, size_t line, size_t column, const char *format, ...);
void semantic_warning(SemanticAnalyzer *analyzer, size_t line, size_t column, const char *format, ...);
void print_semantic_errors(const SemanticAnalyzer *analyzer);

// Utility functions
Type *create_type(TypeKind kind);
Type *create_pointer_type(TypeKind pointer_kind, Type *element_type);
Type *create_function_type(Type **param_types, size_t param_count, Type *return_type);
void free_type(Type *type);
Type *clone_type(const Type *type);
const char *type_to_string(const Type *type);
const char *symbol_kind_to_string(SymbolKind kind);

// Semantic analyzer lifecycle
SemanticAnalyzer *create_semantic_analyzer(void);
void free_semantic_analyzer(SemanticAnalyzer *analyzer);
void reset_semantic_analyzer(SemanticAnalyzer *analyzer);

#endif // FCX_SEMANTIC_H
