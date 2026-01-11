// For strdup on some systems
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Symbol table implementation

SymbolTable *create_symbol_table(void) {
    SymbolTable *table = malloc(sizeof(SymbolTable));
    if (!table) return NULL;
    
    // Create global scope
    Scope *global = malloc(sizeof(Scope));
    if (!global) {
        free(table);
        return NULL;
    }
    
    global->symbols = NULL;
    global->symbol_count = 0;
    global->symbol_capacity = 0;
    global->parent = NULL;
    global->depth = 0;
    
    table->global_scope = global;
    table->current_scope = global;
    table->scope_depth = 0;
    
    return table;
}

void free_scope(Scope *scope) {
    if (!scope) return;
    
    for (size_t i = 0; i < scope->symbol_count; i++) {
        if (scope->symbols[i]) {
            free(scope->symbols[i]->name);
            free_type(scope->symbols[i]->type);
            free(scope->symbols[i]);
        }
    }
    free(scope->symbols);
    free(scope);
}

void free_symbol_table(SymbolTable *table) {
    if (!table) return;
    
    // Free all scopes
    Scope *scope = table->current_scope;
    while (scope) {
        Scope *parent = scope->parent;
        free_scope(scope);
        scope = parent;
    }
    
    free(table);
}

void enter_scope(SymbolTable *table) {
    if (!table) return;
    
    Scope *new_scope = malloc(sizeof(Scope));
    if (!new_scope) return;
    
    new_scope->symbols = NULL;
    new_scope->symbol_count = 0;
    new_scope->symbol_capacity = 0;
    new_scope->parent = table->current_scope;
    new_scope->depth = table->scope_depth + 1;
    
    table->current_scope = new_scope;
    table->scope_depth++;
}

void exit_scope(SymbolTable *table) {
    if (!table || !table->current_scope || !table->current_scope->parent) {
        return;
    }
    
    Scope *old_scope = table->current_scope;
    table->current_scope = old_scope->parent;
    table->scope_depth--;
    
    free_scope(old_scope);
}

Symbol *declare_symbol(SymbolTable *table, const char *name, SymbolKind kind, Type *type, size_t line, size_t column) {
    if (!table || !name) return NULL;
    
    Scope *scope = table->current_scope;
    
    // Check if symbol already exists in current scope
    if (symbol_exists_in_current_scope(table, name)) {
        return NULL;
    }
    
    // Expand symbol array if needed
    if (scope->symbol_count >= scope->symbol_capacity) {
        size_t new_capacity = scope->symbol_capacity ? scope->symbol_capacity * 2 : 8;
        Symbol **new_symbols = realloc(scope->symbols, new_capacity * sizeof(Symbol*));
        if (!new_symbols) return NULL;
        scope->symbols = new_symbols;
        scope->symbol_capacity = new_capacity;
    }
    
    // Create new symbol
    Symbol *symbol = malloc(sizeof(Symbol));
    if (!symbol) return NULL;
    
    symbol->name = strdup(name);
    symbol->kind = kind;
    symbol->type = type;
    symbol->is_initialized = false;
    symbol->is_mutable = (kind == SYMBOL_VARIABLE);
    symbol->scope_depth = scope->depth;
    symbol->params = NULL;
    symbol->param_count = 0;
    symbol->return_type = NULL;
    symbol->line = line;
    symbol->column = column;
    
    scope->symbols[scope->symbol_count++] = symbol;
    
    return symbol;
}

Symbol *semantic_lookup_symbol_in_scope(Scope *scope, const char *name) {
    if (!scope || !name) return NULL;
    
    for (size_t i = 0; i < scope->symbol_count; i++) {
        if (scope->symbols[i] && strcmp(scope->symbols[i]->name, name) == 0) {
            return scope->symbols[i];
        }
    }
    
    return NULL;
}

Symbol *semantic_lookup_symbol(SymbolTable *table, const char *name) {
    if (!table || !name) return NULL;
    
    // Search from current scope up to global scope
    Scope *scope = table->current_scope;
    while (scope) {
        Symbol *symbol = semantic_lookup_symbol_in_scope(scope, name);
        if (symbol) return symbol;
        scope = scope->parent;
    }
    
    return NULL;
}

bool symbol_exists_in_current_scope(SymbolTable *table, const char *name) {
    if (!table || !name) return false;
    return semantic_lookup_symbol_in_scope(table->current_scope, name) != NULL;
}

// Type operations

Type *create_type(TypeKind kind) {
    Type *type = malloc(sizeof(Type));
    if (!type) return NULL;
    
    type->kind = kind;
    memset(&type->data, 0, sizeof(type->data));
    
    return type;
}

Type *create_pointer_type(TypeKind pointer_kind, Type *element_type) {
    if (pointer_kind != TYPE_PTR && pointer_kind != TYPE_RAWPTR && pointer_kind != TYPE_BYTEPTR) {
        return NULL;
    }
    
    Type *type = create_type(pointer_kind);
    if (!type) return NULL;
    
    if (pointer_kind == TYPE_PTR) {
        type->data.element_type = element_type;
    } else {
        type->data.element_type = NULL; // rawptr and byteptr don't have element types
    }
    
    return type;
}

Type *create_function_type(Type **param_types, size_t param_count, Type *return_type) {
    Type *type = create_type(TYPE_FUNCTION);
    if (!type) return NULL;
    
    type->data.function.param_types = param_types;
    type->data.function.param_count = param_count;
    type->data.function.return_type = return_type;
    
    return type;
}

void free_type(Type *type) {
    if (!type) return;
    
    switch (type->kind) {
        case TYPE_PTR:
            free_type(type->data.element_type);
            break;
        case TYPE_FUNCTION:
            for (size_t i = 0; i < type->data.function.param_count; i++) {
                free_type(type->data.function.param_types[i]);
            }
            free(type->data.function.param_types);
            free_type(type->data.function.return_type);
            break;
        case TYPE_ARRAY:
            free_type(type->data.array.element_type);
            break;
        case TYPE_STRUCT:
            // TODO: Free struct fields
            break;
        default:
            break;
    }
    
    free(type);
}

Type *clone_type(const Type *type) {
    if (!type) return NULL;
    
    Type *cloned = create_type(type->kind);
    if (!cloned) return NULL;
    
    switch (type->kind) {
        case TYPE_PTR:
            cloned->data.element_type = clone_type(type->data.element_type);
            break;
        case TYPE_FUNCTION:
            // TODO: Clone function type properly
            break;
        case TYPE_ARRAY:
            cloned->data.array.element_type = clone_type(type->data.array.element_type);
            cloned->data.array.size = type->data.array.size;
            break;
        default:
            break;
    }
    
    return cloned;
}

bool types_equal(const Type *a, const Type *b) {
    if (!a || !b) return false;
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TYPE_PTR:
            return types_equal(a->data.element_type, b->data.element_type);
        case TYPE_RAWPTR:
        case TYPE_BYTEPTR:
            return true; // rawptr and byteptr are equal if kinds match
        case TYPE_FUNCTION:
            if (a->data.function.param_count != b->data.function.param_count) return false;
            for (size_t i = 0; i < a->data.function.param_count; i++) {
                if (!types_equal(a->data.function.param_types[i], b->data.function.param_types[i])) {
                    return false;
                }
            }
            return types_equal(a->data.function.return_type, b->data.function.return_type);
        case TYPE_ARRAY:
            return a->data.array.size == b->data.array.size &&
                   types_equal(a->data.array.element_type, b->data.array.element_type);
        default:
            return true; // Primitive types are equal if kinds match
    }
}

