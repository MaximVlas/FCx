#ifndef FCX_LEXER_H
#define FCX_LEXER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stddef.h>

// Token types for FCx language
typedef enum {
    // Literals
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,
    TOK_CHAR,
    TOK_IDENTIFIER,
    
    // Keywords
    KW_LET,
    KW_CONST,
    KW_FN,
    KW_IF,
    KW_ELSE,
    KW_LOOP,
    KW_WHILE,
    KW_RET,
    KW_HALT,
    KW_BREAK,
    KW_CONTINUE,
    KW_TRY,
    KW_CATCH,
    
    // Module system keywords (Rust-style) UNUSED AT THE MOMENT
    KW_MOD,         // mod foo;
    KW_USE,         // use std::io;
    KW_PUB,         // pub fn bar()
    KW_SELF,        // use self::foo;
    KW_SUPER,       // use super::bar;
    KW_CRATE,       // use crate::baz;
    KW_AS,          // use foo as bar;
    
    // Types
    KW_I8, KW_I16, KW_I32, KW_I64,
    KW_I128, KW_I256, KW_I512, KW_I1024,  // Big integers (signed)
    KW_U8, KW_U16, KW_U32, KW_U64,
    KW_U128, KW_U256, KW_U512, KW_U1024,  // Big integers (unsigned)
    KW_F32, KW_F64,
    KW_PTR, KW_RAWPTR,
    
    // Operators (200+ operators from combinatorial patterns)
    // Shift/Rotate Family
    OP_LSHIFT,          // <<
    OP_RSHIFT,          // >>
    OP_LOGICAL_RSHIFT,  // >>>
    OP_ROTATE_LEFT,     // <<<
    OP_ROTATE_RIGHT,    // >>>>
    OP_SLICE_START,     // </
    OP_SLICE_END,       // />
    OP_SLICE_RANGE,     // </>
    OP_REVERSE_SLICE,   // >/<
    
    // Arithmetic/Assignment Family
    OP_ADD_ASSIGN,      // +=
    OP_SUB_ASSIGN,      // -=
    OP_MUL_ASSIGN,      // *=
    OP_LSHIFT_ASSIGN,   // <<=
    OP_CAS,             // <=> (compare-and-swap)
    OP_SWAP,            // <==> (atomic swap)
    
    // Data Movement Family
    OP_MOVE_FORWARD,    // >
    OP_MOVE_BACKWARD,   // <
    OP_VOLATILE_STORE,  // ><
    OP_NO_ALIAS_STORE,  // <>
    OP_PUSH_INTO,       // |>
    OP_POP_FROM,        // <|
    OP_PUSH_SHIFT,      // >>|
    OP_POP_SHIFT,       // |<<
    
    // Bitfield Family
    OP_BITFIELD_EXTRACT, // &>
    OP_BITFIELD_INSERT,  // &<
    OP_BITWISE_ROTATE_XOR, // ^>
    OP_SHIFT_MASK,      // <<&
    OP_EXTRACT_RSHIFT,  // &>>
    
    // Memory Allocation Family
    OP_ALLOCATE,        // mem>
    OP_DEALLOCATE,      // >mem
    OP_MMIO_MAP,        // @>
    OP_MMIO_UNMAP,      // <@
    OP_LAYOUT_ACCESS,   // ->>
    OP_REVERSE_LAYOUT,  // <<-
    OP_STACK_ALLOC,     // stack>
    
    // Atomic/Concurrency Family
    OP_ATOMIC_READ,     // !
    OP_ATOMIC_WRITE,    // !!
    OP_ATOMIC_TRIPLE,   // !!!
    OP_ATOMIC_COND,     // !?
    OP_ATOMIC_FETCH_ADD, // ?!!
    OP_ATOMIC_XOR,      // ~!
    OP_ATOMIC_FENCE,    // |!|
    OP_BARRIER_FULL,    // !=>
    OP_BARRIER_RELEASE, // !>
    OP_BARRIER_ACQUIRE, // !<
    
    // Syscall/OS Family
    OP_WRITE_SYSCALL,   // $/
    OP_READ_SYSCALL,    // /$
    OP_RAW_SYSCALL,     // sys%
    OP_INLINE_ASM,      // asm%
    OP_SYS_WRAPPER,     // @sys
    OP_PRIV_ESCALATE,   // #!
    OP_CAPABILITY_CHECK, // !#
    OP_RESOURCE_QUERY,  // %$
    OP_RESOURCE_ALLOC,  // $%
    
    // IO/Formatting Family
    OP_PRINT_COMPACT,   // >>>
    OP_FORMAT_PRINT,    // <<<
    OP_ENCODE_BYTES,    // />/
    OP_DECODE_BYTES,    // <\<
    OP_DIRECT_OUTPUT,   // >>
    OP_DIRECT_INPUT,    // <<
    
    // Comparison Family
    OP_LT,              // <
    OP_LE,              // <=
    OP_GT,              // >
    OP_GE,              // >=
    OP_EQ,              // ==
    OP_NE,              // !=
    OP_PATTERN_NE,      // <>
    OP_OVERLAP_TEST,    // ><
    OP_LE_OR_FLAG,      // <=|
    OP_IMPLIES,         // |=>
    
    // Arithmetic Dense Family
    OP_DIV,             // /
    OP_INT_DIV,         // //
    OP_FAST_RECIP,      // ///
    OP_QUAD_DIV,        // ////
    OP_PENTA_DIV,       // /////
    OP_MOD_DIVISOR,     // /%
    OP_SIMD_DIV,        // /|/
    OP_PARALLEL_DIV,    // |/|
    
    // Phase 1: Trivial single-instruction additions
    OP_POPCOUNT,        // popcount>
    OP_CLZ,             // clz>
    OP_CTZ,             // ctz>
    OP_BYTESWAP,        // byteswap>
    OP_MIN,             // <?
    OP_MAX,             // >?
    OP_THREE_WAY_CMP,   // <=>? / <~>
    OP_CLAMP,           // <|>
    OP_LE_MAYBE,        // <=?
    OP_GE_MAYBE,        // >=?
    OP_EQ_MAYBE,        // ==?
    OP_NE_MAYBE,        // !=?
    OP_LT_DOUBLE,       // <??
    OP_GT_DOUBLE,       // >??
    OP_CMP_ASSERT,      // <=>!
    OP_SWAP_ASSERT,     // <==>!
    OP_SQRT,            // sqrt>
    OP_RSQRT,           // rsqrt>
    OP_ABS,             // abs>
    OP_FLOOR,           // floor>
    OP_CEIL,            // ceil>
    OP_TRUNC,           // trunc>
    OP_ROUND,           // round>
    OP_PREFETCH,        // prefetch>
    OP_PREFETCH_W,      // prefetch_write>
    
    // Phase 2: Arithmetic extensions
    OP_SAT_ADD,         // +|
    OP_SAT_SUB,         // -|
    OP_SAT_MUL,         // *|
    OP_WRAP_ADD,        // +%
    OP_WRAP_SUB,        // -%
    OP_WRAP_MUL,        // *%
    OP_CHECKED_ADD,     // +?
    OP_CHECKED_SUB,     // -?
    OP_CHECKED_MUL,     // *?
    
    // Phase 3: Range and alignment
    OP_RANGE,           // ..
    OP_RANGE_INCLUSIVE, // ..=
    OP_RANGE_EXCLUSIVE, // ..< / ..>
    OP_ALIGN_UP,        // align_up>
    OP_ALIGN_DOWN,      // align_down>
    OP_IS_ALIGNED,      // is_aligned?>
    OP_ARENA_ALLOC,     // arena>
    OP_ARENA_FREE,      // >arena
    OP_SLAB_ALLOC,      // slab>
    OP_SLAB_FREE,       // >slab
    
    // Phase 4: Compile-time operators
    OP_SIZEOF,          // @sizeof>
    OP_ALIGNOF,         // @alignof>
    OP_OFFSETOF,        // @offsetof>
    OP_STATIC_ASSERT,   // @!
    OP_AT_SYMBOL,       // @
    
    // Pointer casting operators (Task 4.3)
    OP_CAST_TO,         // :> (cast-to operator)
    OP_REINTERPRET_CAST, // :>: (reinterpret cast)
    OP_PTR_TO_INT,      // <|> (pointer-to-integer)
    OP_INT_TO_PTR,      // |<> (integer-to-pointer)
    
    // Special operators
    OP_ASSIGN,          // =
    OP_ASSIGN_INFER,    // :=
    OP_FUNCTION_DEF,    // <=> (in function context)
    OP_CONDITIONAL,     // ?
    OP_ERROR_HANDLE,    // ?!
    
    // Punctuation
    TOK_SEMICOLON,      // ;
    TOK_COLON,          // :
    TOK_DOUBLE_COLON,   // :: (module path separator)
    TOK_COMMA,          // ,
    TOK_DOT,            // .
    TOK_LPAREN,         // (
    TOK_RPAREN,         // )
    TOK_LBRACE,         // {
    TOK_RBRACE,         // }
    TOK_LBRACKET,       // [
    TOK_RBRACKET,       // ]
    
    // Special
    TOK_EOF,
    TOK_ERROR,
    
    // Total count for validation
    TOK_COUNT
} TokenKind;

