#ifndef FCX_PARSER_H
#define FCX_PARSER_H

#include "../lexer/lexer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Type Type;

// Expression types
typedef enum {
    EXPR_LITERAL,
    EXPR_IDENTIFIER,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_TERNARY,
    EXPR_CALL,
    EXPR_INDEX,        // Array/pointer indexing: ptr[i]
    EXPR_DEREF,        // Pointer dereference: @ptr or ptr@
    EXPR_ASSIGNMENT,
    EXPR_MULTI_ASSIGN,
    EXPR_CONDITIONAL,
    EXPR_FUNCTION_DEF,
    EXPR_MEMORY_OP,
    EXPR_ATOMIC_OP,
    EXPR_SYSCALL_OP,
    EXPR_INLINE_ASM    // Inline assembly: asm% "..."
} ExprType;

// Statement types
typedef enum {
    STMT_EXPRESSION,
    STMT_LET,
    STMT_FUNCTION,
    STMT_IF,
    STMT_LOOP,
    STMT_RETURN,
    STMT_HALT,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_MOD,           // mod foo; or mod foo { ... }
    STMT_USE,           // use std::io;
} StmtType;

// Type system
typedef enum {
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_I128, TYPE_I256, TYPE_I512, TYPE_I1024,  // Big integers (signed)
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_U128, TYPE_U256, TYPE_U512, TYPE_U1024,  // Big integers (unsigned)
    TYPE_F32, TYPE_F64,
    TYPE_BOOL,
    TYPE_BYTE,
    TYPE_PTR,      // ptr<T>
    TYPE_RAWPTR,   // rawptr
    TYPE_BYTEPTR,  // byteptr
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_FUNCTION,
    TYPE_CHANNEL,
    TYPE_THREAD_HANDLE,
    TYPE_SYSCALL_RESULT
} TypeKind;

// Literal value
typedef struct {
    enum {
        LIT_INTEGER,
        LIT_FLOAT,
        LIT_STRING,
        LIT_BOOLEAN,
        LIT_CHARACTER,
        LIT_RAW_BYTES,
        LIT_BIGINT       // For integers > 64 bits (up to 1024 bits)
    } type;
    
    union {
        int64_t integer;
        double floating;
        char *string;
        bool boolean;
        char character;
        struct {
            uint8_t *data;
            size_t length;
        } raw_bytes;
        struct {
            uint64_t limbs[16];  // Up to 1024 bits (16 x 64-bit limbs), little-endian
            uint8_t num_limbs;   // Number of limbs used (1-16)
            bool is_negative;
        } bigint;
    } value;
} LiteralValue;

// Type structure
struct Type {
    TypeKind kind;
    union {
        struct Type *element_type; // for ptr<T>, arrays
        struct {
            struct Type **param_types;
            size_t param_count;
            struct Type *return_type;
        } function;
        struct {
            char *name;
            struct Field *fields;
            size_t field_count;
            bool packed;
            size_t alignment;
        } struct_type;
        struct {
            struct Type *element_type;
            size_t size;
        } array;
    } data;
};

// Function parameter
typedef struct {
    char *name;
    Type *type;
} Parameter;

// Block of statements
typedef struct {
    Stmt **statements;
    size_t count;
    size_t capacity;
} Block;

// Expression structure
struct Expr {
    ExprType type;
    size_t line;
    size_t column;
    
    union {
        LiteralValue literal;
        char *identifier;
        
        struct {
            TokenKind op;
            Expr *left;
            Expr *right;
        } binary;
        
        struct {
            TokenKind op;
            Expr *operand;
        } unary;
        
        struct {
            TokenKind op;
            Expr *first;
            Expr *second;
            Expr *third;
        } ternary;
        
        struct {
            Expr *function;
            Expr **args;
            size_t arg_count;
        } call;
        
        struct {
            Expr *base;      // The pointer/array being indexed
            Expr *index;     // The index expression
            uint32_t element_size; // Size of element (0 = infer from type)
        } index;
        
        struct {
            Expr *pointer;   // The pointer being dereferenced
            bool is_write;   // true for store, false for load
            Expr *value;     // Value to store (NULL for load)
        } deref;
        
        struct {
            Expr *target;
            Expr *value;
            TokenKind op; // for :=, +=, <<=, etc.
        } assignment;
        
        struct {
            Expr **targets;
            Expr **values;
            size_t count;
        } multi_assign;
        
        struct {
            Expr *condition;
            Expr *then_expr;
            Expr *else_expr;
        } conditional;
        
        struct {
            char *name;
            Parameter *params;
            size_t param_count;
            Block body;
            bool is_compact; // <=> syntax vs fn syntax
        } function_def;
        