bool types_compatible(const Type *a, const Type *b) {
    if (types_equal(a, b)) return true;
    
    // Integer promotion rules
    if ((a->kind >= TYPE_I8 && a->kind <= TYPE_U64) &&
        (b->kind >= TYPE_I8 && b->kind <= TYPE_U64)) {
        return true; // All integer types are compatible
    }
    
    // Float types are compatible with each other
    if ((a->kind == TYPE_F32 || a->kind == TYPE_F64) &&
        (b->kind == TYPE_F32 || b->kind == TYPE_F64)) {
        return true;
    }
    
    return false;
}

TypeConversionRule get_conversion_rule(const Type *from, const Type *to) {
    if (types_equal(from, to)) return TYPE_CONV_IDENTICAL;
    
    // Integer to integer conversions
    if ((from->kind >= TYPE_I8 && from->kind <= TYPE_U64) &&
        (to->kind >= TYPE_I8 && to->kind <= TYPE_U64)) {
        return TYPE_CONV_IMPLICIT;
    }
    
    // Float to float conversions
    if ((from->kind == TYPE_F32 || from->kind == TYPE_F64) &&
        (to->kind == TYPE_F32 || to->kind == TYPE_F64)) {
        return TYPE_CONV_IMPLICIT;
    }
    
    // Pointer conversions require explicit casts
    if (is_pointer_type(from) && is_pointer_type(to)) {
        return TYPE_CONV_EXPLICIT_CAST;
    }
    
    return TYPE_CONV_INCOMPATIBLE;
}

bool can_convert_type(const Type *from, const Type *to) {
    TypeConversionRule rule = get_conversion_rule(from, to);
    return rule != TYPE_CONV_INCOMPATIBLE;
}

bool type_requires_explicit_cast(const Type *from, const Type *to) {
    return get_conversion_rule(from, to) == TYPE_CONV_EXPLICIT_CAST;
}

// Three-pointer system type checking

bool is_pointer_type(const Type *type) {
    return type && (type->kind == TYPE_PTR || type->kind == TYPE_RAWPTR || type->kind == TYPE_BYTEPTR);
}

bool is_typed_pointer(const Type *type) {
    return type && type->kind == TYPE_PTR;
}

bool is_raw_pointer(const Type *type) {
    return type && type->kind == TYPE_RAWPTR;
}

bool is_byte_pointer(const Type *type) {
    return type && type->kind == TYPE_BYTEPTR;
}

PointerArithmeticRule get_pointer_arithmetic_rule(const Type *ptr_type) {
    if (!is_pointer_type(ptr_type)) {
        return PTR_ARITH_TYPE_ERROR;
    }
    
    if (is_typed_pointer(ptr_type)) {
        return PTR_ARITH_ALLOWED; // ptr<T> - scaled by sizeof(T)
    } else if (is_byte_pointer(ptr_type)) {
        return PTR_ARITH_BYTE_WISE; // byteptr - 1-byte arithmetic
    } else if (is_raw_pointer(ptr_type)) {
        return PTR_ARITH_FORBIDDEN; // rawptr - no arithmetic
    }
    
    return PTR_ARITH_TYPE_ERROR;
}

bool can_dereference_pointer_type(const Type *ptr_type) {
    if (!is_pointer_type(ptr_type)) return false;
    
    // ptr<T> and byteptr can be dereferenced
    // rawptr cannot be dereferenced (must cast first)
    return is_typed_pointer(ptr_type) || is_byte_pointer(ptr_type);
}

// Pointer arithmetic enforcement

bool check_pointer_arithmetic(SemanticAnalyzer *analyzer, Type *ptr_type, TokenKind op, Expr *expr) {
    PointerArithmeticRule rule = get_pointer_arithmetic_rule(ptr_type);
    
    if (rule == PTR_ARITH_FORBIDDEN) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Arithmetic on rawptr is forbidden - cast to byteptr or ptr<T> first");
        return false;
    }
    
    if (rule == PTR_ARITH_TYPE_ERROR) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Cannot perform arithmetic on non-pointer type");
        return false;
    }
    
    // Check if operation is valid for pointer type
    switch (op) {
        case OP_ADD_ASSIGN:
        case OP_SUB_ASSIGN:
        case OP_SAT_ADD:
        case OP_SAT_SUB:
        case OP_WRAP_ADD:
        case OP_WRAP_SUB:
        case OP_CHECKED_ADD:
        case OP_CHECKED_SUB:
            return true;
        default:
            semantic_error(analyzer, expr->line, expr->column,
                          "Invalid arithmetic operation on pointer type");
            return false;
    }
}