// Operator categories for semantic analysis
typedef enum {
    CAT_SHIFT_ROTATE,
    CAT_ARITH_ASSIGN,
    CAT_DATA_MOVEMENT,
    CAT_BITFIELD,
    CAT_MEMORY_ALLOC,
    CAT_ATOMIC_CONCUR,
    CAT_SYSCALL_OS,
    CAT_IO_FORMAT,
    CAT_COMPARISON,
    CAT_ARITH_DENSE,
    CAT_SPECIAL
} OperatorCategory;

// Operator directionality for pattern generation
typedef enum {
    DIR_LEFT_FACING,    // <, <<, <<<
    DIR_RIGHT_FACING,   // >, >>, >>>
    DIR_BIDIRECTIONAL   // <>, ><, <=>
} Direction;

// Operator arity
typedef enum {
    ARITY_UNARY,
    ARITY_BINARY,
    ARITY_TERNARY,
    ARITY_NARY
} Arity;

// Associativity for parsing
typedef enum {
    ASSOC_LEFT,
    ASSOC_RIGHT,
    ASSOC_NONE
} Associativity;

// Operator registry entry
typedef struct {
    const char *symbol;           // "<=>", ">>>", "/|/", etc.
    TokenKind token;              // Token type
    uint8_t precedence;           // 1-12 precedence levels
    Associativity associativity;  // Left, Right, None
    Arity arity;                  // Unary, Binary, Ternary, Nary
    OperatorCategory category;    // Family classification
    const char *semantics;        // Human-readable description
    const char *assembly_template; // x86_64 instruction template
    uint8_t length;               // Symbol length (1-5 characters)
    Direction directionality;     // Left-facing, Right-facing, Bidirectional
} OperatorInfo;

