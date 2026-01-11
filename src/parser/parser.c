#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Forward declarations for parsing functions
Expr *parse_postfix(Parser *parser, Expr *operand);

// Portable string duplication function for C99 compatibility
static char *fcx_strdup(const char *str) {
  if (!str)
    return NULL;
  size_t len = strlen(str);
  char *copy = malloc(len + 1);
  if (copy) {
    memcpy(copy, str, len + 1);
  }
  return copy;
}

// Helper functions to categorize operators
static bool is_memory_operator(TokenKind op) {
  return (op == OP_ALLOCATE || op == OP_DEALLOCATE || op == OP_STACK_ALLOC ||
          op == OP_MMIO_MAP || op == OP_MMIO_UNMAP || op == OP_LAYOUT_ACCESS ||
          op == OP_REVERSE_LAYOUT || op == OP_ARENA_ALLOC ||
          op == OP_ARENA_FREE || op == OP_SLAB_ALLOC || op == OP_SLAB_FREE ||
          op == OP_ALIGN_UP || op == OP_ALIGN_DOWN || op == OP_IS_ALIGNED ||
          op == OP_PREFETCH || op == OP_PREFETCH_W);
}

static bool is_atomic_operator(TokenKind op) {
  return (op == OP_ATOMIC_READ || op == OP_ATOMIC_WRITE ||
          op == OP_ATOMIC_TRIPLE || op == OP_ATOMIC_COND ||
          op == OP_ATOMIC_FETCH_ADD || op == OP_ATOMIC_XOR ||
          op == OP_ATOMIC_FENCE || op == OP_BARRIER_FULL ||
          op == OP_BARRIER_RELEASE || op == OP_BARRIER_ACQUIRE ||
          op == OP_CAS || op == OP_SWAP);
}

static bool is_syscall_operator(TokenKind op) {
  return (op == OP_WRITE_SYSCALL || op == OP_READ_SYSCALL ||
          op == OP_RAW_SYSCALL || op == OP_SYS_WRAPPER ||
          op == OP_PRIV_ESCALATE || op == OP_CAPABILITY_CHECK ||
          op == OP_RESOURCE_QUERY || op == OP_RESOURCE_ALLOC);
}

// FCx Parser Implementation with Operator Disambiguation
// Resolves parsing conflicts for operators like <=> (function vs CAS) and <<<
// (rotate vs format)

void parser_init(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->had_error = false;
  parser->panic_mode = false;

  // Initialize disambiguation context stack
  parser->context_stack.depth = 0;
  parser->context_stack.contexts[0] = CONTEXT_EXPRESSION;

  // Initialize disambiguation flags
  parser->disallow_ambiguous_ops = false;
  parser->strict_parsing = false;

  // Advance to first token
  parser_advance(parser);
}

void parser_advance(Parser *parser) {
  parser->previous = parser->current;
  parser->current = lexer_next_token(parser->lexer);
}

bool parser_check(Parser *parser, TokenKind type) {
  return parser->current.kind == type;
}

bool parser_match(Parser *parser, TokenKind type) {
  if (parser_check(parser, type)) {
    parser_advance(parser);
    return true;
  }
  return false;
}

Token consume(Parser *parser, TokenKind type, const char *message) {
  if (parser->current.kind == type) {
    Token token = parser->current;
    parser_advance(parser);
    return token;
  }

  error_at_current(parser, message);
  return parser->current;
}

void error_at_current(Parser *parser, const char *message) {
  fprintf(stderr, "[Line %zu] Error at '%.*s': %s\n", parser->current.line,
          (int)parser->current.length, parser->current.start, message);
  parser->had_error = true;
}

void error_at_previous(Parser *parser, const char *message) {
  fprintf(stderr, "[Line %zu] Error at '%.*s': %s\n", parser->previous.line,
          (int)parser->previous.length, parser->previous.start, message);
  parser->had_error = true;
}

void synchronize(Parser *parser) {
  parser->panic_mode = false;

  while (parser->current.kind != TOK_EOF) {
    if (parser->previous.kind == TOK_SEMICOLON)
      return;

    switch (parser->current.kind) {
    case KW_FN:
    case KW_LET:
    case KW_CONST:
    case KW_IF:
    case KW_LOOP:
    case KW_WHILE:
    case KW_RET:
    case KW_BREAK:
    case KW_CONTINUE:
      return;
    default:
      break;
    }

    parser_advance(parser);
  }
}

// Disambiguation context management
DisambiguationContext get_current_context(const Parser *parser) {
  if (parser->context_stack.depth == 0) {
    return CONTEXT_EXPRESSION;
  }
  return parser->context_stack.contexts[parser->context_stack.depth - 1];
}

void push_context(Parser *parser, DisambiguationContext context) {
  if (parser->context_stack.depth < 15) {
    parser->context_stack.contexts[parser->context_stack.depth] = context;
    parser->context_stack.depth++;
  }
}

void pop_context(Parser *parser) {
  if (parser->context_stack.depth > 0) {
    parser->context_stack.depth--;
  }
}

bool context_allows_operator(DisambiguationContext context,
                             const char *symbol) {
  if (strcmp(symbol, "<=>") == 0) {
    // <=> can be function definition or compare-and-swap
    return context == CONTEXT_FUNCTION_DEF || context == CONTEXT_ATOMIC_OP ||
           context == CONTEXT_EXPRESSION;
  }
  if (strcmp(symbol, "<<<") == 0) {
    // <<< can be rotate left or format print
    return context !=
           CONTEXT_FORMAT_STRING; // Prefer rotate unless in format context
  }
  return true; // Most operators are unambiguous
}

// Operator disambiguation rules for resolving parsing conflicts
bool is_function_context(Parser *parser) {
  // Check if we're in a context where <=> should be interpreted as function
  // definition
  Token prev = parser->previous;

  // Pattern: identifier <=> (function definition)
  if (prev.kind == TOK_IDENTIFIER) {
    return true;
  }

  // Pattern: @name <=> (named function)
  if (prev.kind == TOK_IDENTIFIER &&
      parser->previous.start > parser->lexer->source &&
      *(parser->previous.start - 1) == '@') {
    return true;
  }

  return false;
}

bool is_cas_context(Parser *parser) {
  // Check if we're in a context where <=> should be interpreted as
  // compare-and-swap
  DisambiguationContext context = get_current_context(parser);

  // In atomic operation context, prefer CAS
  if (context == CONTEXT_ATOMIC_OP) {
    return true;
  }

  // If previous token suggests atomic operation
  if (parser->previous.kind == OP_ATOMIC_READ ||
      parser->previous.kind == OP_ATOMIC_WRITE) {
    return true;
  }

  return false;
}

bool is_format_context(Parser *parser) {
  // Check if we're in a context where <<< should be interpreted as format
  DisambiguationContext context = get_current_context(parser);
  return context == CONTEXT_FORMAT_STRING;
}

TokenKind disambiguate_operator(Parser *parser, const char *symbol) {
  if (strcmp(symbol, "<=>") == 0) {
    return disambiguate_cas_vs_function(parser);
  }
  if (strcmp(symbol, "<<<") == 0) {
    return disambiguate_rotate_vs_format(parser);
  }
  if (strcmp(symbol, ">>") == 0 || strcmp(symbol, "<<") == 0) {
    return disambiguate_shift_vs_io(parser);
  }

  // Default: lookup in operator registry
  const OperatorInfo *op = lookup_operator(symbol);
  return op ? op->token : TOK_ERROR;
}

TokenKind disambiguate_cas_vs_function(Parser *parser) {
  if (is_function_context(parser)) {
    push_context(parser, CONTEXT_FUNCTION_DEF);
    return OP_FUNCTION_DEF;
  } else if (is_cas_context(parser)) {
    push_context(parser, CONTEXT_ATOMIC_OP);
    return OP_CAS;
  } else {
    // Default to CAS in expression context
    return OP_CAS;
  }
}

TokenKind disambiguate_rotate_vs_format(Parser *parser) {
  if (is_format_context(parser)) {
    return OP_FORMAT_PRINT;
  } else {
    // Default to rotate left
    return OP_ROTATE_LEFT;
  }
}

TokenKind disambiguate_shift_vs_io(Parser *parser) {
  DisambiguationContext context = get_current_context(parser);

  // In I/O context, prefer I/O operators
  if (context == CONTEXT_SYSCALL) {
    if (parser->current.start[0] == '>') {
      return OP_DIRECT_OUTPUT;
    } else {
      return OP_DIRECT_INPUT;
    }
  }

  // Default to shift operators
  if (parser->current.start[0] == '>') {
    return OP_RSHIFT;
  } else {
    return OP_LSHIFT;
  }
}

// Parse rule table for Pratt parser - maps tokens to parsing functions
static ParseRule rules[TOK_COUNT];
static bool rules_initialized = false;