bool check_pointer_assignment(SemanticAnalyzer *analyzer, Type *target_type, Type *value_type, Expr *expr) {
    if (!is_pointer_type(target_type)) return true;
    
    // Pointer assignments require explicit casts unless types are identical
    if (!types_equal(target_type, value_type)) {
        if (is_pointer_type(value_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Pointer assignment requires explicit cast - use :> operator");
            return false;
        }
    }
    
    return true;
}

Type *get_pointer_arithmetic_result_type(Type *ptr_type, TokenKind op, Type *operand_type) {
    (void)operand_type; // Unused for now
    
    switch (op) {
        case OP_ADD_ASSIGN:
        case OP_SUB_ASSIGN:
        case OP_SAT_ADD:
        case OP_SAT_SUB:
        case OP_WRAP_ADD:
        case OP_WRAP_SUB:
        case OP_CHECKED_ADD:
        case OP_CHECKED_SUB:
            // Pointer arithmetic returns same pointer type
            return clone_type(ptr_type);
        default:
            return NULL;
    }
}

// Pointer conversion and casting

bool check_pointer_cast(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr) {
    if (!is_pointer_type(from_type) || !is_pointer_type(to_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Cast operator :> requires pointer types");
        return false;
    }
    
    // All pointer-to-pointer casts are allowed with explicit cast operator
    return true;
}

Type *apply_pointer_cast(Type *from_type, Type *to_type) {
    (void)from_type; // Unused
    return clone_type(to_type);
}

bool inject_pointer_cast_if_needed(SemanticAnalyzer *analyzer, Expr **expr, Type *target_type) {
    if (!expr || !*expr || !target_type) return false;
    
    Type *expr_type = analyze_expression(analyzer, *expr);
    if (!expr_type) return false;
    
    // If types match, no cast needed
    if (types_equal(expr_type, target_type)) {
        return true;
    }
    
    // If both are pointers and conversion requires explicit cast, inject it
    if (is_pointer_type(expr_type) && is_pointer_type(target_type)) {
        if (requires_explicit_cast(expr_type, target_type)) {
            // TODO: Create cast expression node
            return true;
        }
    }
    
    return true;
}

// Advanced pointer casting operators implementation

bool check_cast_to_operator(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr) {
    if (!analyzer || !from_type || !to_type || !expr) return false;
    
    // :> operator allows casting between pointer types
    if (!is_pointer_type(from_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Cast-to operator :> requires pointer source type, got '%s'",
                      type_to_string(from_type));
        return false;
    }
    
    if (!is_pointer_type(to_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Cast-to operator :> requires pointer target type, got '%s'",
                      type_to_string(to_type));
        return false;
    }
    
    // All pointer-to-pointer casts are allowed with :> operator
    // ptr<T> :> rawptr - allowed
    // rawptr :> ptr<T> - allowed (unsafe but explicit)
    // ptr<T> :> byteptr - allowed
    // byteptr :> ptr<T> - allowed
    
    return true;
}

bool check_reinterpret_cast_operator(SemanticAnalyzer *analyzer, Type *from_type, Type *to_type, Expr *expr) {
    if (!analyzer || !from_type || !to_type || !expr) return false;
    
    // :>: operator is high-risk reinterpret cast between different typed pointers
    if (!is_typed_pointer(from_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Reinterpret cast :>: requires typed pointer source (ptr<T>), got '%s'",
                      type_to_string(from_type));
        return false;
    }
    
    if (!is_typed_pointer(to_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Reinterpret cast :>: requires typed pointer target (ptr<T>), got '%s'",
                      type_to_string(to_type));
        return false;
    }
    
    // Warn about dangerous reinterpret cast
    semantic_warning(analyzer, expr->line, expr->column,
                    "Reinterpret cast :>: is unsafe - type punning from '%s' to '%s'",
                    type_to_string(from_type), type_to_string(to_type));
    
    return true;
}

bool check_ptr_to_int_cast(SemanticAnalyzer *analyzer, Type *ptr_type, Expr *expr) {
    if (!analyzer || !ptr_type || !expr) return false;
    
    // <|> operator casts any pointer to integer (u64)
    if (!is_pointer_type(ptr_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Pointer-to-integer cast <|> requires pointer operand, got '%s'",
                      type_to_string(ptr_type));
        return false;
    }
    
    // All pointer types can be cast to integer
    return true;
}

bool check_int_to_ptr_cast(SemanticAnalyzer *analyzer, Type *int_type, Type *target_ptr_type, Expr *expr) {
    if (!analyzer || !int_type || !target_ptr_type || !expr) return false;
    
    // |<> operator casts integer to pointer type
    if (!(int_type->kind >= TYPE_I8 && int_type->kind <= TYPE_U64)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Integer-to-pointer cast |<> requires integer operand, got '%s'",
                      type_to_string(int_type));
        return false;
    }
    
    if (!is_pointer_type(target_ptr_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Integer-to-pointer cast |<> requires pointer target type, got '%s'",
                      type_to_string(target_ptr_type));
        return false;
    }
    
    // Warn about potentially unsafe integer-to-pointer cast
    semantic_warning(analyzer, expr->line, expr->column,
                    "Integer-to-pointer cast |<> is unsafe - ensure address is valid");
    
    return true;
}

// Syscall pointer conversion detection and injection

bool detect_syscall_rawptr_requirement(SemanticAnalyzer *analyzer, Expr *syscall_expr) {
    if (!analyzer || !syscall_expr || syscall_expr->type != EXPR_SYSCALL_OP) {
        return false;
    }
    
    // Check each syscall argument for pointer types
    bool needs_conversion = false;
    
    for (size_t i = 0; i < syscall_expr->data.syscall_op.arg_count; i++) {
        Expr *arg = syscall_expr->data.syscall_op.args[i];
        Type *arg_type = analyze_expression(analyzer, arg);
        
        if (!arg_type) continue;
        
        // If argument is a pointer but not rawptr, it needs conversion
        if (is_pointer_type(arg_type) && !is_raw_pointer(arg_type)) {
            needs_conversion = true;
            
            semantic_error(analyzer, arg->line, arg->column,
                          "Syscall argument %zu: pointer type '%s' must be explicitly cast to rawptr using :> operator",
                          i + 1, type_to_string(arg_type));
        }
    }
    
    return !needs_conversion; // Return true if no conversion needed (all valid)
}

bool inject_rawptr_cast_for_syscall(SemanticAnalyzer *analyzer, Expr **arg_expr, size_t arg_index) {
    if (!analyzer || !arg_expr || !*arg_expr) return false;
    
    Type *arg_type = analyze_expression(analyzer, *arg_expr);
    if (!arg_type) return false;
    
    // Only inject cast if it's a non-raw pointer
    if (is_pointer_type(arg_type) && !is_raw_pointer(arg_type)) {
        // In a full implementation, we would create a cast expression node here
        // For now, we just validate that the cast is needed
        semantic_error(analyzer, (*arg_expr)->line, (*arg_expr)->column,
                      "Syscall argument %zu requires explicit cast to rawptr - use :> operator",
                      arg_index + 1);
        return false;
    }
    
    return true;
}

// MMIO operations validation

bool validate_mmio_operation(SemanticAnalyzer *analyzer, Expr *mmio_expr, bool is_map) {
    if (!analyzer || !mmio_expr || mmio_expr->type != EXPR_MEMORY_OP) {
        return false;
    }
    
    int op = mmio_expr->data.memory_op.op;
    
    if (is_map) {
        // @> operator - map MMIO address
        if (op != MEM_MMIO_MAP) {
            return false;
        }
        
        // First operand should be an integer (physical address)
        if (mmio_expr->data.memory_op.operand_count > 0) {
            Type *addr_type = analyze_expression(analyzer, mmio_expr->data.memory_op.operands[0]);
            if (addr_type && !(addr_type->kind >= TYPE_I8 && addr_type->kind <= TYPE_U64)) {
                semantic_error(analyzer, mmio_expr->line, mmio_expr->column,
                              "MMIO map operator @> requires integer address operand, got '%s'",
                              type_to_string(addr_type));
                return false;
            }
        }
        
        // MMIO map returns rawptr
        return true;
    } else {
        // <@ operator - unmap MMIO address
        if (op != MEM_MMIO_UNMAP) {
            return false;
        }
        
        // First operand should be rawptr
        if (mmio_expr->data.memory_op.operand_count > 0) {
            Type *ptr_type = analyze_expression(analyzer, mmio_expr->data.memory_op.operands[0]);
            if (ptr_type && !is_raw_pointer(ptr_type)) {
                semantic_error(analyzer, mmio_expr->line, mmio_expr->column,
                              "MMIO unmap operator <@ requires rawptr operand, got '%s'",
                              type_to_string(ptr_type));
                return false;
            }
        }
        
        return true;
    }
}

bool check_mmio_volatile_semantics(SemanticAnalyzer *analyzer, Type *ptr_type, Expr *expr) {
    if (!analyzer || !ptr_type || !expr) return false;
    
    // MMIO operations should use rawptr with volatile semantics
    // This is enforced through the >< operator for volatile access
    
    if (!is_raw_pointer(ptr_type)) {
        semantic_warning(analyzer, expr->line, expr->column,
                        "MMIO operations should use rawptr for hardware access");
        return false;
    }
    
    // In a full implementation, we would check if the pointer has volatile flag set
    // For now, we just validate the pointer type
    
    return true;
}

// Field access operations

Type *analyze_field_access_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr) return NULL;
    
    // . operator - traditional field access
    // ptr.field or value.field
    
    // For now, return a placeholder type
    // In a full implementation, we would:
    // 1. Analyze the base expression (ptr or value)
    // 2. Check if it's a struct type
    // 3. Look up the field in the struct definition
    // 4. Return the field's type
    
    semantic_error(analyzer, expr->line, expr->column,
                  "Field access operator . not yet fully implemented");
    
    return NULL;
}

Type *analyze_compact_field_access_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr) return NULL;
    
    // : operator - compact field access
    // ptr:field (equivalent to ptr.field)
    
    // For now, return a placeholder type
    // In a full implementation, this would be identical to analyze_field_access_expr
    
    semantic_error(analyzer, expr->line, expr->column,
                  "Compact field access operator : not yet fully implemented");
    
    return NULL;
}

Type *analyze_layout_offset_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr) return NULL;
    
    // ->> operator - layout offset access (compile-time)
    // ptr ->> field returns pointer to field without dereferencing
    
    // This operator performs compile-time offset calculation
    // Returns (ptr as byteptr) + offsetof(T, field) as ptr<FieldType>
    
    // For now, return a placeholder type
    // In a full implementation, we would:
    // 1. Analyze the base pointer expression
    // 2. Get the struct type from the pointer
    // 3. Calculate the field offset at compile time
    // 4. Return a pointer to the field type
    
    semantic_error(analyzer, expr->line, expr->column,
                  "Layout offset operator ->> not yet fully implemented");
    
    return NULL;
}