        struct {
            enum {
                MEM_ALLOCATE,    // mem>
                MEM_DEALLOCATE,  // >mem
                MEM_STACK_ALLOC, // stack>
                MEM_STACK_DEALLOC, // >stack
                MEM_MMIO_MAP,    // @>
                MEM_MMIO_UNMAP,  // <@
                MEM_LAYOUT_ACCESS, // ->>
                MEM_ARENA_ALLOC, // arena>
                MEM_ARENA_RESET, // >arena
                MEM_SLAB_ALLOC,  // slab>
                MEM_SLAB_FREE,   // >slab
                MEM_ALIGN_UP,    // align_up>
                MEM_ALIGN_DOWN,  // align_down>
                MEM_IS_ALIGNED,  // is_aligned?>
                MEM_PREFETCH,    // prefetch>
                MEM_PREFETCH_WRITE // prefetch_write>
            } op;
            Expr **operands;
            size_t operand_count;
        } memory_op;
        
        struct {
            enum {
                ATOMIC_READ,     // !
                ATOMIC_WRITE,    // !!
                ATOMIC_CAS,      // <=>
                ATOMIC_SWAP,     // <==>
                ATOMIC_FETCH_ADD // ?!!
            } op;
            Expr **operands;
            size_t operand_count;
        } atomic_op;
        
        struct {
            Expr *syscall_num; // for sys%(num, args)
            Expr **args;
            size_t arg_count;
            enum {
                SYSCALL_RAW,    // sys%
                SYSCALL_WRITE,  // $/
                SYSCALL_READ    // /$
            } syscall_type;
        } syscall_op;
        
        struct {
            char *asm_template;      // Assembly template string
            char **output_constraints; // Output constraints ("=r", "=a", etc.)
            char **input_constraints;  // Input constraints ("r", "m", etc.)
            Expr **output_exprs;     // Output expressions (variables to write to)
            Expr **input_exprs;      // Input expressions (values to read)
            char **clobbers;         // Clobbered registers ("memory", "cc", etc.)
            size_t output_count;
            size_t input_count;
            size_t clobber_count;
            bool is_volatile;        // volatile asm
        } inline_asm;
    } data;
};

// Loop types
typedef enum {
    LOOP_TRADITIONAL,  // loop { }
    LOOP_COUNT,        // (expr) << n
    LOOP_RANGE,        // i </ n
    LOOP_WHILE         // while condition
} LoopType;

// Syntax verbosity levels
typedef enum {
    SYNTAX_VERBOSE,    // fn name(params) -> type { }
    SYNTAX_MEDIUM,     // name <=> fn(params) { }
    SYNTAX_COMPACT     // name<=>(...){...}
} SyntaxVerbosity;

// Statement structure
struct Stmt {
    StmtType type;
    size_t line;
    size_t column;
    
    union {
        Expr *expression;
        
        struct {
            char *name;
            Type *type_annotation;
            Expr *initializer;
            bool is_const;
        } let;
        
        struct {
            char *name;
            Parameter *params;
            size_t param_count;
            Type *return_type;
            Block body;
            SyntaxVerbosity verbosity;
            bool is_public;     // pub fn
        } function;
        
        struct {
            Expr *condition;
            Block then_branch;
            Block else_branch;
            bool is_compact; // -> syntax vs traditional if
        } if_stmt;
        
        struct {
            LoopType loop_type;
            Expr *condition;
            Block body;
            Expr *iteration;
        } loop;
        
        Expr *return_value; // for return and halt
        
        // Module declaration: mod foo; or mod foo { ... }
        struct {
            char *name;
            bool is_public;     // pub mod
            bool is_inline;     // mod foo { ... } vs mod foo;
            Block body;         // For inline modules
        } mod_decl;
        
        // Use declaration: use std::io::println;
        struct {
            char **path;        // Path segments: ["std", "io", "println"]
            size_t path_len;
            char *alias;        // For "use foo as bar"
            bool is_glob;       // For "use foo::*"
            bool is_public;     // pub use
            // For grouped imports: use foo::{a, b, c}
            char **items;       // Item names in group
            size_t item_count;
        } use_decl;
    } data;
};

// Disambiguation context tracking
typedef enum {
    CONTEXT_EXPRESSION,     // General expression context
    CONTEXT_FUNCTION_DEF,   // Function definition context (<=> as function)
    CONTEXT_ATOMIC_OP,      // Atomic operation context (<=> as CAS)
    CONTEXT_FORMAT_STRING,  // Format string context (<<< as format vs rotate)
    CONTEXT_ASSIGNMENT,     // Assignment context
    CONTEXT_SYSCALL,        // Syscall context
    CONTEXT_MEMORY_OP       // Memory operation context
} DisambiguationContext;

// Context stack for nested disambiguation
typedef struct {
    DisambiguationContext contexts[16]; // Max nesting depth
    size_t depth;
} ContextStack;