// Initialize parse rules table
static void init_parse_rules(void) {
  if (rules_initialized)
    return;

  // Initialize all rules to NULL
  for (int i = 0; i < TOK_COUNT; i++) {
    rules[i].prefix = NULL;
    rules[i].infix = NULL;
    rules[i].precedence = PREC_NONE;
  }

  // Literals and identifiers
  rules[TOK_INTEGER].prefix = parse_literal;
  rules[TOK_FLOAT].prefix = parse_literal;
  rules[TOK_STRING].prefix = parse_literal;
  rules[TOK_CHAR].prefix = parse_literal;
  rules[TOK_IDENTIFIER].prefix = parse_identifier;

  // Handle type keywords that can be used as identifiers in expressions
  rules[KW_PTR].prefix =
      parse_identifier; // Allow 'ptr' to be parsed as identifier
  rules[KW_RAWPTR].prefix =
      parse_identifier; // Allow 'rawptr' to be parsed as identifier

  // Make sure identifier precedence is set
  rules[TOK_INTEGER].precedence = PREC_NONE;
  rules[TOK_FLOAT].precedence = PREC_NONE;
  rules[TOK_STRING].precedence = PREC_NONE;
  rules[TOK_CHAR].precedence = PREC_NONE;
  rules[TOK_IDENTIFIER].precedence = PREC_NONE;
  rules[KW_PTR].precedence = PREC_NONE;
  rules[KW_RAWPTR].precedence = PREC_NONE;

  // Grouping
  rules[TOK_LPAREN].prefix = parse_grouping;
  rules[TOK_LPAREN].infix = parse_call;
  rules[TOK_LPAREN].precedence = PREC_PARENTHESES;

  // Array/pointer indexing: ptr[index]
  rules[TOK_LBRACKET].infix = parse_index;
  rules[TOK_LBRACKET].precedence = PREC_PARENTHESES;

  // Pointer dereference: @ptr (using @ as dereference operator)
  rules[OP_AT_SYMBOL].prefix = parse_deref;
  rules[OP_AT_SYMBOL].precedence = PREC_UNARY;

  // Unary operators
  rules[OP_ATOMIC_READ].prefix = parse_unary;
  rules[OP_ATOMIC_READ].precedence = PREC_UNARY;

  // Unary minus and plus
  rules[OP_SUB_ASSIGN].prefix = parse_unary; // - as unary minus
  rules[OP_ADD_ASSIGN].prefix = parse_unary; // + as unary plus

  // Memory operators as prefix
  rules[OP_ALLOCATE].prefix = parse_unary;
  rules[OP_DEALLOCATE].prefix = parse_unary;
  rules[OP_STACK_ALLOC].prefix = parse_unary;
  rules[OP_MMIO_MAP].prefix = parse_unary;
  rules[OP_ARENA_ALLOC].prefix = parse_unary;
  rules[OP_SLAB_ALLOC].prefix = parse_unary;

  // Atomic operators as prefix (but OP_ATOMIC_WRITE should only be infix for
  // ptr!!) rules[OP_ATOMIC_WRITE].prefix = parse_unary;  // Remove this - !!
  // should only be postfix
  rules[OP_ATOMIC_XOR].prefix = parse_unary;

  // Syscall operators as prefix
  rules[OP_RAW_SYSCALL].prefix = parse_unary;

  // More prefix operators that were missing
  rules[OP_DEALLOCATE].prefix = parse_unary;      // >mem
  rules[OP_ARENA_FREE].prefix = parse_unary;      // >arena
  rules[OP_SLAB_FREE].prefix = parse_unary;       // >slab
  rules[OP_MMIO_UNMAP].prefix = parse_unary;      // <@
  rules[OP_BARRIER_FULL].prefix = parse_unary;    // !=>
  rules[OP_BARRIER_RELEASE].prefix = parse_unary; // !>
  rules[OP_BARRIER_ACQUIRE].prefix = parse_unary; // !<
  rules[OP_PRIV_ESCALATE].prefix = parse_unary;   // #!
  rules[OP_PRIV_ESCALATE].precedence = PREC_UNARY;
  rules[OP_CAPABILITY_CHECK].prefix = parse_unary; // !#
  rules[OP_CAPABILITY_CHECK].precedence = PREC_UNARY;
  // Remove prefix rules for syscall operators - they should only be infix for
  // "fd $/ buf" rules[OP_WRITE_SYSCALL].prefix = parse_unary;    // $/
  // rules[OP_READ_SYSCALL].prefix = parse_unary;     // /$

  // Function-like operators that should be parsed as prefix
  rules[OP_SQRT].prefix = parse_unary;          // sqrt>
  rules[OP_ABS].prefix = parse_unary;           // abs>
  rules[OP_FLOOR].prefix = parse_unary;         // floor>
  rules[OP_CEIL].prefix = parse_unary;          // ceil>
  rules[OP_POPCOUNT].prefix = parse_unary;      // popcount>
  rules[OP_CLZ].prefix = parse_unary;           // clz>
  rules[OP_ALIGN_UP].prefix = parse_unary;      // align_up>
  rules[OP_ALIGN_DOWN].prefix = parse_unary;    // align_down>
  rules[OP_IS_ALIGNED].prefix = parse_unary;    // is_aligned?>
  rules[OP_SIZEOF].prefix = parse_unary;        // @sizeof>
  rules[OP_ALIGNOF].prefix = parse_unary;       // @alignof>
  rules[OP_OFFSETOF].prefix = parse_unary;      // @offsetof>
  rules[OP_STATIC_ASSERT].prefix = parse_unary; // @!
  rules[OP_PREFETCH].prefix = parse_unary;      // prefetch>
  rules[OP_PREFETCH_W].prefix = parse_unary;    // prefetch_write>

  // Print/IO operators that might be missing - prefix only, no infix precedence
  rules[OP_PRINT_COMPACT].prefix = parse_unary; // >>>
  rules[OP_PRINT_COMPACT].precedence = PREC_NONE;
  rules[OP_FORMAT_PRINT].prefix = parse_unary;  // <<<
  rules[OP_FORMAT_PRINT].precedence = PREC_NONE;

  // Add rule for token 83 (print> operator)
  rules[83].prefix = parse_unary;
  rules[83].precedence = PREC_NONE;  // Prefix-only, no infix precedence

  // Token 91 is OP_STACK_ALLOC which is reused for print> by the lexer
  // It should be a prefix unary operator, not infix binary
  rules[91].prefix = parse_unary;
  rules[91].precedence = PREC_NONE;  // Prefix-only, no infix precedence

  // Binary operators by precedence level

  // PREC_SEQUENCE (1) - comma operator
  rules[TOK_COMMA].infix = parse_binary;
  rules[TOK_COMMA].precedence = PREC_SEQUENCE;

  // PREC_ASSIGNMENT (2) - assignment operators
  rules[OP_ASSIGN].infix = parse_assignment;
  rules[OP_ASSIGN].precedence = PREC_ASSIGNMENT;
  rules[OP_ASSIGN_INFER].infix = parse_assignment;
  rules[OP_ASSIGN_INFER].precedence = PREC_ASSIGNMENT;
  // Note: OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN are used for both
  // assignment (+=, -=, *=) and binary operations (+, -, *)
  // The trie matches greedily, so += will be matched over +
  // We set them as binary operators with additive/multiplicative precedence
  // and handle assignment in parse_assignment when the token length is 2

  // Basic arithmetic operators
  // We need to check if there are separate tokens for basic +, -, *, /
  // For now, let's assume the lexer generates the right tokens

  // Basic arithmetic at the right precedence levels
  // Addition and subtraction at PREC_ADDITIVE
  // Note: We might need to map these to different tokens than the assignment
  // versions

  // Multiplication and division at PREC_MULTIPLICATIVE
  rules[OP_DIV].infix = parse_binary;
  rules[OP_DIV].precedence = PREC_MULTIPLICATIVE;

  // We need to find what tokens are generated for basic +, -, * operators
  // The lexer might be generating compound tokens only

  // PREC_LOGICAL (3) - logical operators
  // Note: && and || would be here if they were separate tokens

  // PREC_COMBINED_ASSIGN (4) - complex assignment operators
  rules[OP_LSHIFT_ASSIGN].infix = parse_assignment;
  rules[OP_LSHIFT_ASSIGN].precedence = PREC_COMBINED_ASSIGN;
  rules[OP_CAS].infix = parse_ternary;
  rules[OP_CAS].precedence = PREC_COMBINED_ASSIGN;
  rules[OP_SWAP].infix = parse_binary;
  rules[OP_SWAP].precedence = PREC_COMBINED_ASSIGN;

  // PREC_COMPARISON (5) - comparison operators
  rules[OP_LT].infix = parse_binary;
  rules[OP_LT].precedence = PREC_COMPARISON;
  rules[OP_LE].infix = parse_binary;
  rules[OP_LE].precedence = PREC_COMPARISON;
  rules[OP_GT].infix = parse_binary;
  rules[OP_GT].precedence = PREC_COMPARISON;
  rules[OP_GE].infix = parse_binary;
  rules[OP_GE].precedence = PREC_COMPARISON;
  rules[OP_EQ].infix = parse_binary;
  rules[OP_EQ].precedence = PREC_COMPARISON;
  rules[OP_NE].infix = parse_binary;
  rules[OP_NE].precedence = PREC_COMPARISON;
  rules[OP_PATTERN_NE].infix = parse_binary;
  rules[OP_PATTERN_NE].precedence = PREC_COMPARISON;
  rules[OP_OVERLAP_TEST].infix = parse_binary;
  rules[OP_OVERLAP_TEST].precedence = PREC_COMPARISON;

  // PREC_BITWISE (6) - bitwise operators
  rules[OP_BITFIELD_EXTRACT].infix = parse_binary;
  rules[OP_BITFIELD_EXTRACT].precedence = PREC_BITWISE;
  rules[OP_BITWISE_ROTATE_XOR].infix = parse_binary;
  rules[OP_BITWISE_ROTATE_XOR].precedence = PREC_BITWISE;
  rules[OP_PUSH_INTO].infix = parse_binary;
  rules[OP_PUSH_INTO].precedence = PREC_BITWISE;

  // PREC_ADDITIVE (7) - addition and subtraction
  rules[OP_ADD_ASSIGN].infix = parse_binary;
  rules[OP_ADD_ASSIGN].precedence = PREC_ADDITIVE;
  rules[OP_SUB_ASSIGN].infix = parse_binary;
  rules[OP_SUB_ASSIGN].precedence = PREC_ADDITIVE;
  rules[OP_PUSH_INTO].infix = parse_binary;
  rules[OP_PUSH_INTO].precedence = PREC_ADDITIVE;
  rules[OP_POP_FROM].infix = parse_binary;
  rules[OP_POP_FROM].precedence = PREC_ADDITIVE;

  // Add basic arithmetic if they exist as separate tokens
  // We need to check what tokens are actually generated for +, -, *, /

  // PREC_MULTIPLICATIVE (8) - multiplication, division, modulo
  rules[OP_MUL_ASSIGN].infix = parse_binary;
  rules[OP_MUL_ASSIGN].precedence = PREC_MULTIPLICATIVE;
  rules[OP_DIV].infix = parse_binary;
  rules[OP_DIV].precedence = PREC_MULTIPLICATIVE;
  rules[OP_INT_DIV].infix = parse_binary;
  rules[OP_INT_DIV].precedence = PREC_MULTIPLICATIVE;
  rules[OP_FAST_RECIP].prefix = parse_unary;
  rules[OP_FAST_RECIP].precedence = PREC_MULTIPLICATIVE;
  rules[OP_MOD_DIVISOR].infix = parse_binary;
  rules[OP_MOD_DIVISOR].precedence = PREC_MULTIPLICATIVE;
  rules[OP_SIMD_DIV].infix = parse_binary;
  rules[OP_SIMD_DIV].precedence = PREC_MULTIPLICATIVE;
  rules[OP_PARALLEL_DIV].infix = parse_binary;
  rules[OP_PARALLEL_DIV].precedence = PREC_MULTIPLICATIVE;

  // PREC_SHIFT_ROTATE (9) - shift and rotate operations
  rules[OP_LSHIFT].infix = parse_binary;
  rules[OP_LSHIFT].precedence = PREC_SHIFT_ROTATE;
  rules[OP_RSHIFT].infix = parse_binary;
  rules[OP_RSHIFT].precedence = PREC_SHIFT_ROTATE;
  rules[OP_LOGICAL_RSHIFT].infix = parse_binary;
  rules[OP_LOGICAL_RSHIFT].precedence = PREC_SHIFT_ROTATE;
  rules[OP_ROTATE_LEFT].infix = parse_binary;
  rules[OP_ROTATE_LEFT].precedence = PREC_SHIFT_ROTATE;
  rules[OP_ROTATE_RIGHT].infix = parse_binary;
  rules[OP_ROTATE_RIGHT].precedence = PREC_SHIFT_ROTATE;
  rules[OP_SLICE_START].infix = parse_binary;
  rules[OP_SLICE_START].precedence = PREC_SHIFT_ROTATE;
  rules[OP_SLICE_END].infix = parse_binary;
  rules[OP_SLICE_END].precedence = PREC_SHIFT_ROTATE;
  rules[OP_SLICE_RANGE].infix = parse_ternary;
  rules[OP_SLICE_RANGE].precedence = PREC_SHIFT_ROTATE;

  // PREC_FIELD_LAYOUT (10) - field access and layout operations
  rules[TOK_DOT].infix = parse_binary;
  rules[TOK_DOT].precedence = PREC_FIELD_LAYOUT;
  // TOK_COLON is used for tuple/multi-value expressions like 0:1:2
  rules[TOK_COLON].infix = parse_binary;
  rules[TOK_COLON].precedence =
      PREC_SEQUENCE; // Low precedence for tuple construction
  rules[OP_LAYOUT_ACCESS].infix = parse_binary;
  rules[OP_LAYOUT_ACCESS].precedence = PREC_FIELD_LAYOUT;
  rules[OP_REVERSE_LAYOUT].infix = parse_binary;
  rules[OP_REVERSE_LAYOUT].precedence = PREC_FIELD_LAYOUT;

  // Add basic -> operator if it exists as a separate token
  // The issue might be that -> is a different token than ->>
  // We need to check what token is generated for ->

  // Syscall operators as binary infix operators
  rules[OP_WRITE_SYSCALL].infix = parse_binary;
  rules[OP_WRITE_SYSCALL].precedence = PREC_ADDITIVE;
  rules[OP_READ_SYSCALL].infix = parse_binary;
  rules[OP_READ_SYSCALL].precedence = PREC_ADDITIVE;

  // PREC_UNARY (11) - unary operators
  rules[OP_ATOMIC_READ].prefix = parse_unary;
  rules[OP_ATOMIC_READ].precedence = PREC_UNARY;
  rules[OP_ATOMIC_XOR].prefix = parse_unary;
  rules[OP_ATOMIC_XOR].precedence = PREC_UNARY;

  // Memory allocation operators
  rules[OP_ALLOCATE].prefix = parse_unary;
  rules[OP_ALLOCATE].precedence = PREC_UNARY;
  rules[OP_DEALLOCATE].prefix = parse_unary;
  rules[OP_DEALLOCATE].precedence = PREC_UNARY;
  rules[OP_STACK_ALLOC].prefix = parse_unary;
  rules[OP_STACK_ALLOC].precedence = PREC_UNARY;

  // Atomic operations - !! should be infix (postfix) but parsed as unary
  // postfix
  rules[OP_ATOMIC_WRITE].infix = parse_postfix;
  rules[OP_ATOMIC_WRITE].precedence = PREC_FIELD_LAYOUT;
  rules[OP_ATOMIC_TRIPLE].infix = parse_ternary;
  rules[OP_ATOMIC_TRIPLE].precedence = PREC_UNARY;
  rules[OP_ATOMIC_FETCH_ADD].infix = parse_binary;
  rules[OP_ATOMIC_FETCH_ADD].precedence = PREC_UNARY;

  // Remove duplicate syscall rules - they're already defined above as binary
  // operators
  rules[OP_RAW_SYSCALL].prefix = parse_unary;
  rules[OP_RAW_SYSCALL].precedence = PREC_UNARY;

  // Inline assembly operator (prefix only, no infix)
  rules[OP_INLINE_ASM].prefix = parse_unary;
  rules[OP_INLINE_ASM].precedence = PREC_NONE;  // No infix precedence - prefix only

  // Conditional operator - the ? token should be OP_CONDITIONAL
  rules[OP_CONDITIONAL].infix = parse_ternary;
  rules[OP_CONDITIONAL].precedence = PREC_LOGICAL;

  // Function definition operator (context-dependent)
  rules[OP_FUNCTION_DEF].infix = parse_binary;
  rules[OP_FUNCTION_DEF].precedence = PREC_COMBINED_ASSIGN;

  rules_initialized = true;
}

// Get parse rule for token type
const ParseRule *get_rule(TokenKind type) {
  init_parse_rules();
  if (type >= 0 && type < TOK_COUNT) {
    return &rules[type];
  }
  return &rules[TOK_ERROR];
}

// Core Pratt parser implementation
Expr *parse_precedence(Parser *parser, Precedence precedence) {
  parser_advance(parser);

  const ParseRule *prefix_rule = get_rule(parser->previous.kind);
  if (prefix_rule->prefix == NULL) {
    error_at_previous(parser, "Expected expression");
    return NULL;
  }

  Expr *expr = prefix_rule->prefix(parser);
  if (expr == NULL) {
    return NULL;
  }

  while (precedence <= get_rule(parser->current.kind)->precedence) {
    parser_advance(parser);
    const ParseRule *infix_rule = get_rule(parser->previous.kind);
    if (infix_rule->infix == NULL) {
      break;
    }
    expr = infix_rule->infix(parser, expr);
    if (expr == NULL) {
      return NULL;
    }
  }

  return expr;
}

// Main expression parsing entry point
Expr *parse_expression(Parser *parser) {
  return parse_precedence(parser, PREC_SEQUENCE);
}