Type *analyze_reverse_layout_copy_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr) return NULL;
    
    // <<- operator - reverse layout copy
    // field <<- ptr (equivalent to memcpy(&variable.field, ptr, sizeof(field)))
    
    // This is a high-level block copy operation
    
    // For now, return a placeholder type
    // In a full implementation, we would:
    // 1. Analyze the target field expression
    // 2. Analyze the source pointer expression
    // 3. Validate that the pointer points to compatible data
    // 4. Return void (no type)
    
    semantic_error(analyzer, expr->line, expr->column,
                  "Reverse layout copy operator <<- not yet fully implemented");
    
    return NULL;
}

size_t calculate_field_offset(Type *struct_type, const char *field_name) {
    if (!struct_type || struct_type->kind != TYPE_STRUCT || !field_name) {
        return 0;
    }
    
    // In a full implementation, we would:
    // 1. Iterate through struct fields
    // 2. Calculate cumulative offset based on field sizes and alignment
    // 3. Return the offset of the named field
    
    // For now, return 0 as placeholder
    return 0;
}

// Type inference

Type *infer_type_from_literal(const LiteralValue *literal) {
    if (!literal) return NULL;
    
    switch (literal->type) {
        case LIT_INTEGER:
            return create_type(TYPE_I64); // Default to i64
        case LIT_FLOAT:
            return create_type(TYPE_F64); // Default to f64
        case LIT_STRING:
            // String is ptr<u8>
            return create_pointer_type(TYPE_PTR, create_type(TYPE_U8));
        case LIT_BOOLEAN:
            return create_type(TYPE_BOOL);
        case LIT_CHARACTER:
            return create_type(TYPE_U8);
        case LIT_RAW_BYTES:
            return create_pointer_type(TYPE_BYTEPTR, NULL);
        default:
            return NULL;
    }
}

Type *infer_type_from_expression(SemanticAnalyzer *analyzer, Expr *expr) {
    return analyze_expression(analyzer, expr);
}

Type *infer_binary_operation_type(Type *left, Type *right, TokenKind op) {
    (void)op; // Unused for now
    
    // For arithmetic operations, use the "larger" type
    if (types_equal(left, right)) {
        return clone_type(left);
    }
    
    // Integer promotion
    if ((left->kind >= TYPE_I8 && left->kind <= TYPE_U64) &&
        (right->kind >= TYPE_I8 && right->kind <= TYPE_U64)) {
        // Return the larger type
        if (left->kind > right->kind) {
            return clone_type(left);
        } else {
            return clone_type(right);
        }
    }
    
    // Float promotion
    if ((left->kind == TYPE_F32 || left->kind == TYPE_F64) &&
        (right->kind == TYPE_F32 || right->kind == TYPE_F64)) {
        return create_type(TYPE_F64); // Promote to f64
    }
    
    return clone_type(left);
}

Type *infer_unary_operation_type(Type *operand, TokenKind op) {
    switch (op) {
        case OP_AT_SYMBOL: // Address-of operator (@)
            return create_pointer_type(TYPE_PTR, clone_type(operand));
        case OP_MUL_ASSIGN: // Using * for dereference (temporary)
            if (is_typed_pointer(operand)) {
                return clone_type(operand->data.element_type);
            } else if (is_byte_pointer(operand)) {
                return create_type(TYPE_U8);
            }
            return NULL;
        case OP_ATOMIC_READ: // ! can also be used for dereference
            if (is_typed_pointer(operand)) {
                return clone_type(operand->data.element_type);
            } else if (is_byte_pointer(operand)) {
                return create_type(TYPE_U8);
            }
            return NULL;
        default:
            return clone_type(operand);
    }
}

// Semantic analyzer lifecycle

SemanticAnalyzer *create_semantic_analyzer(void) {
    SemanticAnalyzer *analyzer = malloc(sizeof(SemanticAnalyzer));
    if (!analyzer) return NULL;
    
    analyzer->symbol_table = create_symbol_table();
    if (!analyzer->symbol_table) {
        free(analyzer);
        return NULL;
    }
    
    analyzer->type_context.expected_type = NULL;
    analyzer->type_context.allow_inference = true;
    analyzer->type_context.in_function = false;
    analyzer->type_context.function_return_type = NULL;
    
    analyzer->had_error = false;
    analyzer->error_messages = NULL;
    analyzer->error_count = 0;
    analyzer->error_capacity = 0;
    
    return analyzer;
}

void free_semantic_analyzer(SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    free_symbol_table(analyzer->symbol_table);
    
    for (size_t i = 0; i < analyzer->error_count; i++) {
        free(analyzer->error_messages[i]);
    }
    free(analyzer->error_messages);
    
    free(analyzer);
}

void reset_semantic_analyzer(SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    analyzer->had_error = false;
    
    for (size_t i = 0; i < analyzer->error_count; i++) {
        free(analyzer->error_messages[i]);
    }
    analyzer->error_count = 0;
}

// Error reporting

void semantic_error(SemanticAnalyzer *analyzer, size_t line, size_t column, const char *format, ...) {
    if (!analyzer) return;
    
    analyzer->had_error = true;
    
    // Expand error array if needed
    if (analyzer->error_count >= analyzer->error_capacity) {
        size_t new_capacity = analyzer->error_capacity ? analyzer->error_capacity * 2 : 8;
        char **new_errors = realloc(analyzer->error_messages, new_capacity * sizeof(char*));
        if (!new_errors) return;
        analyzer->error_messages = new_errors;
        analyzer->error_capacity = new_capacity;
    }
    
    // Format error message
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Create full error message with location
    char full_message[1200];
    snprintf(full_message, sizeof(full_message), "[Line %zu, Column %zu] Error: %s", line, column, buffer);
    
    analyzer->error_messages[analyzer->error_count++] = strdup(full_message);
}

void semantic_warning(SemanticAnalyzer *analyzer, size_t line, size_t column, const char *format, ...) {
    if (!analyzer) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    printf("[Line %zu, Column %zu] Warning: %s\n", line, column, buffer);
}

void print_semantic_errors(const SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    for (size_t i = 0; i < analyzer->error_count; i++) {
        printf("%s\n", analyzer->error_messages[i]);
    }
}

// Utility functions

const char *type_to_string(const Type *type) {
    if (!type) return "unknown";
    
    switch (type->kind) {
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8: return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_BYTE: return "byte";
        case TYPE_PTR: return "ptr<T>";
        case TYPE_RAWPTR: return "rawptr";
        case TYPE_BYTEPTR: return "byteptr";
        case TYPE_ARRAY: return "array";
        case TYPE_STRUCT: return "struct";
        case TYPE_FUNCTION: return "function";
        default: return "unknown";
    }
}

const char *symbol_kind_to_string(SymbolKind kind) {
    switch (kind) {
        case SYMBOL_VARIABLE: return "variable";
        case SYMBOL_CONSTANT: return "constant";
        case SYMBOL_FUNCTION: return "function";
        case SYMBOL_PARAMETER: return "parameter";
        case SYMBOL_TYPE: return "type";
        default: return "unknown";
    }
}

// Register hints (stubs for now)

bool has_register_hint(const Symbol *symbol) {
    (void)symbol;
    return false;
}

RegisterHint get_register_hint(const Symbol *symbol) {
    (void)symbol;
    return REG_HINT_NONE;
}