// Parser state
typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
    
    // Disambiguation context stack
    ContextStack context_stack;
    
    // Operator disambiguation flags
    bool disallow_ambiguous_ops;  // --disallow-ambiguous-ops flag
    bool strict_parsing;          // Strict parsing mode
} Parser;

// Precedence levels (12 levels as per design)
typedef enum {
    PREC_NONE = 0,
    PREC_SEQUENCE = 1,     // , (comma operator)
    PREC_ASSIGNMENT = 2,   // =, :=, +=, -=
    PREC_LOGICAL = 3,      // &&, ||
    PREC_COMBINED_ASSIGN = 4, // <<=, <=>, <==>
    PREC_COMPARISON = 5,   // <, <=, >=, >, ==, !=
    PREC_BITWISE = 6,      // &, ^, |, &>, ^<
    PREC_ADDITIVE = 7,     // +, -, |>, <|
    PREC_MULTIPLICATIVE = 8, // *, /, %, //, ///
    PREC_SHIFT_ROTATE = 9, // <<<, >>>, <<, >>
    PREC_FIELD_LAYOUT = 10, // ->>, <<-, ., :
    PREC_UNARY = 11,       // !, ~, unary -, +
    PREC_PARENTHESES = 12  // (), []
} Precedence;

// Parse rule for Pratt parser
typedef struct {
    Expr *(*prefix)(Parser *parser);
    Expr *(*infix)(Parser *parser, Expr *left);
    Precedence precedence;
} ParseRule;

// Function declarations
void parser_init(Parser *parser, Lexer *lexer);
Expr *parse_expression(Parser *parser);
Stmt *parse_statement(Parser *parser);
Stmt *parse_function(Parser *parser);
Block parse_block(Parser *parser);

// Pratt parser functions
Expr *parse_precedence(Parser *parser, Precedence precedence);
const ParseRule *get_rule(TokenKind type);

// Expression parsing functions
Expr *parse_literal(Parser *parser);
Expr *parse_identifier(Parser *parser);
Expr *parse_binary(Parser *parser, Expr *left);
Expr *parse_unary(Parser *parser);
Expr *parse_ternary(Parser *parser, Expr *left);
Expr *parse_call(Parser *parser, Expr *left);
Expr *parse_index(Parser *parser, Expr *left);
Expr *parse_deref(Parser *parser);
Expr *parse_assignment(Parser *parser, Expr *left);
Expr *parse_grouping(Parser *parser);
Expr *parse_function_definition(Parser *parser, Expr *name_expr);
Expr *parse_memory_operation(Parser *parser, TokenKind op);
Expr *parse_atomic_operation(Parser *parser, TokenKind op);
Expr *parse_syscall_operation(Parser *parser, TokenKind op);
Expr *parse_inline_asm(Parser *parser);

// Statement parsing functions
Stmt *parse_let_statement(Parser *parser);
Stmt *parse_function_statement(Parser *parser);
Stmt *parse_if_statement(Parser *parser);
Stmt *parse_loop_statement(Parser *parser);
Stmt *parse_return_statement(Parser *parser);
Stmt *parse_break_continue_statement(Parser *parser);
Stmt *parse_expression_statement(Parser *parser);
Stmt *parse_compact_conditional_statement(Parser *parser);

// Module system parsing functions
Stmt *parse_mod_statement(Parser *parser, bool is_public);
Stmt *parse_use_statement(Parser *parser, bool is_public);
Stmt *parse_pub_statement(Parser *parser);

// Operator disambiguation rules for resolving parsing conflicts
bool is_function_context(Parser *parser);
bool is_cas_context(Parser *parser);
bool is_format_context(Parser *parser);
TokenKind disambiguate_operator(Parser *parser, const char *symbol);





// Disambiguation functions
DisambiguationContext get_current_context(const Parser *parser);
void push_context(Parser *parser, DisambiguationContext context);
void pop_context(Parser *parser);
bool context_allows_operator(DisambiguationContext context, const char *symbol);

// Specific disambiguation cases
TokenKind disambiguate_cas_vs_function(Parser *parser);
TokenKind disambiguate_rotate_vs_format(Parser *parser);
TokenKind disambiguate_shift_vs_io(Parser *parser);

// Utility functions
void parser_advance(Parser *parser);
bool parser_check(Parser *parser, TokenKind type);
bool parser_match(Parser *parser, TokenKind type);
Token consume(Parser *parser, TokenKind type, const char *message);
void synchronize(Parser *parser);
void error_at_current(Parser *parser, const char *message);
void error_at_previous(Parser *parser, const char *message);

// Memory management
Expr *allocate_expr(ExprType type);
Stmt *allocate_stmt(StmtType type);
void free_expr(Expr *expr);
void free_stmt(Stmt *stmt);
void free_block(Block *block);

#endif // FCX_PARSER_H