// Parse literal values
Expr *parse_literal(Parser *parser) {
  Expr *expr = allocate_expr(EXPR_LITERAL);
  if (!expr)
    return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  switch (parser->previous.kind) {
  case TOK_INTEGER:
    // Parse integer from token text - auto-detect base (0x=hex, 0b=binary, 0o=octal)
    {
      const char* start = parser->previous.start;
      size_t len = parser->previous.length;
      int base = 10;
      
      // Detect base
      if (len > 2 && start[0] == '0') {
        if (start[1] == 'x' || start[1] == 'X') {
          base = 16;
          start += 2;
          len -= 2;
        } else if (start[1] == 'b' || start[1] == 'B') {
          base = 2;
          start += 2;
          len -= 2;
        } else if (start[1] == 'o' || start[1] == 'O') {
          base = 8;
          start += 2;
          len -= 2;
        }
      }
      
      // Check if value fits in 64 bits by trying strtoll first
      char *endptr;
      errno = 0;
      long long val64 = strtoll(parser->previous.start, &endptr, 0);
      
      // If no overflow and parsed successfully, use 64-bit integer
      if (errno != ERANGE && endptr == parser->previous.start + parser->previous.length) {
        expr->data.literal.type = LIT_INTEGER;
        expr->data.literal.value.integer = val64;
      } else {
        // Value is too large for 64 bits - parse using limb-based arithmetic
        // Supports up to 1024 bits (16 x 64-bit limbs)
        errno = 0;
        
        // Initialize limbs to zero
        uint64_t limbs[16] = {0};
        uint8_t num_limbs = 1;
        bool overflow = false;
        
        for (size_t i = 0; i < len && !overflow; i++) {
          char c = start[i];
          int digit;
          
          if (c >= '0' && c <= '9') {
            digit = c - '0';
          } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
          } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
          } else if (c == '_') {
            continue; // Skip underscores in numbers
          } else {
            break; // End of number
          }
          
          if (digit >= base) {
            break;
          }
          
          // Multiply all limbs by base and add digit
          // limbs = limbs * base + digit
          uint64_t carry = digit;
          for (uint8_t j = 0; j < num_limbs; j++) {
            // Multiply limb by base and add carry
            __uint128_t product = (__uint128_t)limbs[j] * base + carry;
            limbs[j] = (uint64_t)product;
            carry = (uint64_t)(product >> 64);
          }
          
          // Handle carry overflow into new limbs
          while (carry > 0) {
            if (num_limbs >= 16) {
              overflow = true;
              break;
            }
            limbs[num_limbs++] = carry;
            carry = 0;
          }
        }
        
        if (overflow) {
          error_at_previous(parser, "Integer literal too large (max 1024 bits)");
          free_expr(expr);
          return NULL;
        }
        
        // Store as bigint with limb-based storage
        expr->data.literal.type = LIT_BIGINT;
        for (uint8_t j = 0; j < 16; j++) {
          expr->data.literal.value.bigint.limbs[j] = (j < num_limbs) ? limbs[j] : 0;
        }
        expr->data.literal.value.bigint.num_limbs = num_limbs;
        expr->data.literal.value.bigint.is_negative = false;
      }
    }
    break;
  case TOK_FLOAT:
    expr->data.literal.type = LIT_FLOAT;
    // Parse float from token text
    {
      char *endptr;
      double val = strtod(parser->previous.start, &endptr);
      expr->data.literal.value.floating = val;
    }
    break;
  case TOK_STRING:
    expr->data.literal.type = LIT_STRING;
    // Use the pre-processed string from the lexer
    if (parser->previous.value.string) {
      size_t len = parser->previous.length;
      char *str = malloc(len + 1);
      if (str) {
        memcpy(str, parser->previous.value.string, len);
        str[len] = '\0';
        expr->data.literal.value.string = str;
      } else {
        expr->data.literal.value.string = NULL;
      }
    } else {
      expr->data.literal.value.string = NULL;
    }
    break;
  case TOK_CHAR:
    expr->data.literal.type = LIT_CHARACTER;
    // Extract character (skip quotes)
    expr->data.literal.value.character = parser->previous.start[1];
    break;
  default:
    free_expr(expr);
    error_at_previous(parser, "Invalid literal");
    return NULL;
  }

  return expr;
}

// Parse identifier (including keywords used as identifiers)
Expr *parse_identifier(Parser *parser) {
  Expr *expr = allocate_expr(EXPR_IDENTIFIER);
  if (!expr)
    return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  size_t name_len = parser->previous.length;
  expr->data.identifier = malloc(name_len + 1);
  if (!expr->data.identifier) {
    free_expr(expr);
    return NULL;
  }

  strncpy(expr->data.identifier, parser->previous.start, name_len);
  expr->data.identifier[name_len] = '\0';

  return expr;
}

// Parse binary expressions
Expr *parse_binary(Parser *parser, Expr *left) {
  TokenKind operator_type = parser->previous.kind;
  size_t operator_length =
      parser->previous.length; // Save length before parsing right side
  const ParseRule *rule = get_rule(operator_type);

  // Handle context-aware disambiguation for <=> operator
  if (operator_type == OP_CAS) {
    TokenKind disambiguated = disambiguate_cas_vs_function(parser);
    if (disambiguated == OP_FUNCTION_DEF) {
      // This is a function definition, not CAS
      return parse_function_definition(parser, left);
    }
    operator_type = disambiguated;
  }

  // Special case: -> (length 2) is used for compact if syntax, not as binary
  // operator Check if the next token is a statement keyword (ret, break,
  // continue, etc.) If so, don't parse it as binary expression - return left
  // and let if statement handle it
  if (operator_type == OP_LAYOUT_ACCESS && operator_length == 2) {
    // Check if next token is a statement keyword
    if (parser->current.kind == KW_RET || parser->current.kind == KW_HALT ||
        parser->current.kind == KW_BREAK ||
        parser->current.kind == KW_CONTINUE || parser->current.kind == KW_IF ||
        parser->current.kind == KW_LOOP || parser->current.kind == KW_WHILE ||
        parser->current.kind == KW_LET || parser->current.kind == KW_CONST) {
      // This is compact if/loop syntax, not a binary expression
      // Don't consume the statement keyword, just return left
      // The -> is already consumed, so parse_if_statement won't see it
      // We need to somehow signal that -> was seen
      //
      // Actually, the problem is that parse_if_statement won't see the ->
      // because we already consumed it. We need to create a binary node
      // with a placeholder right side, or use a different approach.
      //
      // Better solution: create a binary node with NULL right side as a marker
      Expr *expr = allocate_expr(EXPR_BINARY);
      if (!expr) {
        free_expr(left);
        return NULL;
      }

      expr->line = left->line;
      expr->column = left->column;
      expr->data.binary.op = operator_type;
      expr->data.binary.left = left;
      expr->data.binary.right = NULL; // Marker for compact syntax

      return expr;
    }
    // Not a statement keyword, so this might be ->> or -> with an expression
    // Continue to parse normally
  }

  Expr *right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
  if (!right) {
    free_expr(left);
    return NULL;
  }

  // Check if this is a special operator that needs a specific AST node type
  if (is_memory_operator(operator_type)) {
    // Create memory operation node
    Expr *expr = allocate_expr(EXPR_MEMORY_OP);
    if (!expr) {
      free_expr(left);
      free_expr(right);
      return NULL;
    }

    expr->line = left->line;
    expr->column = left->column;

    // Determine memory operation type
    if (operator_type == OP_LAYOUT_ACCESS ||
        operator_type == OP_REVERSE_LAYOUT) {
      expr->data.memory_op.op = MEM_LAYOUT_ACCESS;
    } else {
      expr->data.memory_op.op = MEM_ALLOCATE; // Default
    }

    expr->data.memory_op.operands = malloc(2 * sizeof(Expr *));
    if (!expr->data.memory_op.operands) {
      free_expr(left);
      free_expr(right);
      free(expr);
      return NULL;
    }
    expr->data.memory_op.operands[0] = left;
    expr->data.memory_op.operands[1] = right;
    expr->data.memory_op.operand_count = 2;

    return expr;
  } else if (is_atomic_operator(operator_type)) {
    // Create atomic operation node
    Expr *expr = allocate_expr(EXPR_ATOMIC_OP);
    if (!expr) {
      free_expr(left);
      free_expr(right);
      return NULL;
    }

    expr->line = left->line;
    expr->column = left->column;

    // Determine atomic operation type
    if (operator_type == OP_CAS) {
      expr->data.atomic_op.op = ATOMIC_CAS;
    } else if (operator_type == OP_SWAP) {
      expr->data.atomic_op.op = ATOMIC_SWAP;
    } else if (operator_type == OP_ATOMIC_FETCH_ADD) {
      expr->data.atomic_op.op = ATOMIC_FETCH_ADD;
    } else {
      expr->data.atomic_op.op = ATOMIC_READ; // Default
    }

    expr->data.atomic_op.operands = malloc(2 * sizeof(Expr *));
    if (!expr->data.atomic_op.operands) {
      free_expr(left);
      free_expr(right);
      free(expr);
      return NULL;
    }
    expr->data.atomic_op.operands[0] = left;
    expr->data.atomic_op.operands[1] = right;
    expr->data.atomic_op.operand_count = 2;

    return expr;
  } else if (is_syscall_operator(operator_type)) {
    // Create syscall operation node for: fd $/ buffer, length
    // left = fd, right = buffer, need to parse length after comma
    Expr *expr = allocate_expr(EXPR_SYSCALL_OP);
    if (!expr) {
      free_expr(left);
      free_expr(right);
      return NULL;
    }

    expr->line = left->line;
    expr->column = left->column;

    // Determine syscall type
    if (operator_type == OP_WRITE_SYSCALL) {
      expr->data.syscall_op.syscall_type = SYSCALL_WRITE;
    } else if (operator_type == OP_READ_SYSCALL) {
      expr->data.syscall_op.syscall_type = SYSCALL_READ;
    } else {
      expr->data.syscall_op.syscall_type = SYSCALL_RAW; // Default
    }

    // Check for comma to get the length argument
    Expr *length = NULL;
    if (parser_match(parser, TOK_COMMA)) {
      length = parse_precedence(parser, PREC_ADDITIVE);
      if (!length) {
        free_expr(left);
        free_expr(right);
        free(expr);
        return NULL;
      }
    }

    // Allocate args array: fd, buffer, length (3 args if length present, 2 otherwise)
    size_t arg_count = length ? 3 : 2;
    expr->data.syscall_op.args = malloc(arg_count * sizeof(Expr *));
    if (!expr->data.syscall_op.args) {
      free_expr(left);
      free_expr(right);
      if (length) free_expr(length);
      free(expr);
      return NULL;
    }
    expr->data.syscall_op.args[0] = left;   // fd
    expr->data.syscall_op.args[1] = right;  // buffer
    if (length) {
      expr->data.syscall_op.args[2] = length; // length
    }
    expr->data.syscall_op.arg_count = arg_count;
    expr->data.syscall_op.syscall_num = NULL;

    return expr;
  }

  // Check if this is actually an assignment operator (+=, -=, *=)
  // These share tokens with binary operators (+, -, *)
  // If the operator token length is 2, it's an assignment
  if ((operator_type == OP_ADD_ASSIGN || operator_type == OP_SUB_ASSIGN ||
       operator_type == OP_MUL_ASSIGN) &&
      operator_length == 2) {
    // This is an assignment expression
    Expr *expr = allocate_expr(EXPR_ASSIGNMENT);
    if (!expr) {
      free_expr(left);
      free_expr(right);
      return NULL;
    }

    expr->line = left->line;
    expr->column = left->column;
    expr->data.assignment.target = left;
    expr->data.assignment.value = right;
    expr->data.assignment.op = operator_type;

    return expr;
  }

  // Regular binary expression
  Expr *expr = allocate_expr(EXPR_BINARY);
  if (!expr) {
    free_expr(left);
    free_expr(right);
    return NULL;
  }

  expr->line = left->line;
  expr->column = left->column;
  expr->data.binary.op = operator_type;
  expr->data.binary.left = left;
  expr->data.binary.right = right;

  return expr;
}

// Parse unary expressions
Expr *parse_unary(Parser *parser) {
  TokenKind operator_type = parser->previous.kind;

  // Handle print> operator (token 91 = OP_STACK_ALLOC is reused by lexer for print>)
  // Check this BEFORE memory operators since OP_STACK_ALLOC is in is_memory_operator
  if (operator_type == 91 || operator_type == OP_PRINT_COMPACT || 
      operator_type == OP_FORMAT_PRINT) {
    // Parse the operand
    Expr *operand = parse_precedence(parser, PREC_PARENTHESES);
    if (!operand) {
      return NULL;
    }

    Expr *expr = allocate_expr(EXPR_UNARY);
    if (!expr) {
      free_expr(operand);
      return NULL;
    }

    expr->line = operand->line;
    expr->column = operand->column;
    expr->data.unary.op = operator_type;
    expr->data.unary.operand = operand;

    return expr;
  }

  // Check if this is a memory operation, atomic operation, syscall, or inline asm
  if (is_memory_operator(operator_type)) {
    return parse_memory_operation(parser, operator_type);
  } else if (is_atomic_operator(operator_type)) {
    return parse_atomic_operation(parser, operator_type);
  } else if (is_syscall_operator(operator_type)) {
    return parse_syscall_operation(parser, operator_type);
  } else if (operator_type == OP_INLINE_ASM) {
    return parse_inline_asm(parser);
  }

  // Parse the operand with the correct precedence
  // Use PREC_PARENTHESES to get the highest precedence for unary operands
  Expr *operand = parse_precedence(parser, PREC_PARENTHESES);
  if (!operand) {
    return NULL;
  }

  Expr *expr = allocate_expr(EXPR_UNARY);
  if (!expr) {
    free_expr(operand);
    return NULL;
  }

  expr->line = operand->line;
  expr->column = operand->column;
  expr->data.unary.op = operator_type;
  expr->data.unary.operand = operand;

  return expr;
}

// Parse ternary expressions (for CAS, conditional, etc.)
Expr *parse_ternary(Parser *parser, Expr *first) {
  TokenKind operator_type = parser->previous.kind;

  // Handle context-aware disambiguation
  if (operator_type == OP_CAS) {
    push_context(parser, CONTEXT_ATOMIC_OP);
  }

  // Handle different ternary operators differently
  Expr *second, *third;

  if (operator_type == OP_CONDITIONAL) {
    // For conditional: a ? b : c
    // Parse the "then" expression with higher precedence to stop at ':'
    second = parse_precedence(parser, PREC_COMBINED_ASSIGN);
    if (!second) {
      free_expr(first);
      return NULL;
    }

    // Expect and consume the ':'
    if (!parser_match(parser, TOK_COLON)) {
      error_at_current(parser, "Expected ':' in conditional expression");
      free_expr(first);
      free_expr(second);
      return NULL;
    }

    // Parse the "else" expression
    third = parse_precedence(parser, PREC_ASSIGNMENT);
    if (!third) {
      free_expr(first);
      free_expr(second);
      return NULL;
    }
  } else {
    // For other ternary operators like CAS
    second = parse_precedence(parser, PREC_ASSIGNMENT);
    if (!second) {
      free_expr(first);
      return NULL;
    }

    if (operator_type == OP_CAS) {
      if (!parser_match(parser, TOK_COMMA)) {
        // Binary CAS form: a <=> b
        pop_context(parser);

        Expr *expr = allocate_expr(EXPR_ATOMIC_OP);
        if (!expr) {
          free_expr(first);
          free_expr(second);
          return NULL;
        }

        expr->line = first->line;
        expr->column = first->column;
        expr->data.atomic_op.op = ATOMIC_CAS;
        expr->data.atomic_op.operands = malloc(2 * sizeof(Expr *));
        if (!expr->data.atomic_op.operands) {
          free_expr(first);
          free_expr(second);
          free(expr);
          return NULL;
        }
        expr->data.atomic_op.operands[0] = first;
        expr->data.atomic_op.operands[1] = second;
        expr->data.atomic_op.operand_count = 2;

        return expr;
      }
    }

    third = parse_precedence(parser, PREC_ASSIGNMENT);
    if (!third) {
      free_expr(first);
      free_expr(second);
      return NULL;
    }
  }

  if (operator_type == OP_CAS) {
    pop_context(parser);
  }

  Expr *expr = allocate_expr(EXPR_TERNARY);
  if (!expr) {
    free_expr(first);
    free_expr(second);
    free_expr(third);
    return NULL;
  }

  expr->line = first->line;
  expr->column = first->column;
  expr->data.ternary.op = operator_type;
  expr->data.ternary.first = first;
  expr->data.ternary.second = second;
  expr->data.ternary.third = third;

  return expr;
}