// Statement analysis implementations

bool analyze_program(SemanticAnalyzer *analyzer, Stmt **statements, size_t count) {
    if (!analyzer || !statements) return false;
    
    for (size_t i = 0; i < count; i++) {
        if (!analyze_statement(analyzer, statements[i])) {
            return false;
        }
    }
    
    return !analyzer->had_error;
}

bool analyze_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt) return false;
    
    switch (stmt->type) {
        case STMT_LET:
            return analyze_let_statement(analyzer, stmt);
        case STMT_FUNCTION:
            return analyze_function_statement(analyzer, stmt);
        case STMT_IF:
            return analyze_if_statement(analyzer, stmt);
        case STMT_LOOP:
            return analyze_loop_statement(analyzer, stmt);
        case STMT_RETURN:
            return analyze_return_statement(analyzer, stmt);
        case STMT_EXPRESSION:
            return analyze_expression_statement(analyzer, stmt);
        case STMT_HALT:
            // Halt is similar to return
            return analyze_return_statement(analyzer, stmt);
        default:
            semantic_error(analyzer, stmt->line, stmt->column, "Unknown statement type");
            return false;
    }
}

bool analyze_let_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt || stmt->type != STMT_LET) return false;
    
    const char *name = stmt->data.let.name;
    Type *declared_type = stmt->data.let.type_annotation;
    Expr *initializer = stmt->data.let.initializer;
    bool is_const = stmt->data.let.is_const;
    
    // Check if symbol already exists in current scope
    if (symbol_exists_in_current_scope(analyzer->symbol_table, name)) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Symbol '%s' already declared in this scope", name);
        return false;
    }
    
    Type *inferred_type = NULL;
    
    // Type inference with := operator
    if (initializer) {
        inferred_type = analyze_expression(analyzer, initializer);
        if (!inferred_type) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Cannot infer type for variable '%s'", name);
            return false;
        }
    }
    
    // Determine final type
    Type *final_type = NULL;
    if (declared_type && inferred_type) {
        // Both type annotation and initializer present
        if (!types_compatible(declared_type, inferred_type)) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Type mismatch: declared type '%s' incompatible with inferred type '%s'",
                          type_to_string(declared_type), type_to_string(inferred_type));
            return false;
        }
        final_type = declared_type;
    } else if (declared_type) {
        final_type = declared_type;
    } else if (inferred_type) {
        final_type = inferred_type;
    } else {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Variable '%s' requires either type annotation or initializer", name);
        return false;
    }
    
    // Declare symbol
    SymbolKind kind = is_const ? SYMBOL_CONSTANT : SYMBOL_VARIABLE;
    Symbol *symbol = declare_symbol(analyzer->symbol_table, name, kind, clone_type(final_type),
                                   stmt->line, stmt->column);
    
    if (!symbol) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Failed to declare symbol '%s'", name);
        return false;
    }
    
    symbol->is_initialized = (initializer != NULL);
    
    return true;
}

bool analyze_function_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt || stmt->type != STMT_FUNCTION) return false;
    
    const char *name = stmt->data.function.name;
    Parameter *params = stmt->data.function.params;
    size_t param_count = stmt->data.function.param_count;
    Type *return_type = stmt->data.function.return_type;
    
    // Check if function already exists
    if (symbol_exists_in_current_scope(analyzer->symbol_table, name)) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Function '%s' already declared in this scope", name);
        return false;
    }
    
    // Create function type
    Type **param_types = malloc(param_count * sizeof(Type*));
    for (size_t i = 0; i < param_count; i++) {
        param_types[i] = clone_type(params[i].type);
    }
    
    Type *func_type = create_function_type(param_types, param_count,
                                          return_type ? clone_type(return_type) : NULL);
    
    // Declare function symbol
    Symbol *func_symbol = declare_symbol(analyzer->symbol_table, name, SYMBOL_FUNCTION,
                                        func_type, stmt->line, stmt->column);
    
    if (!func_symbol) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Failed to declare function '%s'", name);
        return false;
    }
    
    func_symbol->params = params;
    func_symbol->param_count = param_count;
    func_symbol->return_type = return_type;
    
    // Enter function scope
    enter_scope(analyzer->symbol_table);
    
    // Set function context
    bool prev_in_function = analyzer->type_context.in_function;
    Type *prev_return_type = analyzer->type_context.function_return_type;
    analyzer->type_context.in_function = true;
    analyzer->type_context.function_return_type = return_type;
    
    // Declare parameters in function scope
    for (size_t i = 0; i < param_count; i++) {
        Symbol *param_symbol = declare_symbol(analyzer->symbol_table, params[i].name,
                                             SYMBOL_PARAMETER, clone_type(params[i].type),
                                             stmt->line, stmt->column);
        if (!param_symbol) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Failed to declare parameter '%s'", params[i].name);
            exit_scope(analyzer->symbol_table);
            return false;
        }
        param_symbol->is_initialized = true;
    }
    
    // Analyze function body
    Block *body = &stmt->data.function.body;
    for (size_t i = 0; i < body->count; i++) {
        if (!analyze_statement(analyzer, body->statements[i])) {
            exit_scope(analyzer->symbol_table);
            return false;
        }
    }
    
    // Restore context
    analyzer->type_context.in_function = prev_in_function;
    analyzer->type_context.function_return_type = prev_return_type;
    
    // Exit function scope
    exit_scope(analyzer->symbol_table);
    
    return true;
}

bool analyze_if_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt || stmt->type != STMT_IF) return false;
    
    // Analyze condition
    Type *cond_type = analyze_expression(analyzer, stmt->data.if_stmt.condition);
    if (!cond_type) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Invalid condition in if statement");
        return false;
    }
    
    // Condition should be boolean or integer
    if (cond_type->kind != TYPE_BOOL &&
        !(cond_type->kind >= TYPE_I8 && cond_type->kind <= TYPE_U64)) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Condition must be boolean or integer type");
        return false;
    }
    
    // Analyze then branch
    enter_scope(analyzer->symbol_table);
    Block *then_branch = &stmt->data.if_stmt.then_branch;
    for (size_t i = 0; i < then_branch->count; i++) {
        if (!analyze_statement(analyzer, then_branch->statements[i])) {
            exit_scope(analyzer->symbol_table);
            return false;
        }
    }
    exit_scope(analyzer->symbol_table);
    
    // Analyze else branch if present
    Block *else_branch = &stmt->data.if_stmt.else_branch;
    if (else_branch->count > 0) {
        enter_scope(analyzer->symbol_table);
        for (size_t i = 0; i < else_branch->count; i++) {
            if (!analyze_statement(analyzer, else_branch->statements[i])) {
                exit_scope(analyzer->symbol_table);
                return false;
            }
        }
        exit_scope(analyzer->symbol_table);
    }
    
    return true;
}

bool analyze_loop_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt || stmt->type != STMT_LOOP) return false;
    
    // Analyze condition if present
    if (stmt->data.loop.condition) {
        Type *cond_type = analyze_expression(analyzer, stmt->data.loop.condition);
        if (!cond_type) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Invalid loop condition");
            return false;
        }
    }
    
    // Analyze loop body
    enter_scope(analyzer->symbol_table);
    Block *body = &stmt->data.loop.body;
    for (size_t i = 0; i < body->count; i++) {
        if (!analyze_statement(analyzer, body->statements[i])) {
            exit_scope(analyzer->symbol_table);
            return false;
        }
    }
    exit_scope(analyzer->symbol_table);
    
    return true;
}