// Token structure
typedef struct {
    TokenKind kind;
    const char *start;
    size_t length;
    size_t line;
    size_t column;
    
    // Token value (for literals)
    union {
        int64_t integer;
        double floating;
        char *string;
        char character;
    } value;
} Token;

// Lexer state
typedef struct {
    const char *source;
    const char *current;
    const char *start;
    size_t line;
    size_t column;
    bool had_error;
} Lexer;

// Operator trie node for efficient recognition
typedef struct TrieNode {
    struct TrieNode *children[256]; // ASCII character set
    const OperatorInfo *operator_info; // NULL if not terminal
    bool is_terminal;
} TrieNode;

// Function declarations
void lexer_init(Lexer *lexer, const char *source);
Token lexer_next_token(Lexer *lexer);
bool lexer_is_at_end(const Lexer *lexer);
void lexer_error(Lexer *lexer, const char *message);

// Operator registry functions
const OperatorInfo *lookup_operator(const char *symbol);
const OperatorInfo *find_operator_by_symbol(const char *symbol);
void init_operator_registry(void);
void build_operator_trie(void);
const OperatorInfo *trie_lookup(const char *symbol, size_t length);
const OperatorInfo *trie_lookup_greedy(const char *symbol, size_t max_length, size_t *matched_length);
bool is_valid_operator(const char *symbol);
uint8_t get_operator_precedence(const char *symbol);
OperatorCategory get_operator_category(const char *symbol);
bool operator_has_arity(const char *symbol, Arity expected_arity);
const char *get_operator_assembly_template(const char *symbol);
size_t get_operator_count(void);
bool validate_operator_count(void);
const OperatorInfo *get_operator_by_index(size_t index);
void cleanup_operator_registry(void);

// Accessor functions for error handler
const OperatorInfo* get_operator_registry(void);
size_t get_operator_registry_size(void);

// Combinatorial pattern validation functions
bool validate_combinatorial_generation(void);
void generate_operator_patterns(void);
bool validate_operator_precedence(void);
bool validate_assembly_templates(void);
bool validate_trie_structure(void);
bool validate_complete_operator_registry(void);

// Utility functions
bool is_alpha(char c);
bool is_digit(char c);
bool is_alnum(char c);
char peek(const Lexer *lexer);
char peek_next(const Lexer *lexer);
char advance(Lexer *lexer);
bool match(Lexer *lexer, char expected);

// Testing functions
void test_lexer_functionality(void);

#endif // FCX_LEXER_H