// Parse function calls
Expr *parse_call(Parser *parser, Expr *callee) {
  Expr *expr = allocate_expr(EXPR_CALL);
  if (!expr) {
    free_expr(callee);
    return NULL;
  }

  expr->line = callee->line;
  expr->column = callee->column;
  expr->data.call.function = callee;
  expr->data.call.args = NULL;
  expr->data.call.arg_count = 0;

  // Parse argument list
  if (!parser_check(parser, TOK_RPAREN)) {
    size_t capacity = 4;
    expr->data.call.args = malloc(capacity * sizeof(Expr *));
    if (!expr->data.call.args) {
      free_expr(expr);
      return NULL;
    }

    do {
      if (expr->data.call.arg_count >= capacity) {
        capacity *= 2;
        Expr **new_args =
            realloc(expr->data.call.args, capacity * sizeof(Expr *));
        if (!new_args) {
          free_expr(expr);
          return NULL;
        }
        expr->data.call.args = new_args;
      }

      // Use PREC_ASSIGNMENT to stop before comma operator
      // This ensures each argument is parsed separately
      Expr *arg = parse_precedence(parser, PREC_ASSIGNMENT);
      if (!arg) {
        free_expr(expr);
        return NULL;
      }

      expr->data.call.args[expr->data.call.arg_count++] = arg;
    } while (parser_match(parser, TOK_COMMA));
  }

  consume(parser, TOK_RPAREN, "Expected ')' after arguments");
  return expr;
}

// Parse array/pointer indexing: ptr[index]
Expr *parse_index(Parser *parser, Expr *base) {
  Expr *expr = allocate_expr(EXPR_INDEX);
  if (!expr) {
    free_expr(base);
    return NULL;
  }

  expr->line = base->line;
  expr->column = base->column;
  expr->data.index.base = base;
  expr->data.index.element_size = 0; // Will be inferred from type

  // Parse the index expression
  Expr *index = parse_expression(parser);
  if (!index) {
    free_expr(expr);
    return NULL;
  }
  expr->data.index.index = index;

  consume(parser, TOK_RBRACKET, "Expected ']' after index");
  return expr;
}

// Parse pointer dereference: @ptr (load) or ptr@ (store)
// Using @ as the dereference operator
Expr *parse_deref(Parser *parser) {
  Expr *expr = allocate_expr(EXPR_DEREF);
  if (!expr) {
    return NULL;
  }

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;
  expr->data.deref.is_write = false;
  expr->data.deref.value = NULL;

  // Parse the pointer expression
  Expr *pointer = parse_precedence(parser, PREC_UNARY);
  if (!pointer) {
    free_expr(expr);
    return NULL;
  }
  expr->data.deref.pointer = pointer;

  return expr;
}

// Parse postfix unary expressions (like ptr!!)
Expr *parse_postfix(Parser *parser, Expr *operand) {
  TokenKind operator_type = parser->previous.kind;

  // Check if this is an atomic operation (like ptr!!)
  if (is_atomic_operator(operator_type)) {
    Expr *expr = allocate_expr(EXPR_ATOMIC_OP);
    if (!expr) {
      free_expr(operand);
      return NULL;
    }

    expr->line = operand->line;
    expr->column = operand->column;
    expr->data.atomic_op.op = ATOMIC_WRITE; // !! is atomic write
    expr->data.atomic_op.operands = malloc(sizeof(Expr *));
    if (!expr->data.atomic_op.operands) {
      free_expr(operand);
      free(expr);
      return NULL;
    }
    expr->data.atomic_op.operands[0] = operand;
    expr->data.atomic_op.operand_count = 1;

    return expr;
  }

  // Regular postfix unary expression
  Expr *expr = allocate_expr(EXPR_UNARY);
  if (!expr) {
    free_expr(operand);
    return NULL;
  }

  expr->line = operand->line;
  expr->column = operand->column;
  expr->data.unary.op = operator_type;
  expr->data.unary.operand = operand;

  return expr;
}

// Parse assignment expressions
Expr *parse_assignment(Parser *parser, Expr *target) {
  TokenKind operator_type = parser->previous.kind;

  Expr *value = parse_precedence(parser, PREC_ASSIGNMENT);
  if (!value) {
    free_expr(target);
    return NULL;
  }

  Expr *expr = allocate_expr(EXPR_ASSIGNMENT);
  if (!expr) {
    free_expr(target);
    free_expr(value);
    return NULL;
  }

  expr->line = target->line;
  expr->column = target->column;
  expr->data.assignment.target = target;
  expr->data.assignment.value = value;
  expr->data.assignment.op = operator_type;

  return expr;
}

// Parse grouping (parentheses)
Expr *parse_grouping(Parser *parser) {
  Expr *expr = parse_expression(parser);
  consume(parser, TOK_RPAREN, "Expected ')' after expression");
  return expr;
}

// Parse function definition (when <=> is disambiguated as function)
Expr *parse_function_definition(Parser *parser, Expr *name_expr) {
  if (name_expr->type != EXPR_IDENTIFIER) {
    error_at_previous(parser, "Expected function name before <=>");
    free_expr(name_expr);
    return NULL;
  }

  push_context(parser, CONTEXT_FUNCTION_DEF);

  // Expect 'fn' keyword after <=>
  if (!parser_match(parser, KW_FN)) {
    error_at_current(parser, "Expected 'fn' after <=> in function definition");
    free_expr(name_expr);
    pop_context(parser);
    return NULL;
  }

  consume(parser, TOK_LPAREN, "Expected '(' after 'fn'");

  // Parse parameters
  Parameter *params = NULL;
  size_t param_count = 0;
  size_t param_capacity = 0;

  if (!parser_check(parser, TOK_RPAREN)) {
    param_capacity = 4;
    params = malloc(param_capacity * sizeof(Parameter));
    if (!params) {
      free_expr(name_expr);
      pop_context(parser);
      return NULL;
    }

    do {
      if (param_count >= param_capacity) {
        param_capacity *= 2;
        Parameter *new_params =
            realloc(params, param_capacity * sizeof(Parameter));
        if (!new_params) {
          free(params);
          free_expr(name_expr);
          pop_context(parser);
          return NULL;
        }
        params = new_params;
      }

      consume(parser, TOK_IDENTIFIER, "Expected parameter name");

      size_t name_len = parser->previous.length;
      params[param_count].name = malloc(name_len + 1);
      if (!params[param_count].name) {
        free(params);
        free_expr(name_expr);
        pop_context(parser);
        return NULL;
      }

      strncpy(params[param_count].name, parser->previous.start, name_len);
      params[param_count].name[name_len] = '\0';
      params[param_count].type = NULL; // Type inference for now

      param_count++;
    } while (parser_match(parser, TOK_COMMA));
  }

  consume(parser, TOK_RPAREN, "Expected ')' after parameters");

  // Parse function body
  consume(parser, TOK_LBRACE, "Expected '{' before function body");
  Block body = parse_block(parser);

  pop_context(parser);

  Expr *expr = allocate_expr(EXPR_FUNCTION_DEF);
  if (!expr) {
    free_expr(name_expr);
    free(params);
    return NULL;
  }

  expr->line = name_expr->line;
  expr->column = name_expr->column;
  expr->data.function_def.name = fcx_strdup(name_expr->data.identifier);
  expr->data.function_def.params = params;
  expr->data.function_def.param_count = param_count;
  expr->data.function_def.body = body;
  expr->data.function_def.is_compact = true; // <=> syntax

  free_expr(name_expr);
  return expr;
}

// Statement parsing functions
Stmt *parse_statement(Parser *parser) {
  // Check for compact conditional: ?(condition) -> statement
  if (parser_check(parser, OP_CONDITIONAL)) {
    return parse_compact_conditional_statement(parser);
  }

  // Module system keywords
  if (parser_match(parser, KW_PUB)) {
    return parse_pub_statement(parser);
  }
  if (parser_match(parser, KW_MOD)) {
    return parse_mod_statement(parser, false);
  }
  if (parser_match(parser, KW_USE)) {
    return parse_use_statement(parser, false);
  }

  if (parser_match(parser, KW_LET) || parser_match(parser, KW_CONST)) {
    return parse_let_statement(parser);
  }
  if (parser_match(parser, KW_FN)) {
    return parse_function_statement(parser);
  }
  if (parser_match(parser, KW_IF)) {
    return parse_if_statement(parser);
  }
  if (parser_match(parser, KW_LOOP) || parser_match(parser, KW_WHILE)) {
    return parse_loop_statement(parser);
  }
  if (parser_match(parser, KW_RET) || parser_match(parser, KW_HALT)) {
    return parse_return_statement(parser);
  }
  if (parser_match(parser, KW_BREAK) || parser_match(parser, KW_CONTINUE)) {
    return parse_break_continue_statement(parser);
  }

  return parse_expression_statement(parser);
}

Stmt *parse_function_statement(Parser *parser) {
  // Traditional function syntax: fn name(params) { body }
  consume(parser, TOK_IDENTIFIER, "Expected function name");

  size_t name_len = parser->previous.length;
  char *name = malloc(name_len + 1);
  if (!name)
    return NULL;

  strncpy(name, parser->previous.start, name_len);
  name[name_len] = '\0';

  consume(parser, TOK_LPAREN, "Expected '(' after function name");

  // Parse parameters
  Parameter *params = NULL;
  size_t param_count = 0;
  size_t param_capacity = 0;

  if (!parser_check(parser, TOK_RPAREN)) {
    param_capacity = 4;
    params = malloc(param_capacity * sizeof(Parameter));
    if (!params) {
      free(name);
      return NULL;
    }

    do {
      if (param_count >= param_capacity) {
        param_capacity *= 2;
        Parameter *new_params =
            realloc(params, param_capacity * sizeof(Parameter));
        if (!new_params) {
          for (size_t i = 0; i < param_count; i++) {
            free(params[i].name);
          }
          free(params);
          free(name);
          return NULL;
        }
        params = new_params;
      }

      consume(parser, TOK_IDENTIFIER, "Expected parameter name");

      size_t param_name_len = parser->previous.length;
      params[param_count].name = malloc(param_name_len + 1);
      if (!params[param_count].name) {
        for (size_t i = 0; i < param_count; i++) {
          free(params[i].name);
        }
        free(params);
        free(name);
        return NULL;
      }

      strncpy(params[param_count].name, parser->previous.start, param_name_len);
      params[param_count].name[param_name_len] = '\0';
      params[param_count].type = NULL; // Type inference for now

      param_count++;
    } while (parser_match(parser, TOK_COMMA));
  }

  consume(parser, TOK_RPAREN, "Expected ')' after parameters");

  // Parse optional return type: -> type
  // Check for -> operator (it's tokenized as an operator with kind 62)
  if (parser->current.start && parser->current.length >= 2 &&
      parser->current.start[0] == '-' && parser->current.start[1] == '>') {
    parser_advance(parser); // Skip ->
    // Parse the return type (can be identifier or keyword like i32, i64, etc.)
    // Including bigint types: i128, i256, i512, i1024, u128, u256, u512, u1024
    if (parser_check(parser, TOK_IDENTIFIER) || 
        parser_check(parser, KW_I8) || parser_check(parser, KW_I16) ||
        parser_check(parser, KW_I32) || parser_check(parser, KW_I64) ||
        parser_check(parser, KW_I128) || parser_check(parser, KW_I256) ||
        parser_check(parser, KW_I512) || parser_check(parser, KW_I1024) ||
        parser_check(parser, KW_U8) || parser_check(parser, KW_U16) ||
        parser_check(parser, KW_U32) || parser_check(parser, KW_U64) ||
        parser_check(parser, KW_U128) || parser_check(parser, KW_U256) ||
        parser_check(parser, KW_U512) || parser_check(parser, KW_U1024) ||
        parser_check(parser, KW_F32) || parser_check(parser, KW_F64)) {
      parser_advance(parser);
    }
  }

  // Expect opening brace for function body
  if (!parser_check(parser, TOK_LBRACE)) {
    error_at_current(parser, "Expected '{' before function body");
    return NULL;
  }
  parser_advance(parser); // consume {

  Block body = parse_block(parser);

  Stmt *stmt = allocate_stmt(STMT_FUNCTION);
  if (!stmt) {
    for (size_t i = 0; i < param_count; i++) {
      free(params[i].name);
    }
    free(params);
    free(name);
    return NULL;
  }

  stmt->data.function.name = name;
  stmt->data.function.params = params;
  stmt->data.function.param_count = param_count;
  stmt->data.function.return_type = NULL;
  stmt->data.function.body = body;
  stmt->data.function.verbosity = SYNTAX_VERBOSE;

  return stmt;
}