bool analyze_return_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt) return false;
    
    if (!analyzer->type_context.in_function) {
        semantic_error(analyzer, stmt->line, stmt->column,
                      "Return statement outside of function");
        return false;
    }
    
    Expr *return_value = stmt->data.return_value;
    Type *expected_return_type = analyzer->type_context.function_return_type;
    
    if (return_value) {
        Type *actual_type = analyze_expression(analyzer, return_value);
        if (!actual_type) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Invalid return expression");
            return false;
        }
        
        if (expected_return_type) {
            if (!types_compatible(actual_type, expected_return_type)) {
                semantic_error(analyzer, stmt->line, stmt->column,
                              "Return type mismatch: expected '%s', got '%s'",
                              type_to_string(expected_return_type),
                              type_to_string(actual_type));
                return false;
            }
        }
    } else {
        // No return value - function should return void or have no return type
        if (expected_return_type) {
            semantic_error(analyzer, stmt->line, stmt->column,
                          "Function expects return value of type '%s'",
                          type_to_string(expected_return_type));
            return false;
        }
    }
    
    return true;
}

bool analyze_expression_statement(SemanticAnalyzer *analyzer, Stmt *stmt) {
    if (!analyzer || !stmt || stmt->type != STMT_EXPRESSION) return false;
    
    Type *expr_type = analyze_expression(analyzer, stmt->data.expression);
    return expr_type != NULL;
}

// Expression analysis implementations

Type *analyze_expression(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr) return NULL;
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return analyze_literal_expr(analyzer, expr);
        case EXPR_IDENTIFIER:
            return analyze_identifier_expr(analyzer, expr);
        case EXPR_BINARY:
            return analyze_binary_expr(analyzer, expr);
        case EXPR_UNARY:
            return analyze_unary_expr(analyzer, expr);
        case EXPR_ASSIGNMENT:
            return analyze_assignment_expr(analyzer, expr);
        case EXPR_CALL:
            return analyze_call_expr(analyzer, expr);
        case EXPR_MEMORY_OP:
            return analyze_memory_op_expr(analyzer, expr);
        case EXPR_ATOMIC_OP:
            return analyze_atomic_op_expr(analyzer, expr);
        case EXPR_SYSCALL_OP:
            return analyze_syscall_op_expr(analyzer, expr);
        default:
            semantic_error(analyzer, expr->line, expr->column, "Unknown expression type");
            return NULL;
    }
}

Type *analyze_literal_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_LITERAL) return NULL;
    
    return infer_type_from_literal(&expr->data.literal);
}

Type *analyze_identifier_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_IDENTIFIER) return NULL;
    
    const char *name = expr->data.identifier;
    Symbol *symbol = semantic_lookup_symbol(analyzer->symbol_table, name);
    
    if (!symbol) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Undefined identifier '%s'", name);
        return NULL;
    }
    
    if (!symbol->is_initialized && symbol->kind == SYMBOL_VARIABLE) {
        semantic_warning(analyzer, expr->line, expr->column,
                        "Variable '%s' may not be initialized", name);
    }
    
    return clone_type(symbol->type);
}

Type *analyze_binary_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_BINARY) return NULL;
    
    Type *left_type = analyze_expression(analyzer, expr->data.binary.left);
    Type *right_type = analyze_expression(analyzer, expr->data.binary.right);
    
    if (!left_type || !right_type) {
        return NULL;
    }
    
    TokenKind op = expr->data.binary.op;
    
    // Handle pointer casting operators
    switch (op) {
        case OP_CAST_TO: // :> operator
            if (!check_cast_to_operator(analyzer, left_type, right_type, expr)) {
                return NULL;
            }
            return apply_pointer_cast(left_type, right_type);
            
        case OP_REINTERPRET_CAST: // :>: operator
            if (!check_reinterpret_cast_operator(analyzer, left_type, right_type, expr)) {
                return NULL;
            }
            return apply_pointer_cast(left_type, right_type);
            
        case OP_PTR_TO_INT: // <|> operator
            if (!check_ptr_to_int_cast(analyzer, left_type, expr)) {
                return NULL;
            }
            return create_type(TYPE_U64); // Returns u64
            
        case OP_INT_TO_PTR: // |<> operator
            if (!check_int_to_ptr_cast(analyzer, left_type, right_type, expr)) {
                return NULL;
            }
            return clone_type(right_type); // Returns target pointer type
            
        default:
            break;
    }
    
    // Validate operator usage against registry
    if (!validate_operator_usage(analyzer, op, left_type, right_type, NULL, expr)) {
        return NULL;
    }
    
    // Check pointer arithmetic
    if (is_pointer_type(left_type)) {
        if (!check_pointer_arithmetic(analyzer, left_type, op, expr)) {
            return NULL;
        }
        return get_pointer_arithmetic_result_type(left_type, op, right_type);
    }
    
    // Check type compatibility
    if (!types_compatible(left_type, right_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Type mismatch in binary operation: '%s' and '%s'",
                      type_to_string(left_type), type_to_string(right_type));
        return NULL;
    }
    
    return infer_binary_operation_type(left_type, right_type, op);
}

Type *analyze_unary_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_UNARY) return NULL;
    
    Type *operand_type = analyze_expression(analyzer, expr->data.unary.operand);
    if (!operand_type) {
        return NULL;
    }
    
    TokenKind op = expr->data.unary.op;
    
    // Check dereference operator (using ! for atomic read/dereference)
    if (op == OP_ATOMIC_READ || op == OP_MUL_ASSIGN) {
        if (!can_dereference_pointer_type(operand_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Cannot dereference type '%s' - rawptr must be cast first",
                          type_to_string(operand_type));
            return NULL;
        }
    }
    
    return infer_unary_operation_type(operand_type, op);
}

Type *analyze_assignment_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_ASSIGNMENT) return NULL;
    
    // Analyze target
    Type *target_type = analyze_expression(analyzer, expr->data.assignment.target);
    if (!target_type) {
        return NULL;
    }
    
    // Check if target is mutable
    if (expr->data.assignment.target->type == EXPR_IDENTIFIER) {
        const char *name = expr->data.assignment.target->data.identifier;
        Symbol *symbol = semantic_lookup_symbol(analyzer->symbol_table, name);
        if (symbol && !symbol->is_mutable) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Cannot assign to constant '%s'", name);
            return NULL;
        }
    }
    
    // Analyze value
    Type *value_type = analyze_expression(analyzer, expr->data.assignment.value);
    if (!value_type) {
        return NULL;
    }
    
    // Check pointer assignment
    if (!check_pointer_assignment(analyzer, target_type, value_type, expr)) {
        return NULL;
    }
    
    // Check type compatibility
    if (!types_compatible(target_type, value_type)) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Type mismatch in assignment: cannot assign '%s' to '%s'",
                      type_to_string(value_type), type_to_string(target_type));
        return NULL;
    }
    
    return clone_type(target_type);
}

Type *analyze_call_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_CALL) return NULL;
    
    // Analyze function expression
    Type *func_type = analyze_expression(analyzer, expr->data.call.function);
    if (!func_type) {
        return NULL;
    }
    
    // Check if it's a function type
    if (func_type->kind != TYPE_FUNCTION) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Expression is not callable");
        return NULL;
    }
    
    // Check argument count
    size_t expected_count = func_type->data.function.param_count;
    size_t actual_count = expr->data.call.arg_count;
    
    if (expected_count != actual_count) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Function expects %zu arguments, got %zu",
                      expected_count, actual_count);
        return NULL;
    }
    
    // Check argument types
    for (size_t i = 0; i < actual_count; i++) {
        Type *arg_type = analyze_expression(analyzer, expr->data.call.args[i]);
        if (!arg_type) {
            return NULL;
        }
        
        Type *param_type = func_type->data.function.param_types[i];
        if (!types_compatible(arg_type, param_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Argument %zu type mismatch: expected '%s', got '%s'",
                          i + 1, type_to_string(param_type), type_to_string(arg_type));
            return NULL;
        }
    }
    
    return clone_type(func_type->data.function.return_type);
}

