#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stub implementation of FCx lexer
// This would be fully implemented with operator trie in the complete compiler

void lexer_init(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->current = source;
  lexer->start = source;
  lexer->line = 1;
  lexer->column = 1;
  lexer->had_error = false;

  // Ensure operator registry is initialized
  static bool registry_initialized = false;
  if (!registry_initialized) {
    init_operator_registry();
    registry_initialized = true;
  }
}

bool lexer_is_at_end(const Lexer *lexer) { return *lexer->current == '\0'; }

char peek(const Lexer *lexer) { return *lexer->current; }

char peek_next(const Lexer *lexer) {
  if (lexer_is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

char advance(Lexer *lexer) {
  if (lexer_is_at_end(lexer))
    return '\0';

  char c = *lexer->current++;
  if (c == '\n') {
    lexer->line++;
    lexer->column = 1;
  } else {
    lexer->column++;
  }
  return c;
}

bool match(Lexer *lexer, char expected) {
  if (lexer_is_at_end(lexer))
    return false;
  if (*lexer->current != expected)
    return false;

  advance(lexer);
  return true;
}

// Find similar operators for error suggestions
static void suggest_similar_operators(const char *invalid_symbol,
                                      char *suggestion_buffer,
                                      size_t buffer_size) {
  suggestion_buffer[0] = '\0'; // Initialize empty

  if (invalid_symbol == NULL || strlen(invalid_symbol) == 0) {
    return;
  }

  // Look for operators that start with the same character
  char first_char = invalid_symbol[0];
  size_t suggestions_found = 0;
  const size_t max_suggestions = 3;

  for (size_t i = 0;
       i < get_operator_count() && suggestions_found < max_suggestions; i++) {
    const OperatorInfo *op = get_operator_by_index(i);
    if (op && op->symbol && op->symbol[0] == first_char) {
      if (suggestions_found == 0) {
        snprintf(suggestion_buffer, buffer_size, " Did you mean: '%s'",
                 op->symbol);
      } else {
        size_t current_len = strlen(suggestion_buffer);
        snprintf(suggestion_buffer + current_len, buffer_size - current_len,
                 ", '%s'", op->symbol);
      }
      suggestions_found++;
    }
  }

  if (suggestions_found > 0) {
    size_t current_len = strlen(suggestion_buffer);
    snprintf(suggestion_buffer + current_len, buffer_size - current_len, "?");
  }
}

void lexer_error(Lexer *lexer, const char *message) {
  fprintf(stderr, "[Line %zu:%zu] Error: %s\n", lexer->line, lexer->column,
          message);
  lexer->had_error = true;
}

// Character classification
bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

// Skip whitespace
static void skip_whitespace(Lexer *lexer) {
  while (true) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance(lexer);
      break;
    case '\n':
      advance(lexer);
      break;
    case '/':
      // Check for comments
      if (peek_next(lexer) == '/') {
        // Line comment - skip until end of line
        while (peek(lexer) != '\n' && !lexer_is_at_end(lexer)) {
          advance(lexer);
        }
      } else if (peek_next(lexer) == '*') {
        // Block comment - skip until */
        advance(lexer); // Skip /
        advance(lexer); // Skip *
        while (!lexer_is_at_end(lexer)) {
          if (peek(lexer) == '*' && peek_next(lexer) == '/') {
            advance(lexer); // Skip *
            advance(lexer); // Skip /
            break;
          }
          advance(lexer);
        }
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

// Create token
static Token make_token(Lexer *lexer, TokenKind kind) {
  Token token;
  token.kind = kind;
  token.start = lexer->start;
  token.length = (size_t)(lexer->current - lexer->start);
  token.line = lexer->line;
  token.column = lexer->column - token.length;
  return token;
}

// Create error token
static Token error_token(Lexer *lexer, const char *message) {
  Token token;
  token.kind = TOK_ERROR;
  token.start = message;
  token.length = strlen(message);
  token.line = lexer->line;
  token.column = lexer->column;
  lexer_error(lexer, message);
  return token;
}

// Process escape sequence, returns the escaped character
// Returns -1 on invalid escape
static int process_escape(char c) {
  switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '0':  return '\0';
    case '\\': return '\\';
    case '"':  return '"';
    case '\'': return '\'';
    case 'a':  return '\a';
    case 'b':  return '\b';
    case 'f':  return '\f';
    case 'v':  return '\v';
    default:   return -1;
  }
}

// Scan string literal with escape sequence processing
static Token scan_string(Lexer *lexer) {
  // First pass: count length
  const char *scan = lexer->current;
  size_t processed_len = 0;
  
  while (*scan != '"' && *scan != '\0') {
    if (*scan == '\\' && scan[1] != '\0') {
      char esc = scan[1];
      int escaped = process_escape(esc);
      if (escaped == -1) {
        // Unknown escape - keep both chars
        processed_len += 2;
      } else {
        processed_len++;
      }
      scan += 2;
    } else {
      scan++;
      processed_len++;
    }
  }
  
  if (*scan == '\0') {
    while (!lexer_is_at_end(lexer) && peek(lexer) != '"') {
      advance(lexer);
    }
    return error_token(lexer, "Unterminated string");
  }
  
  // Allocate processed string buffer
  char *processed = malloc(processed_len + 1);
  if (!processed) {
    return error_token(lexer, "Out of memory");
  }
  
  // Second pass: copy and process escapes
  size_t out_pos = 0;
  while (peek(lexer) != '"') {
    char c = advance(lexer);
    if (c == '\\') {
      char esc = advance(lexer);
      int escaped = process_escape(esc);
      if (escaped == -1) {
        processed[out_pos++] = c;
        processed[out_pos++] = esc;
      } else {
        processed[out_pos++] = (char)escaped;
      }
    } else {
      processed[out_pos++] = c;
    }
  }
  processed[out_pos] = '\0';
  
  // Closing quote
  advance(lexer);
  
  Token token = make_token(lexer, TOK_STRING);
  token.value.string = processed;
  token.length = processed_len;
  return token;
}

// Helper for hex digits
static bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Helper for binary digits
static bool is_binary_digit(char c) {
  return c == '0' || c == '1';
}

// Helper for octal digits
static bool is_octal_digit(char c) {
  return c >= '0' && c <= '7';
}

// Scan number literal (supports hex 0x, binary 0b, octal 0o)
static Token scan_number(Lexer *lexer) {
  // Check for hex, binary, or octal prefix
  if (peek(lexer) == '0') {
    char next = peek_next(lexer);
    
    // Hex: 0x or 0X
    if (next == 'x' || next == 'X') {
      advance(lexer); // consume '0'
      advance(lexer); // consume 'x'
      while (is_hex_digit(peek(lexer))) {
        advance(lexer);
      }
      return make_token(lexer, TOK_INTEGER);
    }
    
    // Binary: 0b or 0B
    if (next == 'b' || next == 'B') {
      advance(lexer); // consume '0'
      advance(lexer); // consume 'b'
      while (is_binary_digit(peek(lexer))) {
        advance(lexer);
      }
      return make_token(lexer, TOK_INTEGER);
    }
    
    // Octal: 0o or 0O
    if (next == 'o' || next == 'O') {
      advance(lexer); // consume '0'
      advance(lexer); // consume 'o'
      while (is_octal_digit(peek(lexer))) {
        advance(lexer);
      }
      return make_token(lexer, TOK_INTEGER);
    }
  }
  
  // Regular decimal number
  while (is_digit(peek(lexer))) {
    advance(lexer);
  }

  // Look for decimal point
  if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
    advance(lexer); // Consume '.'
    while (is_digit(peek(lexer))) {
      advance(lexer);
    }
    return make_token(lexer, TOK_FLOAT);
  }

  return make_token(lexer, TOK_INTEGER);
}

// Check keyword
static TokenKind check_keyword(const char *start, size_t length,
                               const char *rest, TokenKind kind) {
  if (strlen(rest) == length && memcmp(start, rest, length) == 0) {
    return kind;
  }
  return TOK_IDENTIFIER;
}

// Identify keyword or identifier
static TokenKind identifier_type(Lexer *lexer) {
  size_t length = (size_t)(lexer->current - lexer->start);
  const char *start = lexer->start;

  switch (start[0]) {
  case 'a':
    return check_keyword(start, length, "as", KW_AS);
  case 'l':
    if (length == 3 && memcmp(start, "let", 3) == 0) {
      return KW_LET;
    } else if (length == 4 && memcmp(start, "loop", 4) == 0) {
      return KW_LOOP;
    }
    break;
  case 'c':
    if (length == 5 && memcmp(start, "const", 5) == 0) {
      return KW_CONST;
    } else if (length == 8 && memcmp(start, "continue", 8) == 0) {
      return KW_CONTINUE;
    } else if (length == 5 && memcmp(start, "crate", 5) == 0) {
      return KW_CRATE;
    }
    break;
  case 'f':
    if (length == 2 && memcmp(start, "fn", 2) == 0) {
      return KW_FN;
    } else if (length == 3 && memcmp(start, "f32", 3) == 0) {
      return KW_F32;
    } else if (length == 3 && memcmp(start, "f64", 3) == 0) {
      return KW_F64;
    }
    break;
  case 'i':
    if (length > 1) {
      switch (start[1]) {
      case 'f':
        return check_keyword(start, length, "if", KW_IF);
      case '8':
        return check_keyword(start, length, "i8", KW_I8);
      case '1':
        if (length == 3) return check_keyword(start, length, "i16", KW_I16);
        if (length == 4) return check_keyword(start, length, "i128", KW_I128);
        if (length == 5) return check_keyword(start, length, "i1024", KW_I1024);
        break;
      case '2':
        return check_keyword(start, length, "i256", KW_I256);
      case '3':
        return check_keyword(start, length, "i32", KW_I32);
      case '5':
        return check_keyword(start, length, "i512", KW_I512);
      case '6':
        return check_keyword(start, length, "i64", KW_I64);
      }
    }
    break;
  case 'm':
    return check_keyword(start, length, "mod", KW_MOD);
  case 'u':
    if (length == 3 && memcmp(start, "use", 3) == 0) {
      return KW_USE;
    } else if (length > 1) {
      switch (start[1]) {
      case '8':
        return check_keyword(start, length, "u8", KW_U8);
      case '1':
        if (length == 3) return check_keyword(start, length, "u16", KW_U16);
        if (length == 4) return check_keyword(start, length, "u128", KW_U128);
        if (length == 5) return check_keyword(start, length, "u1024", KW_U1024);
        break;
      case '2':
        return check_keyword(start, length, "u256", KW_U256);
      case '3':
        return check_keyword(start, length, "u32", KW_U32);
      case '5':
        return check_keyword(start, length, "u512", KW_U512);
      case '6':
        return check_keyword(start, length, "u64", KW_U64);
      }
    }
    break;
  case 'r':
    return check_keyword(start, length, "ret", KW_RET);
  case 'h':
    return check_keyword(start, length, "halt", KW_HALT);
  case 'e':
    return check_keyword(start, length, "else", KW_ELSE);
  case 'w':
    return check_keyword(start, length, "while", KW_WHILE);
  case 'p':
    if (length == 3 && memcmp(start, "ptr", 3) == 0) {
      return KW_PTR;
    } else if (length == 3 && memcmp(start, "pub", 3) == 0) {
      return KW_PUB;
    }
    break;
  case 'b':
    return check_keyword(start, length, "break", KW_BREAK);
  case 's':
    if (length == 4 && memcmp(start, "self", 4) == 0) {
      return KW_SELF;
    } else if (length == 5 && memcmp(start, "super", 5) == 0) {
      return KW_SUPER;
    }
    break;
  }

  return TOK_IDENTIFIER;
}

// Scan identifier
static Token scan_identifier(Lexer *lexer) {
  while (is_alnum(peek(lexer))) {
    advance(lexer);
  }

  TokenKind kind = identifier_type(lexer);
  return make_token(lexer, kind);
}

// Scan operator using greedy maximal matching trie lookup
static Token scan_operator(Lexer *lexer) {
  const char *start = lexer->current;

  // Calculate maximum possible operator length (up to end of source or 20
  // chars)
  size_t remaining = strlen(lexer->current);
  size_t max_length = remaining > 20 ? 20 : remaining;

  size_t matched_length = 0;
  const OperatorInfo *best_match =
      trie_lookup_greedy(start, max_length, &matched_length);

  // Check for single character punctuation that's not in operator registry
  char c = peek(lexer);

#ifdef TEST_MODE
  if (c == '/' && strlen(lexer->current) >= 2) {
    char debug_str[21] = {0};
    size_t len = strlen(lexer->current);
    size_t copy_len = len < 20 ? len : 20;
    memcpy(debug_str, lexer->current, copy_len);
    debug_str[copy_len] = '\0';
    if (strncmp(debug_str, "//", 2) == 0 && strlen(debug_str) <= 8) {
      printf("DEBUG: scan_operator for '%s', best_match=%p, matched_length=%zu\n",
             debug_str, (void *)best_match, matched_length);
    }
  }
#endif

  if (best_match && matched_length > 0) {
    // Advance by the matched length
    for (size_t i = 0; i < matched_length; i++) {
      advance(lexer);
    }

#ifdef TEST_MODE
    if (c == '/' && strlen(lexer->current) >= 2) {
      char debug_str[21] = {0};
      size_t len = strlen(lexer->current);
      size_t copy_len = len < 20 ? len : 20;
      memcpy(debug_str, lexer->current, copy_len);
      debug_str[copy_len] = '\0';
      if (strncmp(debug_str, "//", 2) == 0 && strlen(debug_str) <= 8) {
        printf("DEBUG: scan_operator returning token %d for '%s'\n",
               best_match->token, best_match->symbol);
      }
    }
#endif

    return make_token(lexer, best_match->token);
  }
  switch (c) {
  case '(':
    advance(lexer);
    return make_token(lexer, TOK_LPAREN);
  case ')':
    advance(lexer);
    return make_token(lexer, TOK_RPAREN);
  case '{':
    advance(lexer);
    return make_token(lexer, TOK_LBRACE);
  case '}':
    advance(lexer);
    return make_token(lexer, TOK_RBRACE);
  case '[':
    advance(lexer);
    return make_token(lexer, TOK_LBRACKET);
  case ']':
    advance(lexer);
    return make_token(lexer, TOK_RBRACKET);
  case ',':
    advance(lexer);
    return make_token(lexer, TOK_COMMA);
  case ';':
    advance(lexer);
    return make_token(lexer, TOK_SEMICOLON);
  case ':':
    // Check if this is := or just :
    if (peek_next(lexer) != '=') {
      advance(lexer);
      return make_token(lexer, TOK_COLON);
    }
    // Fall through to default for := operator
    break;
  case '.':
    // Check if this is .. or just .
    if (peek_next(lexer) != '.') {
      advance(lexer);
      return make_token(lexer, TOK_DOT);
    }
    // Fall through to default for .. and ..= operators
    break;
  default:
    // Check if this is a potential operator symbol
    if (strchr("<>/"
               "|\\:;!?^@%$&*~`.,_"
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
               c) != NULL) {
      // Try to read a few more characters to see if it forms a valid operator
      char potential_op[21] = {0}; // Max 20 chars + null terminator
      size_t op_len = 0;

      // Read up to 20 characters or until non-operator character
      for (size_t i = 0; i < 20 && lexer->current[i] != '\0'; i++) {
        char ch = lexer->current[i];
        if (strchr("<>/"
                   "|\\:;!?^@%$&*~`.,_"
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
                   ch) != NULL) {
          potential_op[op_len++] = ch;
        } else {
          break;
        }
      }

      // Generate error message with suggestions
      char error_msg[256];
      char suggestions[128];
      suggest_similar_operators(potential_op, suggestions, sizeof(suggestions));

      snprintf(error_msg, sizeof(error_msg),
               "Unrecognized operator symbol: '%s'%s", potential_op,
               suggestions);

      // Consume the invalid operator characters
      for (size_t i = 0; i < op_len; i++) {
        advance(lexer);
      }

      return error_token(lexer, error_msg);
    } else {
      // Truly unexpected character
      char error_msg[64];
      snprintf(error_msg, sizeof(error_msg), "Unexpected character: '%c'", c);
      advance(lexer); // Consume the character
      return error_token(lexer, error_msg);
    }
  }
  
  // If we broke out of switch (for : or . that are part of operators),
  // return an error since the operator should have been matched by trie
  char error_msg[64];
  snprintf(error_msg, sizeof(error_msg), "Unexpected character: '%c'", c);
  advance(lexer);
  return error_token(lexer, error_msg);
}

// Main lexer function
Token lexer_next_token(Lexer *lexer) {
  skip_whitespace(lexer);

  lexer->start = lexer->current;

  if (lexer_is_at_end(lexer)) {
    return make_token(lexer, TOK_EOF);
  }

  char c = peek(lexer);

  // Handle special punctuation BEFORE operator lookup
  // These need special treatment because they can be standalone or part of operators
  switch (c) {
  case ';':
    // Only standalone ; is TOK_SEMICOLON
    // Check if next char could be part of an operator
    if (peek_next(lexer) != '>' && peek_next(lexer) != ';') {
      advance(lexer);
      return make_token(lexer, TOK_SEMICOLON);
    }
    break; // Let operator lookup handle ;> and ;;
  case ':':
    // Check for :: (module path separator)
    if (peek_next(lexer) == ':') {
      advance(lexer);
      advance(lexer);
      return make_token(lexer, TOK_DOUBLE_COLON);
    }
    // Only standalone : is TOK_COLON
    // Check if next char could be part of an operator
    if (peek_next(lexer) != '=' && peek_next(lexer) != '>') {
      advance(lexer);
      return make_token(lexer, TOK_COLON);
    }
    break; // Let operator lookup handle :=, :>, etc.
  case '.':
    // Only standalone . is TOK_DOT
    // Check if next char could be part of an operator
    if (peek_next(lexer) != '.') {
      advance(lexer);
      return make_token(lexer, TOK_DOT);
    }
    break; // Let operator lookup handle .. and ..=
  case ',':
    advance(lexer);
    return make_token(lexer, TOK_COMMA);
  case '(':
    advance(lexer);
    return make_token(lexer, TOK_LPAREN);
  case ')':
    advance(lexer);
    return make_token(lexer, TOK_RPAREN);
  case '{':
    advance(lexer);
    return make_token(lexer, TOK_LBRACE);
  case '}':
    advance(lexer);
    return make_token(lexer, TOK_RBRACE);
  case '[':
    advance(lexer);
    return make_token(lexer, TOK_LBRACKET);
  case ']':
    advance(lexer);
    return make_token(lexer, TOK_RBRACKET);
  default:
    break;
  }

  // Always try operator lookup first for any character that could start an
  // operator This handles all operators including multi-character ones like
  // mem>, stack>, print>, etc.
  size_t remaining = strlen(lexer->current);
  size_t max_length = remaining > 20 ? 20 : remaining;
  size_t matched_length = 0;
  const OperatorInfo *op_match =
      trie_lookup_greedy(lexer->current, max_length, &matched_length);

#ifdef TEST_MODE
  // Debug output only for problematic operators
  if (c == '/' && strlen(lexer->current) >= 2) {
    char debug_str[21] = {0};
    size_t len = strlen(lexer->current);
    size_t copy_len = len < 20 ? len : 20;
    memcpy(debug_str, lexer->current, copy_len);
    debug_str[copy_len] = '\0';
    if (strncmp(debug_str, "//", 2) == 0 && strlen(debug_str) <= 8) {
      printf("DEBUG: Lexing '%s', trie_match=%p, matched_length=%zu\n", debug_str,
             (void *)op_match, matched_length);
      if (op_match) {
        printf("DEBUG: Found operator '%s' -> token %d\n", op_match->symbol,
               op_match->token);
      }
    }
  }
#endif

  if (op_match && matched_length > 0) {
    // Found an operator, advance by the matched length
    for (size_t i = 0; i < matched_length; i++) {
      advance(lexer);
    }

#ifdef TEST_MODE
    if (c == '/') {
      printf("DEBUG: Returning token %d for operator '%s'\n", op_match->token,
             op_match->symbol);
    }
#endif

    return make_token(lexer, op_match->token);
  }

  // No operator match, check for other token types
  if (is_alpha(c)) {
    return scan_identifier(lexer);
  }

  // Numbers
  if (is_digit(c)) {
    return scan_number(lexer);
  }

  // String literals
  if (c == '"') {
    advance(lexer); // Skip opening quote
    return scan_string(lexer);
  }

  // Character literals
  if (c == '\'') {
    advance(lexer); // Skip opening quote
    if (lexer_is_at_end(lexer)) {
      return error_token(lexer, "Unterminated character literal");
    }
    advance(lexer); // The character
    if (peek(lexer) != '\'') {
      return error_token(lexer, "Unterminated character literal");
    }
    advance(lexer); // Skip closing quote
    return make_token(lexer, TOK_CHAR);
  }

  // Operators and punctuation
  return scan_operator(lexer);
}

// Test lexer with sample FCx code
void test_lexer_functionality(void) {
  printf("=== FCx Lexer Functionality Test ===\n");

  const char *test_cases[] = {
      // Basic operators
      "a << b",              // Left shift
      "x >>> y",             // Logical right shift
      "ptr <=> (exp, new)",  // Compare-and-swap
      "mem>1024,8",          // Memory allocation
      "fd $/ buf, len",      // Write syscall
      "x !! value",          // Atomic write
      "print >>> \"hello\"", // Compact print

      // Complex expressions
      "let x := (a << 2) + b",
      "?(n<=0) -> ret 0",
      "@fibonacci <=> fn(n: i32) -> i32 { ... }",

      // Error cases
      "invalid_op_xyz", // Should suggest similar operators
      "<<>>",           // Invalid combination
  };

  size_t test_count = sizeof(test_cases) / sizeof(test_cases[0]);

  for (size_t i = 0; i < test_count; i++) {
    printf("\nTest %zu: \"%s\"\n", i + 1, test_cases[i]);

    Lexer lexer;
    lexer_init(&lexer, test_cases[i]);

    Token token;
    int token_count = 0;
    do {
      token = lexer_next_token(&lexer);
      token_count++;

      if (token.kind == TOK_ERROR) {
        printf("  ERROR: %.*s\n", (int)token.length, token.start);
        break;
      } else if (token.kind != TOK_EOF) {
        printf("  Token %d: %.*s (kind: %d)\n", token_count, (int)token.length,
               token.start, token.kind);
      }

      // Prevent infinite loops
      if (token_count > 20) {
        printf("  ... (truncated after 20 tokens)\n");
        break;
      }
    } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);

    if (token.kind == TOK_EOF) {
      printf("  Successfully tokenized (%d tokens)\n", token_count - 1);
    }
  }

  printf("\n=== Lexer Test Complete ===\n");
}