// Parse compact conditional statement: ?(condition) -> statement
Stmt *parse_compact_conditional_statement(Parser *parser) {
  consume(parser, OP_CONDITIONAL, "Expected '?' for compact conditional");
  consume(parser, TOK_LPAREN, "Expected '(' after '?'");

  Expr *condition = parse_expression(parser);
  if (!condition)
    return NULL;

  consume(parser, TOK_RPAREN, "Expected ')' after condition");
  consume(parser, OP_LAYOUT_ACCESS,
          "Expected '->' after condition"); // -> operator

  Stmt *then_stmt = parse_statement(parser);
  if (!then_stmt) {
    free_expr(condition);
    return NULL;
  }

  // Create block with single statement
  Block then_branch = {0};
  then_branch.capacity = 1;
  then_branch.statements = malloc(sizeof(Stmt *));
  if (!then_branch.statements) {
    free_expr(condition);
    free_stmt(then_stmt);
    return NULL;
  }
  then_branch.statements[0] = then_stmt;
  then_branch.count = 1;

  Stmt *stmt = allocate_stmt(STMT_IF);
  if (!stmt) {
    free_expr(condition);
    free_block(&then_branch);
    return NULL;
  }

  stmt->data.if_stmt.condition = condition;
  stmt->data.if_stmt.then_branch = then_branch;
  stmt->data.if_stmt.else_branch = (Block){0};
  stmt->data.if_stmt.is_compact = true;

  return stmt;
}

Stmt *parse_let_statement(Parser *parser) {
  bool is_const = (parser->previous.kind == KW_CONST);

  // Parse variable name
  consume(parser, TOK_IDENTIFIER, "Expected variable name");
  
  size_t name_len = parser->previous.length;
  char *name = malloc(name_len + 1);
  if (!name) return NULL;
  strncpy(name, parser->previous.start, name_len);
  name[name_len] = '\0';

  Type *type_annotation = NULL;
  Expr *initializer = NULL;

  // Check for type annotation: let x: i64 = ...
  // Or multi-variable: let a:b := ...
  if (parser_match(parser, TOK_COLON)) {
    // Check if next token is a type keyword
    if (parser_check(parser, KW_I8) || parser_check(parser, KW_I16) ||
        parser_check(parser, KW_I32) || parser_check(parser, KW_I64) ||
        parser_check(parser, KW_I128) || parser_check(parser, KW_I256) ||
        parser_check(parser, KW_I512) || parser_check(parser, KW_I1024) ||
        parser_check(parser, KW_U8) || parser_check(parser, KW_U16) ||
        parser_check(parser, KW_U32) || parser_check(parser, KW_U64) ||
        parser_check(parser, KW_U128) || parser_check(parser, KW_U256) ||
        parser_check(parser, KW_U512) || parser_check(parser, KW_U1024) ||
        parser_check(parser, KW_F32) || parser_check(parser, KW_F64) ||
        parser_check(parser, KW_PTR) || parser_check(parser, KW_RAWPTR)) {
      // This is a type annotation
      type_annotation = malloc(sizeof(Type));
      if (type_annotation) {
        // Map token to TypeKind
        switch (parser->current.kind) {
          case KW_I8: type_annotation->kind = TYPE_I8; break;
          case KW_I16: type_annotation->kind = TYPE_I16; break;
          case KW_I32: type_annotation->kind = TYPE_I32; break;
          case KW_I64: type_annotation->kind = TYPE_I64; break;
          case KW_I128: type_annotation->kind = TYPE_I128; break;
          case KW_I256: type_annotation->kind = TYPE_I256; break;
          case KW_I512: type_annotation->kind = TYPE_I512; break;
          case KW_I1024: type_annotation->kind = TYPE_I1024; break;
          case KW_U8: type_annotation->kind = TYPE_U8; break;
          case KW_U16: type_annotation->kind = TYPE_U16; break;
          case KW_U32: type_annotation->kind = TYPE_U32; break;
          case KW_U64: type_annotation->kind = TYPE_U64; break;
          case KW_U128: type_annotation->kind = TYPE_U128; break;
          case KW_U256: type_annotation->kind = TYPE_U256; break;
          case KW_U512: type_annotation->kind = TYPE_U512; break;
          case KW_U1024: type_annotation->kind = TYPE_U1024; break;
          case KW_F32: type_annotation->kind = TYPE_F32; break;
          case KW_F64: type_annotation->kind = TYPE_F64; break;
          case KW_PTR: type_annotation->kind = TYPE_PTR; break;
          case KW_RAWPTR: type_annotation->kind = TYPE_RAWPTR; break;
          default: type_annotation->kind = TYPE_I64; break;
        }
      }
      parser_advance(parser); // consume the type keyword
    } else {
      // This is multi-variable declaration: let a:b := ...
      // Parse remaining variable names
      char **names = malloc(8 * sizeof(char *));
      size_t name_count = 1;
      size_t name_capacity = 8;
      names[0] = name;
      
      do {
        consume(parser, TOK_IDENTIFIER, "Expected variable name");
        
        if (name_count >= name_capacity) {
          name_capacity *= 2;
          char **new_names = realloc(names, name_capacity * sizeof(char *));
          if (!new_names) {
            for (size_t i = 0; i < name_count; i++) free(names[i]);
            free(names);
            return NULL;
          }
          names = new_names;
        }
        
        size_t len = parser->previous.length;
        names[name_count] = malloc(len + 1);
        if (!names[name_count]) {
          for (size_t i = 0; i < name_count; i++) free(names[i]);
          free(names);
          return NULL;
        }
        strncpy(names[name_count], parser->previous.start, len);
        names[name_count][len] = '\0';
        name_count++;
      } while (parser_match(parser, TOK_COLON));
      
      // Parse initializer
      if (parser_match(parser, OP_ASSIGN_INFER) || parser_match(parser, OP_ASSIGN)) {
        initializer = parse_expression(parser);
      }
      
      // Semicolons are optional
      if (parser_check(parser, TOK_SEMICOLON)) parser_advance(parser);
      
      // Create multi-assignment statement
      Expr *multi_assign = allocate_expr(EXPR_MULTI_ASSIGN);
      if (!multi_assign) {
        for (size_t i = 0; i < name_count; i++) free(names[i]);
        free(names);
        free_expr(initializer);
        return NULL;
      }
      
      multi_assign->data.multi_assign.targets = malloc(name_count * sizeof(Expr *));
      for (size_t i = 0; i < name_count; i++) {
        Expr *id_expr = allocate_expr(EXPR_IDENTIFIER);
        id_expr->data.identifier = names[i];
        multi_assign->data.multi_assign.targets[i] = id_expr;
      }
      multi_assign->data.multi_assign.count = name_count;
      multi_assign->data.multi_assign.values = malloc(sizeof(Expr *));
      multi_assign->data.multi_assign.values[0] = initializer;
      
      free(names);
      
      Stmt *stmt = allocate_stmt(STMT_LET);
      stmt->data.let.name = fcx_strdup(multi_assign->data.multi_assign.targets[0]->data.identifier);
      stmt->data.let.type_annotation = NULL;
      stmt->data.let.initializer = multi_assign;
      stmt->data.let.is_const = is_const;
      return stmt;
    }
  }

  // Optional initializer: = or :=
  if (parser_match(parser, OP_ASSIGN_INFER) || parser_match(parser, OP_ASSIGN)) {
    initializer = parse_expression(parser);
    if (!initializer) {
      free(name);
      if (type_annotation) free(type_annotation);
      return NULL;
    }
  }

  // Semicolons are optional in FCx
  if (parser_check(parser, TOK_SEMICOLON)) {
    parser_advance(parser);
  }

  Stmt *stmt = allocate_stmt(STMT_LET);
  if (!stmt) {
    free(name);
    if (type_annotation) free(type_annotation);
    free_expr(initializer);
    return NULL;
  }

  stmt->data.let.name = name;
  stmt->data.let.type_annotation = type_annotation;
  stmt->data.let.initializer = initializer;
  stmt->data.let.is_const = is_const;

  return stmt;
}


Stmt *parse_if_statement(Parser *parser) {
  // Parse condition normally - parse_expression will consume everything
  // including -> if it's part of the expression
  Expr *condition = parse_expression(parser);
  if (!condition)
    return NULL;

  bool is_compact = false;

  // The issue: -> has higher precedence than comparison operators,
  // so "x > 0 -> ret" gets parsed as "x > (0 -> ret)" not "(x > 0) -> ret"
  // We need to extract the condition from this nested structure

  // Check if condition is a comparison with -> on the right side
  if (condition->type == EXPR_BINARY && (condition->data.binary.op == OP_LT ||
                                         condition->data.binary.op == OP_LE ||
                                         condition->data.binary.op == OP_GT ||
                                         condition->data.binary.op == OP_GE ||
                                         condition->data.binary.op == OP_EQ ||
                                         condition->data.binary.op == OP_NE)) {
    // Check if the right side is a binary expression with ->
    Expr *right = condition->data.binary.right;
    if (right && right->type == EXPR_BINARY &&
        right->data.binary.op == OP_LAYOUT_ACCESS &&
        right->data.binary.right == NULL) {
      // Found it! The structure is: comparison(left, arrow(value, NULL))
      // We need: comparison(left, value) as condition, then parse statement

      // Extract the value from the arrow expression
      Expr *value = right->data.binary.left;

      // Free the arrow wrapper
      free(right);

      // Update the comparison's right side
      condition->data.binary.right = value;

      is_compact = true;

      // Parse single statement for compact if
      Stmt *then_stmt = parse_statement(parser);
      if (!then_stmt) {
        free_expr(condition);
        return NULL;
      }

      // Create block with single statement
      Block then_branch = {0};
      then_branch.capacity = 1;
      then_branch.statements = malloc(sizeof(Stmt *));
      if (!then_branch.statements) {
        free_expr(condition);
        free_stmt(then_stmt);
        return NULL;
      }
      then_branch.statements[0] = then_stmt;
      then_branch.count = 1;

      Stmt *stmt = allocate_stmt(STMT_IF);
      if (!stmt) {
        free_expr(condition);
        free_block(&then_branch);
        return NULL;
      }

      stmt->data.if_stmt.condition = condition;
      stmt->data.if_stmt.then_branch = then_branch;
      stmt->data.if_stmt.else_branch = (Block){0};
      stmt->data.if_stmt.is_compact = true;

      return stmt;
    }
  }

  // Check if the condition accidentally consumed -> as a binary operator at top
  // level
  if (condition->type == EXPR_BINARY &&
      condition->data.binary.op == OP_LAYOUT_ACCESS &&
      condition->data.binary.right == NULL) {
    // This is compact if syntax with -> marker
    // Extract the left side as the actual condition
    Expr *actual_condition = condition->data.binary.left;

    // Free the binary wrapper but keep the left child
    free(condition);
    condition = actual_condition;

    is_compact = true;

    // Parse single statement for compact if
    Stmt *then_stmt = parse_statement(parser);
    if (!then_stmt) {
      free_expr(condition);
      return NULL;
    }

    // Create block with single statement
    Block then_branch = {0};
    then_branch.capacity = 1;
    then_branch.statements = malloc(sizeof(Stmt *));
    if (!then_branch.statements) {
      free_expr(condition);
      free_stmt(then_stmt);
      return NULL;
    }
    then_branch.statements[0] = then_stmt;
    then_branch.count = 1;

    Stmt *stmt = allocate_stmt(STMT_IF);
    if (!stmt) {
      free_expr(condition);
      free_block(&then_branch);
      return NULL;
    }

    stmt->data.if_stmt.condition = condition;
    stmt->data.if_stmt.then_branch = then_branch;
    stmt->data.if_stmt.else_branch = (Block){0};
    stmt->data.if_stmt.is_compact = true;

    return stmt;
  }

  // Check for compact syntax: if condition -> statement
  if (parser_match(parser, OP_LAYOUT_ACCESS)) { // -> operator
    is_compact = true;

    // Parse single statement for compact if
    Stmt *then_stmt = parse_statement(parser);
    if (!then_stmt) {
      free_expr(condition);
      return NULL;
    }

    // Create block with single statement
    Block then_branch = {0};
    then_branch.capacity = 1;
    then_branch.statements = malloc(sizeof(Stmt *));
    if (!then_branch.statements) {
      free_expr(condition);
      free_stmt(then_stmt);
      return NULL;
    }
    then_branch.statements[0] = then_stmt;
    then_branch.count = 1;

    Stmt *stmt = allocate_stmt(STMT_IF);
    if (!stmt) {
      free_expr(condition);
      free_block(&then_branch);
      return NULL;
    }

    stmt->data.if_stmt.condition = condition;
    stmt->data.if_stmt.then_branch = then_branch;
    stmt->data.if_stmt.else_branch = (Block){0};
    stmt->data.if_stmt.is_compact = true;

    return stmt;
  }

  // Traditional syntax: if condition { block }
  consume(parser, TOK_LBRACE, "Expected '{' after if condition");
  Block then_branch = parse_block(parser);

  Block else_branch = {0};
  if (parser_match(parser, KW_ELSE)) {
    consume(parser, TOK_LBRACE, "Expected '{' after else");
    else_branch = parse_block(parser);
  }

  Stmt *stmt = allocate_stmt(STMT_IF);
  if (!stmt) {
    free_expr(condition);
    return NULL;
  }

  stmt->data.if_stmt.condition = condition;
  stmt->data.if_stmt.then_branch = then_branch;
  stmt->data.if_stmt.else_branch = else_branch;
  stmt->data.if_stmt.is_compact = is_compact;

  return stmt;
}

Stmt *parse_loop_statement(Parser *parser) {
  LoopType loop_type = LOOP_TRADITIONAL;
  Expr *condition = NULL;
  Expr *iteration = NULL;

  if (parser->previous.kind == KW_WHILE) {
    loop_type = LOOP_WHILE;
    condition = parse_expression(parser);
    if (!condition)
      return NULL;
  } else if (parser->previous.kind == KW_LOOP) {
    // Check for compact loop syntax
    if (parser_check(parser, TOK_LPAREN)) {
      // Count loop: (n) << body
      parser_advance(parser);
      condition = parse_expression(parser);
      if (!condition)
        return NULL;

      consume(parser, TOK_RPAREN, "Expected ')' after loop count");
      consume(parser, OP_LSHIFT, "Expected '<<' after loop count");

      loop_type = LOOP_COUNT;
    } else if (parser_check(parser, TOK_IDENTIFIER)) {
      // Range loop: i </ n: body
      Token var_token = parser->current;
      parser_advance(parser);

      if (parser_match(parser, OP_SLICE_START)) { // </ operator
        // Create identifier expression for loop variable
        Expr *var_expr = allocate_expr(EXPR_IDENTIFIER);
        if (!var_expr)
          return NULL;

        size_t name_len = var_token.length;
        var_expr->data.identifier = malloc(name_len + 1);
        if (!var_expr->data.identifier) {
          free_expr(var_expr);
          return NULL;
        }
        strncpy(var_expr->data.identifier, var_token.start, name_len);
        var_expr->data.identifier[name_len] = '\0';

        condition = var_expr; // Store loop variable in condition field
        iteration = parse_expression(parser); // Parse bound expression
        if (!iteration) {
          free_expr(condition);
          return NULL;
        }

        consume(parser, TOK_COLON, "Expected ':' after range bound");
        loop_type = LOOP_RANGE;
      } else {
        // Put the identifier back and parse as traditional loop
        parser->current = var_token;
      }
    }
  }

  consume(parser, TOK_LBRACE, "Expected '{' before loop body");
  Block body = parse_block(parser);

  Stmt *stmt = allocate_stmt(STMT_LOOP);
  if (!stmt) {
    free_expr(condition);
    free_expr(iteration);
    return NULL;
  }

  stmt->data.loop.loop_type = loop_type;
  stmt->data.loop.condition = condition;
  stmt->data.loop.body = body;
  stmt->data.loop.iteration = iteration;

  return stmt;
}