Type *analyze_memory_op_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_MEMORY_OP) return NULL;
    
    // Validate memory operation operands
    for (size_t i = 0; i < expr->data.memory_op.operand_count; i++) {
        Type *operand_type = analyze_expression(analyzer, expr->data.memory_op.operands[i]);
        if (!operand_type) {
            return NULL;
        }
    }
    
    switch (expr->data.memory_op.op) {
        case MEM_ALLOCATE: // mem>
            // Returns ptr<T> - for now return generic pointer
            return create_pointer_type(TYPE_PTR, create_type(TYPE_U8));
            
        case MEM_DEALLOCATE: // >mem
            // Validate that operand is a pointer
            if (expr->data.memory_op.operand_count > 0) {
                Type *ptr_type = analyze_expression(analyzer, expr->data.memory_op.operands[0]);
                if (ptr_type && !is_pointer_type(ptr_type)) {
                    semantic_error(analyzer, expr->line, expr->column,
                                  "Deallocation operator >mem requires pointer operand");
                    return NULL;
                }
            }
            // Returns void (no type)
            return NULL;
            
        case MEM_STACK_ALLOC: // stack>
            // Returns rawptr
            return create_pointer_type(TYPE_RAWPTR, NULL);
            
        case MEM_MMIO_MAP: // @>
            // Validate MMIO map operation
            if (!validate_mmio_operation(analyzer, expr, true)) {
                return NULL;
            }
            // Returns rawptr for MMIO with volatile semantics
            return create_pointer_type(TYPE_RAWPTR, NULL);
            
        case MEM_MMIO_UNMAP: // <@
            // Validate MMIO unmap operation
            if (!validate_mmio_operation(analyzer, expr, false)) {
                return NULL;
            }
            // Returns void (no type)
            return NULL;
            
        case MEM_LAYOUT_ACCESS: // ->>
            // Layout offset access - returns pointer to field
            return analyze_layout_offset_expr(analyzer, expr);
            
        case MEM_ARENA_ALLOC: // arena>
            // Returns ptr<T>
            return create_pointer_type(TYPE_PTR, create_type(TYPE_U8));
            
        case MEM_SLAB_ALLOC: // slab>
            // Returns ptr<T>
            return create_pointer_type(TYPE_PTR, create_type(TYPE_U8));
            
        default:
            semantic_error(analyzer, expr->line, expr->column,
                          "Unknown memory operation");
            return NULL;
    }
}

Type *analyze_atomic_op_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_ATOMIC_OP) return NULL;
    
    // Analyze operands
    for (size_t i = 0; i < expr->data.atomic_op.operand_count; i++) {
        Type *operand_type = analyze_expression(analyzer, expr->data.atomic_op.operands[i]);
        if (!operand_type) {
            return NULL;
        }
        
        // First operand should be a pointer for atomic operations
        if (i == 0 && !is_pointer_type(operand_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Atomic operation requires pointer operand");
            return NULL;
        }
        
        // Validate that rawptr cannot be used for atomic operations
        if (i == 0 && is_raw_pointer(operand_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Atomic operations cannot use rawptr - cast to ptr<T> or byteptr first");
            return NULL;
        }
    }
    
    // Validate atomic operation arity
    switch (expr->data.atomic_op.op) {
        case ATOMIC_READ: // !
            if (expr->data.atomic_op.operand_count != 1) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Atomic read (!) requires exactly 1 operand");
                return NULL;
            }
            break;
            
        case ATOMIC_WRITE: // !!
            if (expr->data.atomic_op.operand_count != 2) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Atomic write (!!) requires exactly 2 operands");
                return NULL;
            }
            break;
            
        case ATOMIC_CAS: // <=>
            if (expr->data.atomic_op.operand_count != 3) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Compare-and-swap (<=>) requires exactly 3 operands");
                return NULL;
            }
            break;
            
        case ATOMIC_SWAP: // <==>
            if (expr->data.atomic_op.operand_count != 2) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Atomic swap (<==>)  requires exactly 2 operands");
                return NULL;
            }
            break;
            
        case ATOMIC_FETCH_ADD: // ?!!
            if (expr->data.atomic_op.operand_count != 2) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Atomic fetch-add (?!!) requires exactly 2 operands");
                return NULL;
            }
            break;
            
        default:
            semantic_error(analyzer, expr->line, expr->column,
                          "Unknown atomic operation");
            return NULL;
    }
    
    // Return type depends on operation
    switch (expr->data.atomic_op.op) {
        case ATOMIC_READ: // !
            // Returns value type
            if (expr->data.atomic_op.operand_count > 0) {
                Type *ptr_type = analyze_expression(analyzer, expr->data.atomic_op.operands[0]);
                if (is_typed_pointer(ptr_type)) {
                    return clone_type(ptr_type->data.element_type);
                } else if (is_byte_pointer(ptr_type)) {
                    return create_type(TYPE_U8);
                }
            }
            return create_type(TYPE_U64);
            
        case ATOMIC_WRITE: // !!
        case ATOMIC_SWAP: // <==>
        case ATOMIC_FETCH_ADD: // ?!!
            // Returns void
            return NULL;
            
        case ATOMIC_CAS: // <=>
            // Returns boolean for CAS
            return create_type(TYPE_BOOL);
            
        default:
            return NULL;
    }
}

Type *analyze_syscall_op_expr(SemanticAnalyzer *analyzer, Expr *expr) {
    if (!analyzer || !expr || expr->type != EXPR_SYSCALL_OP) return NULL;
    
    // Detect and validate syscall rawptr requirements
    if (!detect_syscall_rawptr_requirement(analyzer, expr)) {
        return NULL;
    }
    
    // Analyze syscall arguments
    for (size_t i = 0; i < expr->data.syscall_op.arg_count; i++) {
        Type *arg_type = analyze_expression(analyzer, expr->data.syscall_op.args[i]);
        if (!arg_type) {
            return NULL;
        }
        
        // Syscall pointer arguments MUST be rawptr (strict requirement)
        if (is_pointer_type(arg_type) && !is_raw_pointer(arg_type)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Syscall argument %zu: pointer must be cast to rawptr using :> operator", i + 1);
            return NULL;
        }
    }
    
    // Validate syscall operator arity based on operation type
    // $/ (write) and /$ (read) require 3 arguments: fd, buffer, length
    // sys% (raw syscall) requires at least 1 argument (syscall number)
    
    // Syscalls return i64 (or i32 for file descriptors)
    return create_type(TYPE_I64);
}

// Operator validation against registry