Stmt *parse_return_statement(Parser *parser) {
  bool is_halt = (parser->previous.kind == KW_HALT);

  Expr *value = NULL;
  if (!parser_check(parser, TOK_SEMICOLON) && !parser_check(parser, TOK_RBRACE) && !parser_check(parser, TOK_EOF)) {
    value = parse_expression(parser);
    if (!value)
      return NULL;
  }

  // Semicolons are optional in FCx - consume if present, but don't require
  // Allow statements to end with newline, semicolon, or closing brace
  if (parser_check(parser, TOK_SEMICOLON)) {
    parser_advance(parser);
  }

  Stmt *stmt = allocate_stmt(is_halt ? STMT_HALT : STMT_RETURN);
  if (!stmt) {
    free_expr(value);
    return NULL;
  }

  stmt->data.return_value = value;
  return stmt;
}

Stmt *parse_break_continue_statement(Parser *parser) {
  bool is_break = (parser->previous.kind == KW_BREAK);

  // Semicolons are optional in FCx - consume if present, but don't require
  if (parser_check(parser, TOK_SEMICOLON)) {
    parser_advance(parser);
  }

  // Create proper break/continue statement
  Stmt *stmt = allocate_stmt(is_break ? STMT_BREAK : STMT_CONTINUE);
  if (!stmt)
    return NULL;

  stmt->line = parser->previous.line;
  stmt->column = parser->previous.column;
  return stmt;
}

Stmt *parse_expression_statement(Parser *parser) {
  Expr *expr = parse_expression(parser);
  if (!expr)
    return NULL;

  // Semicolons are optional in FCx - consume if present, but don't require
  // Allow statements to end with newline, semicolon, or closing brace
  if (parser_check(parser, TOK_SEMICOLON)) {
    parser_advance(parser);
  }

  Stmt *stmt = allocate_stmt(STMT_EXPRESSION);
  if (!stmt) {
    free_expr(expr);
    return NULL;
  }

  stmt->data.expression = expr;
  return stmt;
}

Block parse_block(Parser *parser) {
  Block block = {0};
  block.capacity = 8;
  block.statements = malloc(block.capacity * sizeof(Stmt *));
  if (!block.statements) {
    return block;
  }

  while (!parser_check(parser, TOK_RBRACE) && !parser_check(parser, TOK_EOF)) {
    if (block.count >= block.capacity) {
      block.capacity *= 2;
      Stmt **new_statements =
          realloc(block.statements, block.capacity * sizeof(Stmt *));
      if (!new_statements) {
        free_block(&block);
        block.statements = NULL;
        return block;
      }
      block.statements = new_statements;
    }

    Stmt *stmt = parse_statement(parser);
    if (!stmt) {
      if (parser->panic_mode) {
        synchronize(parser);
        continue;
      }
      break;
    }

    block.statements[block.count++] = stmt;
  }

  consume(parser, TOK_RBRACE, "Expected '}' after block");
  return block;
}

// Parse memory operations (mem>, >mem, stack>, @>, etc.)
Expr *parse_memory_operation(Parser *parser, TokenKind op) {
  Expr *expr = allocate_expr(EXPR_MEMORY_OP);
  if (!expr)
    return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  // Determine memory operation type
  if (op == OP_ALLOCATE) {
    expr->data.memory_op.op = MEM_ALLOCATE;
  } else if (op == OP_ARENA_ALLOC) {
    expr->data.memory_op.op = MEM_ARENA_ALLOC;
  } else if (op == OP_SLAB_ALLOC) {
    expr->data.memory_op.op = MEM_SLAB_ALLOC;
  } else if (op == OP_DEALLOCATE) {
    expr->data.memory_op.op = MEM_DEALLOCATE;
  } else if (op == OP_ARENA_FREE) {
    expr->data.memory_op.op = MEM_ARENA_RESET;
  } else if (op == OP_SLAB_FREE) {
    expr->data.memory_op.op = MEM_SLAB_FREE;
  } else if (op == OP_STACK_ALLOC) {
    expr->data.memory_op.op = MEM_STACK_ALLOC;
  } else if (op == OP_MMIO_MAP) {
    expr->data.memory_op.op = MEM_MMIO_MAP;
  } else if (op == OP_MMIO_UNMAP) {
    expr->data.memory_op.op = MEM_MMIO_UNMAP;
  } else if (op == OP_LAYOUT_ACCESS || op == OP_REVERSE_LAYOUT) {
    expr->data.memory_op.op = MEM_LAYOUT_ACCESS;
  } else if (op == OP_ALIGN_UP) {
    expr->data.memory_op.op = MEM_ALIGN_UP;
  } else if (op == OP_ALIGN_DOWN) {
    expr->data.memory_op.op = MEM_ALIGN_DOWN;
  } else if (op == OP_IS_ALIGNED) {
    expr->data.memory_op.op = MEM_IS_ALIGNED;
  } else if (op == OP_PREFETCH) {
    expr->data.memory_op.op = MEM_PREFETCH;
  } else if (op == OP_PREFETCH_W) {
    expr->data.memory_op.op = MEM_PREFETCH_WRITE;
  } else {
    expr->data.memory_op.op = MEM_ALLOCATE; // Default
  }

  // Allocate space for operands (max 3 for most operations)
  size_t capacity = 4;
  expr->data.memory_op.operands = malloc(capacity * sizeof(Expr *));
  if (!expr->data.memory_op.operands) {
    free_expr(expr);
    return NULL;
  }
  expr->data.memory_op.operand_count = 0;

  // Parse first operand
  Expr *first_operand = parse_precedence(parser, PREC_ASSIGNMENT);
  if (!first_operand) {
    free(expr->data.memory_op.operands);
    free(expr);
    return NULL;
  }
  expr->data.memory_op.operands[expr->data.memory_op.operand_count++] = first_operand;

  // Parse additional operands separated by commas
  while (parser_match(parser, TOK_COMMA)) {
    if (expr->data.memory_op.operand_count >= capacity) {
      capacity *= 2;
      Expr **new_operands = realloc(expr->data.memory_op.operands, capacity * sizeof(Expr *));
      if (!new_operands) {
        for (size_t i = 0; i < expr->data.memory_op.operand_count; i++) {
          free_expr(expr->data.memory_op.operands[i]);
        }
        free(expr->data.memory_op.operands);
        free(expr);
        return NULL;
      }
      expr->data.memory_op.operands = new_operands;
    }

    Expr *operand = parse_precedence(parser, PREC_ASSIGNMENT);
    if (!operand) {
      for (size_t i = 0; i < expr->data.memory_op.operand_count; i++) {
        free_expr(expr->data.memory_op.operands[i]);
      }
      free(expr->data.memory_op.operands);
      free(expr);
      return NULL;
    }
    expr->data.memory_op.operands[expr->data.memory_op.operand_count++] = operand;
  }

  return expr;
}

// Parse atomic operations (!, !!, <=>, etc.)
Expr *parse_atomic_operation(Parser *parser, TokenKind op) {
  Expr *expr = allocate_expr(EXPR_ATOMIC_OP);
  if (!expr)
    return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  // Determine atomic operation type
  if (op == OP_ATOMIC_READ) {
    expr->data.atomic_op.op = ATOMIC_READ;
  } else if (op == OP_ATOMIC_WRITE) {
    expr->data.atomic_op.op = ATOMIC_WRITE;
  } else if (op == OP_CAS) {
    expr->data.atomic_op.op = ATOMIC_CAS;
  } else if (op == OP_SWAP) {
    expr->data.atomic_op.op = ATOMIC_SWAP;
  } else if (op == OP_ATOMIC_FETCH_ADD) {
    expr->data.atomic_op.op = ATOMIC_FETCH_ADD;
  } else {
    expr->data.atomic_op.op = ATOMIC_READ; // Default
  }

  // Parse operands based on operation type
  size_t operand_count = 1;
  if (op == OP_ATOMIC_WRITE || op == OP_SWAP || op == OP_ATOMIC_FETCH_ADD) {
    operand_count = 2; // target and value
  } else if (op == OP_CAS) {
    operand_count = 3; // target, expected, desired
  }

  expr->data.atomic_op.operands = malloc(operand_count * sizeof(Expr *));
  if (!expr->data.atomic_op.operands) {
    free_expr(expr);
    return NULL;
  }

  for (size_t i = 0; i < operand_count; i++) {
    expr->data.atomic_op.operands[i] = parse_precedence(parser, PREC_UNARY);
    if (!expr->data.atomic_op.operands[i]) {
      for (size_t j = 0; j < i; j++) {
        free_expr(expr->data.atomic_op.operands[j]);
      }
      free(expr->data.atomic_op.operands);
      expr->data.atomic_op.operands = NULL; // Prevent double free
      free(expr); // Use free() directly instead of free_expr()
      return NULL;
    }
  }

  expr->data.atomic_op.operand_count = operand_count;
  return expr;
}

// Parse syscall operations ($/, /$, sys%, etc.)
Expr *parse_syscall_operation(Parser *parser, TokenKind op) {
  Expr *expr = allocate_expr(EXPR_SYSCALL_OP);
  if (!expr)
    return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  // Determine syscall operation type
  if (op == OP_WRITE_SYSCALL) {
    expr->data.syscall_op.syscall_type = SYSCALL_WRITE;
  } else if (op == OP_READ_SYSCALL) {
    expr->data.syscall_op.syscall_type = SYSCALL_READ;
  } else if (op == OP_RAW_SYSCALL) {
    expr->data.syscall_op.syscall_type = SYSCALL_RAW;
  } else {
    expr->data.syscall_op.syscall_type = SYSCALL_RAW; // Default
  }

  // Parse syscall number (for raw syscalls)
  if (op == OP_RAW_SYSCALL) {
    consume(parser, TOK_LPAREN, "Expected '(' after sys%");
    // Use PREC_ASSIGNMENT to stop at comma (which has PREC_SEQUENCE)
    expr->data.syscall_op.syscall_num = parse_precedence(parser, PREC_ASSIGNMENT);
    if (!expr->data.syscall_op.syscall_num) {
      free_expr(expr);
      return NULL;
    }

    // Parse arguments
    size_t arg_capacity = 4;
    expr->data.syscall_op.args = malloc(arg_capacity * sizeof(Expr *));
    if (!expr->data.syscall_op.args) {
      free_expr(expr->data.syscall_op.syscall_num);
      free_expr(expr);
      return NULL;
    }

    expr->data.syscall_op.arg_count = 0;

    while (parser_match(parser, TOK_COMMA)) {
      if (expr->data.syscall_op.arg_count >= arg_capacity) {
        arg_capacity *= 2;
        Expr **new_args =
            realloc(expr->data.syscall_op.args, arg_capacity * sizeof(Expr *));
        if (!new_args) {
          for (size_t i = 0; i < expr->data.syscall_op.arg_count; i++) {
            free_expr(expr->data.syscall_op.args[i]);
          }
          free(expr->data.syscall_op.args);
          free_expr(expr->data.syscall_op.syscall_num);
          free_expr(expr);
          return NULL;
        }
        expr->data.syscall_op.args = new_args;
      }

      // Use PREC_ASSIGNMENT to stop at comma
      Expr *arg = parse_precedence(parser, PREC_ASSIGNMENT);
      if (!arg) {
        for (size_t i = 0; i < expr->data.syscall_op.arg_count; i++) {
          free_expr(expr->data.syscall_op.args[i]);
        }
        free(expr->data.syscall_op.args);
        expr->data.syscall_op.args = NULL;
        free_expr(expr->data.syscall_op.syscall_num);
        expr->data.syscall_op.syscall_num = NULL;
        free(expr);
        return NULL;
      }

      expr->data.syscall_op.args[expr->data.syscall_op.arg_count++] = arg;
    }

    consume(parser, TOK_RPAREN, "Expected ')' after syscall arguments");
  } else if (op == OP_PRIV_ESCALATE || op == OP_CAPABILITY_CHECK) {
    // For #! and !# operators, parse single argument
    expr->data.syscall_op.syscall_num = NULL;
    expr->data.syscall_op.args = malloc(1 * sizeof(Expr *));
    if (!expr->data.syscall_op.args) {
      free_expr(expr);
      return NULL;
    }

    // Parse single argument
    expr->data.syscall_op.args[0] = parse_precedence(parser, PREC_PARENTHESES);
    if (!expr->data.syscall_op.args[0]) {
      free(expr->data.syscall_op.args);
      free_expr(expr);
      return NULL;
    }

    expr->data.syscall_op.arg_count = 1;
  } else {
    // For $/ and /$ operators, parse: fd $/ buffer, length
    // The fd was already parsed as the left operand (passed in)
    // We need to parse: buffer, length
    expr->data.syscall_op.syscall_num = NULL;
    expr->data.syscall_op.args = malloc(3 * sizeof(Expr *));
    if (!expr->data.syscall_op.args) {
      free_expr(expr);
      return NULL;
    }

    // Parse file descriptor
    expr->data.syscall_op.args[0] = parse_precedence(parser, PREC_PARENTHESES);
    if (!expr->data.syscall_op.args[0]) {
      free(expr->data.syscall_op.args);
      free_expr(expr);
      return NULL;
    }

    // Parse buffer
    expr->data.syscall_op.args[1] = parse_precedence(parser, PREC_PARENTHESES);
    if (!expr->data.syscall_op.args[1]) {
      free_expr(expr->data.syscall_op.args[0]);
      free(expr->data.syscall_op.args);
      expr->data.syscall_op.args = NULL;
      free(expr);
      return NULL;
    }

    expr->data.syscall_op.arg_count = 2;
  }

  return expr;
}

// Parse inline assembly with new syntax:
//
// FCx Inline Assembly Syntax (AT&T syntax, x86-64):
// ================================================
//
// Basic forms:
//   asm% [ nop ]                                    // Single instruction
//   asm% [ mov %rax, %rbx ]                         // Register to register
//   asm% [ 
//       push %rbp
//       mov %rsp, %rbp
//       pop %rbp
//   ]                                               // Multiline block
//
// With output (result stored in variable):
//   let x = asm% [ xorq %0, %0 ] -> "=r"            // Output to any register
//   let x = asm% [ rdtsc ] -> "=a"                  // Output to specific reg (rax)
//
// With inputs:
//   asm% [ addq %1, %0 ] -> "=r" <- "r"(a), "0"(b)  // a + b, result in output
//
// With clobbers (registers modified by asm):
//   asm% [ cpuid ] -> "=a","=b","=c","=d" ~> "memory"
//
// Constraint letters (GCC/LLVM style):
//   "r"  - any general register
//   "a"  - rax
//   "b"  - rbx  
//   "c"  - rcx
//   "d"  - rdx
//   "S"  - rsi
//   "D"  - rdi
//   "m"  - memory operand
//   "i"  - immediate integer
//   "0"-"9" - same as operand N
//   "="  - output operand (write-only)
//   "+"  - input/output operand (read-write)
//
// Common clobbers:
//   "memory" - asm reads/writes memory
//   "cc"     - asm modifies condition codes (flags)
//   Register names like "rax", "rbx", etc.
//
// FCx Extension: Use ${varname} to reference variables
//   asm% "movq ${x}, %rax"  -> generates input constraint for x
//   The ${...} is replaced with $0, $1, etc. and constraints are auto-generated
//

// Helper: Extract variable references from asm template
// Returns processed template with ${var} replaced by $0, $1, etc.
// Populates var_names array with variable names found
static char* extract_asm_variables(const char* template, char*** var_names, size_t* var_count) {
    *var_count = 0;
    *var_names = NULL;
    
    if (!template) return NULL;
    
    // First pass: count variables
    size_t count = 0;
    const char* p = template;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            count++;
            p += 2;
            while (*p && *p != '}') p++;
            if (*p == '}') p++;
        } else {
            p++;
        }
    }
    
    if (count == 0) {
        // No variables, return copy of template
        return fcx_strdup(template);
    }
    
    // Allocate var_names array
    *var_names = malloc(count * sizeof(char*));
    if (!*var_names) return NULL;
    
    // Second pass: extract names and build new template
    size_t template_len = strlen(template);
    char* result = malloc(template_len + count * 10 + 1); // Extra space for $N replacements
    if (!result) {
        free(*var_names);
        *var_names = NULL;
        return NULL;
    }
    
    char* dst = result;
    p = template;
    size_t var_idx = 0;
    
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            // Found ${varname}
            p += 2; // Skip ${
            const char* name_start = p;
            while (*p && *p != '}') p++;
            size_t name_len = p - name_start;
            
            // Check if we already have this variable
            int existing_idx = -1;
            for (size_t i = 0; i < var_idx; i++) {
                if (strlen((*var_names)[i]) == name_len && 
                    strncmp((*var_names)[i], name_start, name_len) == 0) {
                    existing_idx = (int)i;
                    break;
                }
            }
            
            if (existing_idx >= 0) {
                // Reuse existing variable index
                dst += sprintf(dst, "$%d", existing_idx);
            } else {
                // New variable
                char* name = malloc(name_len + 1);
                if (name) {
                    memcpy(name, name_start, name_len);
                    name[name_len] = '\0';
                    (*var_names)[var_idx] = name;
                    dst += sprintf(dst, "$%d", (int)var_idx);
                    var_idx++;
                }
            }
            
            if (*p == '}') p++; // Skip }
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
    
    *var_count = var_idx;
    return result;
}

Expr *parse_inline_asm(Parser *parser) {
  Expr *expr = allocate_expr(EXPR_INLINE_ASM);
  if (!expr) return NULL;

  expr->line = parser->previous.line;
  expr->column = parser->previous.column;

  // Initialize inline_asm data
  expr->data.inline_asm.asm_template = NULL;
  expr->data.inline_asm.output_constraints = NULL;
  expr->data.inline_asm.input_constraints = NULL;
  expr->data.inline_asm.output_exprs = NULL;
  expr->data.inline_asm.input_exprs = NULL;
  expr->data.inline_asm.clobbers = NULL;
  expr->data.inline_asm.output_count = 0;
  expr->data.inline_asm.input_count = 0;
  expr->data.inline_asm.clobber_count = 0;
  expr->data.inline_asm.is_volatile = true; // Default to volatile for safety

  char *raw_template = NULL;
  
  // Check what follows asm%:
  // - String: asm% "instruction"  
  // - Brace:  asm% { multiline }
  
  // Look at the current token
  if (parser_check(parser, TOK_STRING)) {
    // asm% "single instruction"
    parser_advance(parser);
    size_t len = parser->previous.length - 2; // remove quotes
    raw_template = malloc(len + 1);
    if (!raw_template) { free_expr(expr); return NULL; }
    memcpy(raw_template, parser->previous.start + 1, len);
    raw_template[len] = '\0';
    
  } else if (parser_check(parser, TOK_LBRACE)) {
    // asm% { multiline block }
    // The current token is '{', we need to scan raw source for content
    
    // Get position right after the '{'
    const char *block_start = parser->current.start + parser->current.length;
    const char *ptr = block_start;
    int brace_depth = 1;
    size_t lines_skipped = 0;
    
    while (*ptr && brace_depth > 0) {
      char c = *ptr;
      if (c == '{') brace_depth++;
      else if (c == '}') brace_depth--;
      if (c == '\n') lines_skipped++;
      if (brace_depth > 0) ptr++;
    }
    
    if (brace_depth != 0) {
      error_at_current(parser, "Unterminated assembly block, expected '}'");
      free_expr(expr);
      return NULL;
    }
    
    // Copy content between { and }
    size_t len = ptr - block_start;
    char *raw = malloc(len + 1);
    if (!raw) { free_expr(expr); return NULL; }
    memcpy(raw, block_start, len);
    raw[len] = '\0';
    
    // Normalize: trim and convert newlines
    raw_template = malloc(len * 2 + 1);
    if (!raw_template) { free(raw); free_expr(expr); return NULL; }
    
    char *dst = raw_template;
    const char *src = raw;
    bool at_start = true;
    bool had_content = false;
    
    while (*src) {
      if (*src == '\n' || *src == '\r') {
        if (had_content) { *dst++ = '\n'; at_start = true; had_content = false; }
        src++;
      } else if (*src == ' ' || *src == '\t') {
        if (!at_start && had_content) *dst++ = ' ';
        src++;
      } else {
        at_start = false; had_content = true;
        *dst++ = *src++;
      }
    }
    *dst = '\0';
    while (dst > raw_template && (dst[-1] == ' ' || dst[-1] == '\n')) *--dst = '\0';
    free(raw);
    
    // Move lexer past the closing '}' and get next token
    parser->lexer->current = ptr + 1; // Skip past '}'
    parser->lexer->line += lines_skipped;
    parser_advance(parser); // get next token
    
  } else {
    error_at_current(parser, "Expected '\"string\"' or '{block}' after asm%");
    free_expr(expr);
    return NULL;
  }
  
  // Process template for ${varname} references
  char** var_names = NULL;
  size_t var_count = 0;
  char* processed_template = extract_asm_variables(raw_template, &var_names, &var_count);
  free(raw_template);
  
  if (!processed_template) {
    free_expr(expr);
    return NULL;
  }
  
  expr->data.inline_asm.asm_template = processed_template;
  
  // Create input expressions for each variable reference
  if (var_count > 0) {
    expr->data.inline_asm.input_constraints = malloc(var_count * sizeof(char*));
    expr->data.inline_asm.input_exprs = malloc(var_count * sizeof(Expr*));
    
    if (!expr->data.inline_asm.input_constraints || !expr->data.inline_asm.input_exprs) {
      for (size_t i = 0; i < var_count; i++) free(var_names[i]);
      free(var_names);
      free_expr(expr);
      return NULL;
    }
    
    for (size_t i = 0; i < var_count; i++) {
      // Default constraint: "r" (any general register)
      expr->data.inline_asm.input_constraints[i] = fcx_strdup("r");
      
      // Create identifier expression for the variable
      Expr* var_expr = allocate_expr(EXPR_IDENTIFIER);
      if (var_expr) {
        var_expr->line = expr->line;
        var_expr->column = expr->column;
        var_expr->data.identifier = var_names[i]; // Transfer ownership
      } else {
        free(var_names[i]);
      }
      expr->data.inline_asm.input_exprs[i] = var_expr;
    }
    
    expr->data.inline_asm.input_count = var_count;
    free(var_names); // Array freed, strings transferred to input_exprs
  }

  // Parse optional output constraints: "=r", "=a", etc.
  if (parser_check(parser, TOK_STRING)) {
    // Parse output constraints
    size_t output_capacity = 4;
    expr->data.inline_asm.output_constraints = malloc(output_capacity * sizeof(char*));
    expr->data.inline_asm.output_exprs = malloc(output_capacity * sizeof(Expr*));
    if (!expr->data.inline_asm.output_constraints || !expr->data.inline_asm.output_exprs) {
      free_expr(expr);
      return NULL;
    }

    // Parse output list: "=r", "=a", etc.
    while (parser_check(parser, TOK_STRING)) {
      if (expr->data.inline_asm.output_count >= output_capacity) {
        output_capacity *= 2;
        expr->data.inline_asm.output_constraints = realloc(
            expr->data.inline_asm.output_constraints, output_capacity * sizeof(char*));
        expr->data.inline_asm.output_exprs = realloc(
            expr->data.inline_asm.output_exprs, output_capacity * sizeof(Expr*));
      }

      parser_advance(parser);
      size_t clen = parser->previous.length - 2;
      char *constraint = malloc(clen + 1);
      if (!constraint) {
        free_expr(expr);
        return NULL;
      }
      memcpy(constraint, parser->previous.start + 1, clen);
      constraint[clen] = '\0';
      expr->data.inline_asm.output_constraints[expr->data.inline_asm.output_count] = constraint;
      expr->data.inline_asm.output_exprs[expr->data.inline_asm.output_count] = NULL;
      expr->data.inline_asm.output_count++;

      if (!parser_match(parser, TOK_COMMA)) break;
    }
  }

  // Parse optional inputs: <- "constraint"(expr), ...
  // Check for <- (OP_MOVE_BACKWARD is <)
  if (parser_check(parser, OP_MOVE_BACKWARD) || parser_check(parser, OP_LT)) {
    parser_advance(parser); // consume <
    
    // Check for - after <
    // For simplicity, just check if we have a string next
  }
  
  // If we see a string with parentheses, it's an input
  if (parser_check(parser, TOK_STRING)) {
    // Check if this looks like an input (has parentheses after)
    // Peek ahead - for now just parse as inputs if we already have outputs
    if (expr->data.inline_asm.output_count > 0) {
      size_t input_capacity = 4;
      expr->data.inline_asm.input_constraints = malloc(input_capacity * sizeof(char*));
      expr->data.inline_asm.input_exprs = malloc(input_capacity * sizeof(Expr*));
      if (!expr->data.inline_asm.input_constraints || !expr->data.inline_asm.input_exprs) {
        free_expr(expr);
        return NULL;
      }

      while (parser_check(parser, TOK_STRING)) {
        if (expr->data.inline_asm.input_count >= input_capacity) {
          input_capacity *= 2;
          expr->data.inline_asm.input_constraints = realloc(
              expr->data.inline_asm.input_constraints, input_capacity * sizeof(char*));
          expr->data.inline_asm.input_exprs = realloc(
              expr->data.inline_asm.input_exprs, input_capacity * sizeof(Expr*));
        }

        parser_advance(parser);
        size_t clen = parser->previous.length - 2;
        char *constraint = malloc(clen + 1);
        if (!constraint) {
          free_expr(expr);
          return NULL;
        }
        memcpy(constraint, parser->previous.start + 1, clen);
        constraint[clen] = '\0';
        expr->data.inline_asm.input_constraints[expr->data.inline_asm.input_count] = constraint;

        // Parse (expr) for input value
        if (parser_match(parser, TOK_LPAREN)) {
          Expr *in_expr = parse_precedence(parser, PREC_ASSIGNMENT);
          if (!in_expr) {
            free_expr(expr);
            return NULL;
          }
          expr->data.inline_asm.input_exprs[expr->data.inline_asm.input_count] = in_expr;
          consume(parser, TOK_RPAREN, "Expected ')' after input expression");
        } else {
          expr->data.inline_asm.input_exprs[expr->data.inline_asm.input_count] = NULL;
        }

        expr->data.inline_asm.input_count++;
        if (!parser_match(parser, TOK_COMMA)) break;
      }
    }
  }

  return expr;
}

Stmt *parse_function(Parser *parser) {
  return parse_function_statement(parser);
}

// Memory management
Expr *allocate_expr(ExprType type) {
  Expr *expr = malloc(sizeof(Expr));
  if (expr) {
    expr->type = type;
    expr->line = 0;
    expr->column = 0;

    // Initialize all union members to NULL/0
    memset(&expr->data, 0, sizeof(expr->data));
  }
  return expr;
}

Stmt *allocate_stmt(StmtType type) {
  Stmt *stmt = malloc(sizeof(Stmt));
  if (stmt) {
    stmt->type = type;
    stmt->line = 0;
    stmt->column = 0;

    // Initialize all union members to NULL/0
    memset(&stmt->data, 0, sizeof(stmt->data));
  }
  return stmt;
}