bool validate_operator_usage(SemanticAnalyzer *analyzer, TokenKind op, Type *left_type, Type *right_type, Type *third_type, Expr *expr) {
    if (!analyzer || !expr) return false;
    
    // Get operator info from registry
    const OperatorInfo *op_info = get_operator_info_by_token(op);
    if (!op_info) {
        // Operator not in registry - this is a critical error
        semantic_error(analyzer, expr->line, expr->column,
                      "Operator not found in registry");
        return false;
    }
    
    // Validate operator arity
    size_t operand_count = 1; // Start with left operand
    if (right_type) operand_count++;
    if (third_type) operand_count++;
    
    bool arity_valid = false;
    switch (op_info->arity) {
        case ARITY_UNARY:
            arity_valid = (operand_count == 1);
            break;
        case ARITY_BINARY:
            arity_valid = (operand_count == 2);
            break;
        case ARITY_TERNARY:
            arity_valid = (operand_count == 3);
            break;
        case ARITY_NARY:
            arity_valid = (operand_count >= 1);
            break;
    }
    
    if (!arity_valid) {
        semantic_error(analyzer, expr->line, expr->column,
                      "Operator '%s' arity mismatch: expected %s, got %zu operands",
                      op_info->symbol,
                      op_info->arity == ARITY_UNARY ? "unary" :
                      op_info->arity == ARITY_BINARY ? "binary" :
                      op_info->arity == ARITY_TERNARY ? "ternary" : "n-ary",
                      operand_count);
        return false;
    }
    
    // Special case: Check for bitfield operations by token kind
    // (Some bitfield tokens are reused in multiple categories)
    if (op == OP_BITFIELD_EXTRACT || op == OP_BITFIELD_INSERT || 
        op == OP_BITWISE_ROTATE_XOR || op == OP_SHIFT_MASK || op == OP_EXTRACT_RSHIFT) {
        if (left_type && !(left_type->kind >= TYPE_I8 && left_type->kind <= TYPE_U64)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Bitfield operator requires integer operands, got '%s'",
                          type_to_string(left_type));
            return false;
        }
        if (right_type && !(right_type->kind >= TYPE_I8 && right_type->kind <= TYPE_U64)) {
            semantic_error(analyzer, expr->line, expr->column,
                          "Bitfield operator requires integer operands, got '%s'",
                          type_to_string(right_type));
            return false;
        }
    }
    
    // Validate operator category-specific type requirements
    switch (op_info->category) {
        case CAT_ATOMIC_CONCUR:
            // Atomic operations require pointer operands (not rawptr)
            if (left_type && is_raw_pointer(left_type)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Atomic operator '%s' cannot use rawptr - cast to ptr<T> or byteptr first",
                              op_info->symbol);
                return false;
            }
            break;
            
        case CAT_MEMORY_ALLOC:
            // Memory allocation operators return specific pointer types
            // This is validated in analyze_memory_op_expr
            break;
            
        case CAT_SYSCALL_OS:
            // Syscall operators require rawptr for pointer arguments
            // This is validated in analyze_syscall_op_expr
            break;
            
        case CAT_BITFIELD:
            // Bitfield operations require integer types
            if (left_type && !(left_type->kind >= TYPE_I8 && left_type->kind <= TYPE_U64)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Bitfield operator '%s' requires integer operands",
                              op_info->symbol);
                return false;
            }
            if (right_type && !(right_type->kind >= TYPE_I8 && right_type->kind <= TYPE_U64)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Bitfield operator '%s' requires integer operands",
                              op_info->symbol);
                return false;
            }
            break;
            
        case CAT_SHIFT_ROTATE:
            // Shift/rotate operations require integer types
            if (left_type && !(left_type->kind >= TYPE_I8 && left_type->kind <= TYPE_U64) &&
                !is_pointer_type(left_type)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Shift/rotate operator '%s' requires integer or pointer operands",
                              op_info->symbol);
                return false;
            }
            break;
            
        case CAT_ARITH_DENSE:
            // Arithmetic operations require numeric types
            if (left_type && !(left_type->kind >= TYPE_I8 && left_type->kind <= TYPE_F64)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "Arithmetic operator '%s' requires numeric operands",
                              op_info->symbol);
                return false;
            }
            break;
            
        case CAT_COMPARISON:
            // Comparison operations can work with most types
            break;
            
        case CAT_DATA_MOVEMENT:
        case CAT_ARITH_ASSIGN:
        case CAT_IO_FORMAT:
        case CAT_SPECIAL:
            // These categories have flexible type requirements
            break;
    }
    
    return true;
}

bool validate_memory_barrier_usage(SemanticAnalyzer *analyzer, TokenKind op, Expr *expr) {
    if (!analyzer || !expr) return false;
    
    // Memory barriers (!=> full, !> release, !< acquire) don't take operands
    // They are standalone operations
    switch (op) {
        case OP_BARRIER_FULL:   // !=>
        case OP_BARRIER_RELEASE: // !>
        case OP_BARRIER_ACQUIRE: // !<
            // These are valid memory barrier operations
            return true;
            
        default:
            return false;
    }
}

bool validate_simd_operation(SemanticAnalyzer *analyzer, TokenKind op, Type *operand_type, Expr *expr) {
    if (!analyzer || !expr) return false;
    
    // SIMD operations (/|/, |/|) require numeric types
    // In a full implementation, we'd check for vector types
    switch (op) {
        case OP_SIMD_DIV:      // /|/
        case OP_PARALLEL_DIV:  // |/|
            if (operand_type && !(operand_type->kind >= TYPE_I8 && operand_type->kind <= TYPE_F64)) {
                semantic_error(analyzer, expr->line, expr->column,
                              "SIMD operation requires numeric operands");
                return false;
            }
            return true;
            
        default:
            return false;
    }
}

// Helper function to get operator info by token kind
const OperatorInfo *get_operator_info_by_token(TokenKind token) {
    // Iterate through operator registry to find matching token
    size_t op_count = get_operator_count();
    for (size_t i = 0; i < op_count; i++) {
        const OperatorInfo *op = get_operator_by_index(i);
        if (op && op->token == token) {
            return op;
        }
    }
    return NULL;
}

// Function parameter checking

bool check_function_call(SemanticAnalyzer *analyzer, Symbol *function, Expr **args, size_t arg_count, Expr *call_expr) {
    if (!analyzer || !function || function->kind != SYMBOL_FUNCTION) return false;
    
    if (function->param_count != arg_count) {
        semantic_error(analyzer, call_expr->line, call_expr->column,
                      "Function '%s' expects %zu arguments, got %zu",
                      function->name, function->param_count, arg_count);
        return false;
    }
    
    return check_parameter_types(analyzer, function->params, function->param_count,
                                 args, arg_count);
}

bool check_function_return(SemanticAnalyzer *analyzer, Type *return_type, Expr *return_expr) {
    if (!analyzer) return false;
    
    Type *expected_type = analyzer->type_context.function_return_type;
    
    if (!expected_type && return_type) {
        semantic_error(analyzer, return_expr->line, return_expr->column,
                      "Function does not expect a return value");
        return false;
    }
    
    if (expected_type && !return_type) {
        semantic_error(analyzer, return_expr->line, return_expr->column,
                      "Function expects return value of type '%s'",
                      type_to_string(expected_type));
        return false;
    }
    
    if (expected_type && return_type && !types_compatible(expected_type, return_type)) {
        semantic_error(analyzer, return_expr->line, return_expr->column,
                      "Return type mismatch: expected '%s', got '%s'",
                      type_to_string(expected_type), type_to_string(return_type));
        return false;
    }
    
    return true;
}

bool check_parameter_types(SemanticAnalyzer *analyzer, Parameter *params, size_t param_count, Expr **args, size_t arg_count) {
    if (!analyzer || !params || !args) return false;
    
    if (param_count != arg_count) return false;
    
    for (size_t i = 0; i < param_count; i++) {
        Type *param_type = params[i].type;
        Type *arg_type = analyze_expression(analyzer, args[i]);
        
        if (!arg_type) {
            return false;
        }
        
        if (!types_compatible(param_type, arg_type)) {
            semantic_error(analyzer, args[i]->line, args[i]->column,
                          "Parameter %zu type mismatch: expected '%s', got '%s'",
                          i + 1, type_to_string(param_type), type_to_string(arg_type));
            return false;
        }
    }
    
    return true;
}