void free_expr(Expr *expr) {
  if (!expr)
    return;

  switch (expr->type) {
  case EXPR_LITERAL:
    if (expr->data.literal.type == LIT_STRING &&
        expr->data.literal.value.string) {
      free(expr->data.literal.value.string);
    }
    break;

  case EXPR_IDENTIFIER:
    if (expr->data.identifier) {
      free(expr->data.identifier);
    }
    break;

  case EXPR_BINARY:
    free_expr(expr->data.binary.left);
    free_expr(expr->data.binary.right);
    break;

  case EXPR_UNARY:
    free_expr(expr->data.unary.operand);
    break;

  case EXPR_TERNARY:
    free_expr(expr->data.ternary.first);
    free_expr(expr->data.ternary.second);
    free_expr(expr->data.ternary.third);
    break;

  case EXPR_CALL:
    free_expr(expr->data.call.function);
    for (size_t i = 0; i < expr->data.call.arg_count; i++) {
      free_expr(expr->data.call.args[i]);
    }
    free(expr->data.call.args);
    break;

  case EXPR_INDEX:
    free_expr(expr->data.index.base);
    free_expr(expr->data.index.index);
    break;

  case EXPR_DEREF:
    free_expr(expr->data.deref.pointer);
    if (expr->data.deref.value) {
      free_expr(expr->data.deref.value);
    }
    break;

  case EXPR_ASSIGNMENT:
    free_expr(expr->data.assignment.target);
    free_expr(expr->data.assignment.value);
    break;

  case EXPR_MULTI_ASSIGN:
    for (size_t i = 0; i < expr->data.multi_assign.count; i++) {
      free_expr(expr->data.multi_assign.targets[i]);
      if (i < expr->data.multi_assign.count && expr->data.multi_assign.values) {
        free_expr(expr->data.multi_assign.values[i]);
      }
    }
    free(expr->data.multi_assign.targets);
    free(expr->data.multi_assign.values);
    break;

  case EXPR_CONDITIONAL:
    free_expr(expr->data.conditional.condition);
    free_expr(expr->data.conditional.then_expr);
    free_expr(expr->data.conditional.else_expr);
    break;

  case EXPR_FUNCTION_DEF:
    if (expr->data.function_def.name) {
      free(expr->data.function_def.name);
    }
    for (size_t i = 0; i < expr->data.function_def.param_count; i++) {
      if (expr->data.function_def.params[i].name) {
        free(expr->data.function_def.params[i].name);
      }
    }
    free(expr->data.function_def.params);
    free_block(&expr->data.function_def.body);
    break;

  case EXPR_MEMORY_OP:
    for (size_t i = 0; i < expr->data.memory_op.operand_count; i++) {
      free_expr(expr->data.memory_op.operands[i]);
    }
    free(expr->data.memory_op.operands);
    break;

  case EXPR_ATOMIC_OP:
    for (size_t i = 0; i < expr->data.atomic_op.operand_count; i++) {
      free_expr(expr->data.atomic_op.operands[i]);
    }
    free(expr->data.atomic_op.operands);
    break;

  case EXPR_SYSCALL_OP:
    if (expr->data.syscall_op.syscall_num) {
      free_expr(expr->data.syscall_op.syscall_num);
    }
    for (size_t i = 0; i < expr->data.syscall_op.arg_count; i++) {
      free_expr(expr->data.syscall_op.args[i]);
    }
    free(expr->data.syscall_op.args);
    break;
    
  case EXPR_INLINE_ASM:
    if (expr->data.inline_asm.asm_template) {
      free((void*)expr->data.inline_asm.asm_template);
    }
    for (size_t i = 0; i < expr->data.inline_asm.output_count; i++) {
      if (expr->data.inline_asm.output_constraints && expr->data.inline_asm.output_constraints[i]) {
        free((void*)expr->data.inline_asm.output_constraints[i]);
      }
      if (expr->data.inline_asm.output_exprs && expr->data.inline_asm.output_exprs[i]) {
        free_expr(expr->data.inline_asm.output_exprs[i]);
      }
    }
    free(expr->data.inline_asm.output_constraints);
    free(expr->data.inline_asm.output_exprs);
    for (size_t i = 0; i < expr->data.inline_asm.input_count; i++) {
      if (expr->data.inline_asm.input_constraints && expr->data.inline_asm.input_constraints[i]) {
        free((void*)expr->data.inline_asm.input_constraints[i]);
      }
      if (expr->data.inline_asm.input_exprs && expr->data.inline_asm.input_exprs[i]) {
        free_expr(expr->data.inline_asm.input_exprs[i]);
      }
    }
    free(expr->data.inline_asm.input_constraints);
    free(expr->data.inline_asm.input_exprs);
    for (size_t i = 0; i < expr->data.inline_asm.clobber_count; i++) {
      if (expr->data.inline_asm.clobbers && expr->data.inline_asm.clobbers[i]) {
        free((void*)expr->data.inline_asm.clobbers[i]);
      }
    }
    free(expr->data.inline_asm.clobbers);
    break;
  }

  free(expr);
}

void free_stmt(Stmt *stmt) {
  if (!stmt)
    return;

  switch (stmt->type) {
  case STMT_EXPRESSION:
    free_expr(stmt->data.expression);
    break;

  case STMT_LET:
    if (stmt->data.let.name) {
      free(stmt->data.let.name);
    }
    free_expr(stmt->data.let.initializer);
    break;

  case STMT_FUNCTION:
    if (stmt->data.function.name) {
      free(stmt->data.function.name);
    }
    for (size_t i = 0; i < stmt->data.function.param_count; i++) {
      if (stmt->data.function.params[i].name) {
        free(stmt->data.function.params[i].name);
      }
    }
    free(stmt->data.function.params);
    free_block(&stmt->data.function.body);
    break;

  case STMT_IF:
    free_expr(stmt->data.if_stmt.condition);
    free_block(&stmt->data.if_stmt.then_branch);
    free_block(&stmt->data.if_stmt.else_branch);
    break;

  case STMT_LOOP:
    free_expr(stmt->data.loop.condition);
    free_expr(stmt->data.loop.iteration);
    free_block(&stmt->data.loop.body);
    break;

  case STMT_RETURN:
  case STMT_HALT:
    free_expr(stmt->data.return_value);
    break;
    
  case STMT_BREAK:
  case STMT_CONTINUE:
    break;
    
  case STMT_MOD:
    if (stmt->data.mod_decl.name) {
      free(stmt->data.mod_decl.name);
    }
    if (stmt->data.mod_decl.is_inline) {
      free_block(&stmt->data.mod_decl.body);
    }
    break;
    
  case STMT_USE:
    if (stmt->data.use_decl.path) {
      for (size_t i = 0; i < stmt->data.use_decl.path_len; i++) {
        free(stmt->data.use_decl.path[i]);
      }
      free(stmt->data.use_decl.path);
    }
    if (stmt->data.use_decl.alias) {
      free(stmt->data.use_decl.alias);
    }
    if (stmt->data.use_decl.items) {
      for (size_t i = 0; i < stmt->data.use_decl.item_count; i++) {
        free(stmt->data.use_decl.items[i]);
      }
      free(stmt->data.use_decl.items);
    }
    break;
  }

  free(stmt);
}

void free_block(Block *block) {
  if (block && block->statements) {
    for (size_t i = 0; i < block->count; i++) {
      free_stmt(block->statements[i]);
    }
    free(block->statements);
    block->statements = NULL;
    block->count = 0;
    block->capacity = 0;
  }
}


// ============================================================================
// Module System Parsing
// ============================================================================

// Helper to duplicate a string
static char* parser_strdup(const char* s, size_t len) {
    char* dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

// Parse: pub fn/mod/use/const/let
Stmt *parse_pub_statement(Parser *parser) {
    // We've already consumed 'pub'
    
    if (parser_match(parser, KW_FN)) {
        Stmt *stmt = parse_function_statement(parser);
        if (stmt) {
            stmt->data.function.is_public = true;
        }
        return stmt;
    }
    
    if (parser_match(parser, KW_MOD)) {
        return parse_mod_statement(parser, true);
    }
    
    if (parser_match(parser, KW_USE)) {
        return parse_use_statement(parser, true);
    }
    
    // Could also support pub let, pub const in the future
    error_at_current(parser, "Expected 'fn', 'mod', or 'use' after 'pub'");
    return NULL;
}

// Parse: mod name; or mod name { ... }
Stmt *parse_mod_statement(Parser *parser, bool is_public) {
    // We've already consumed 'mod'
    
    consume(parser, TOK_IDENTIFIER, "Expected module name after 'mod'");
    
    size_t name_len = parser->previous.length;
    char *name = parser_strdup(parser->previous.start, name_len);
    if (!name) return NULL;
    
    Stmt *stmt = allocate_stmt(STMT_MOD);
    if (!stmt) {
        free(name);
        return NULL;
    }
    
    stmt->line = parser->previous.line;
    stmt->column = parser->previous.column;
    stmt->data.mod_decl.name = name;
    stmt->data.mod_decl.is_public = is_public;
    stmt->data.mod_decl.is_inline = false;
    stmt->data.mod_decl.body.statements = NULL;
    stmt->data.mod_decl.body.count = 0;
    stmt->data.mod_decl.body.capacity = 0;
    
    // Check for inline module: mod name { ... }
    if (parser_match(parser, TOK_LBRACE)) {
        stmt->data.mod_decl.is_inline = true;
        stmt->data.mod_decl.body = parse_block(parser);
    } else {
        // External module: mod name;
        // Semicolon is optional
        parser_match(parser, TOK_SEMICOLON);
    }
    
    return stmt;
}

// Parse a module path: std::io::println
// Returns array of path segments
static char** parse_use_path(Parser *parser, size_t *out_len) {
    size_t capacity = 4;
    size_t count = 0;
    char **path = malloc(capacity * sizeof(char*));
    if (!path) return NULL;
    
    // First segment
    if (parser_match(parser, KW_CRATE)) {
        path[count++] = parser_strdup("crate", 5);
    } else if (parser_match(parser, KW_SELF)) {
        path[count++] = parser_strdup("self", 4);
    } else if (parser_match(parser, KW_SUPER)) {
        path[count++] = parser_strdup("super", 5);
    } else {
        consume(parser, TOK_IDENTIFIER, "Expected module path");
        path[count++] = parser_strdup(parser->previous.start, parser->previous.length);
    }
    
    // Continue with :: segments
    while (parser_match(parser, TOK_DOUBLE_COLON)) {
        if (count >= capacity) {
            capacity *= 2;
            char **new_path = realloc(path, capacity * sizeof(char*));
            if (!new_path) {
                for (size_t i = 0; i < count; i++) free(path[i]);
                free(path);
                return NULL;
            }
            path = new_path;
        }
        
        // Check for glob: use foo::*
        if (parser_check(parser, OP_MUL_ASSIGN) || 
            (parser->current.start && parser->current.length == 1 && parser->current.start[0] == '*')) {
            parser_advance(parser);
            path[count++] = parser_strdup("*", 1);
            break;
        }
        
        // Check for grouped import: use foo::{a, b}
        if (parser_check(parser, TOK_LBRACE)) {
            // Don't consume the brace, let caller handle it
            break;
        }
        
        // Check for super
        if (parser_match(parser, KW_SUPER)) {
            path[count++] = parser_strdup("super", 5);
            continue;
        }
        
        // Regular identifier
        consume(parser, TOK_IDENTIFIER, "Expected identifier in module path");
        path[count++] = parser_strdup(parser->previous.start, parser->previous.length);
    }
    
    *out_len = count;
    return path;
}

// Parse: use std::io; or use std::io::println; or use std::io::*; or use std::io::{a, b};
Stmt *parse_use_statement(Parser *parser, bool is_public) {
    // We've already consumed 'use'
    
    Stmt *stmt = allocate_stmt(STMT_USE);
    if (!stmt) return NULL;
    
    stmt->line = parser->previous.line;
    stmt->column = parser->previous.column;
    stmt->data.use_decl.is_public = is_public;
    stmt->data.use_decl.alias = NULL;
    stmt->data.use_decl.is_glob = false;
    stmt->data.use_decl.items = NULL;
    stmt->data.use_decl.item_count = 0;
    
    // Parse the path
    size_t path_len = 0;
    char **path = parse_use_path(parser, &path_len);
    if (!path) {
        free(stmt);
        return NULL;
    }
    
    stmt->data.use_decl.path = path;
    stmt->data.use_decl.path_len = path_len;
    
    // Check if last segment is glob
    if (path_len > 0 && strcmp(path[path_len - 1], "*") == 0) {
        stmt->data.use_decl.is_glob = true;
    }
    
    // Check for grouped import: use foo::{a, b, c}
    if (parser_match(parser, TOK_LBRACE)) {
        size_t item_capacity = 4;
        size_t item_count = 0;
        char **items = malloc(item_capacity * sizeof(char*));
        if (!items) {
            free_stmt(stmt);
            return NULL;
        }
        
        do {
            if (item_count >= item_capacity) {
                item_capacity *= 2;
                char **new_items = realloc(items, item_capacity * sizeof(char*));
                if (!new_items) {
                    for (size_t i = 0; i < item_count; i++) free(items[i]);
                    free(items);
                    free_stmt(stmt);
                    return NULL;
                }
                items = new_items;
            }
            
            // Check for self in group: use foo::{self, bar}
            if (parser_match(parser, KW_SELF)) {
                items[item_count++] = parser_strdup("self", 4);
            } else {
                consume(parser, TOK_IDENTIFIER, "Expected identifier in use group");
                items[item_count++] = parser_strdup(parser->previous.start, parser->previous.length);
            }
        } while (parser_match(parser, TOK_COMMA));
        
        consume(parser, TOK_RBRACE, "Expected '}' after use group");
        
        stmt->data.use_decl.items = items;
        stmt->data.use_decl.item_count = item_count;
    }
    
    // Check for alias: use foo as bar
    if (parser_match(parser, KW_AS)) {
        consume(parser, TOK_IDENTIFIER, "Expected identifier after 'as'");
        stmt->data.use_decl.alias = parser_strdup(parser->previous.start, parser->previous.length);
    }
    
    // Semicolon is optional
    parser_match(parser, TOK_SEMICOLON);
    
    return stmt;
}
