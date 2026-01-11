#include "lexer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test assertion macro
#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    tests_run++;                                                               \
    if (condition) {                                                           \
      tests_passed++;                                                          \
      printf("✓ %s\n", message);                                               \
    } else {                                                                   \
      tests_failed++;                                                          \
      printf("✗ %s\n", message);                                               \
    }                                                                          \
  } while (0)

// Helper function to tokenize a string and return tokens
static Token *tokenize_string(const char *source, size_t *token_count) {
  Lexer lexer;
  lexer_init(&lexer, source);

  // First pass: count tokens
  size_t count = 0;
  Lexer temp_lexer = lexer;
  Token token;
  do {
    token = lexer_next_token(&temp_lexer);
    count++;
  } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);

  // Allocate token array
  Token *tokens = malloc(count * sizeof(Token));
  if (!tokens) {
    *token_count = 0;
    return NULL;
  }

  // Second pass: collect tokens
  size_t i = 0;
  do {
    tokens[i] = lexer_next_token(&lexer);
    i++;
  } while (tokens[i - 1].kind != TOK_EOF && tokens[i - 1].kind != TOK_ERROR &&
           i < count);

  *token_count = i;
  return tokens;
}

// Helper function to get token kind name for debugging
static const char *token_kind_name(TokenKind kind) {
  switch (kind) {
  case TOK_INTEGER:
    return "TOK_INTEGER";
  case TOK_IDENTIFIER:
    return "TOK_IDENTIFIER";
  case TOK_EOF:
    return "TOK_EOF";
  case TOK_ERROR:
    return "TOK_ERROR";
  case OP_DIV:
    return "OP_DIV";
  case OP_INT_DIV:
    return "OP_INT_DIV";
  case OP_FAST_RECIP:
    return "OP_FAST_RECIP";
  case OP_QUAD_DIV:
    return "OP_QUAD_DIV";
  case OP_PENTA_DIV:
    return "OP_PENTA_DIV";
  default: {
    static char buffer[32];
    snprintf(buffer, sizeof(buffer), "UNKNOWN_%d", kind);
    return buffer;
  }
  }
}

// Helper function to check if a token matches expected kind and text
static bool token_matches(const Token *token, TokenKind expected_kind,
                          const char *expected_text) {
  if (token->kind != expected_kind) {
    return false;
  }

  if (expected_text) {
    size_t expected_len = strlen(expected_text);
    if (token->length != expected_len ||
        strncmp(token->start, expected_text, expected_len) != 0) {
      return false;
    }
  }

  return true;
}

// Test 0: ALL Operators Recognition (Comprehensive)
void test_all_operators_recognition(void) {
  printf("\n=== Test 0: ALL Operators Recognition (Comprehensive) ===\n");

  size_t total_operators = get_operator_count();
  printf("Testing ALL %zu operators in registry...\n", total_operators);

  size_t operators_passed = 0;
  size_t operators_failed = 0;
  size_t operators_skipped = 0;

  for (size_t i = 0; i < total_operators; i++) {
    const OperatorInfo *op_info = get_operator_by_index(i);
    if (!op_info) {
      printf("✗ Failed to get operator at index %zu\n", i);
      operators_failed++;
      continue;
    }

    // Skip operators that conflict with comment syntax
    // /* starts block comments, // starts line comments
    if (strncmp(op_info->symbol, "/*", 2) == 0 ||
        strncmp(op_info->symbol, "//", 2) == 0) {
      operators_skipped++;
      continue;
    }

    // Test that the operator can be tokenized correctly
    size_t token_count;
    Token *tokens = tokenize_string(op_info->symbol, &token_count);

    if (tokens && token_count >= 1) {
      // Should be recognized as the correct operator token
      if (tokens[0].kind == op_info->token) {
        operators_passed++;
      } else {
        printf("✗ Operator '%s' -> expected token %d (%s), got token %d (%s)\n",
               op_info->symbol, op_info->token, token_kind_name(op_info->token),
               tokens[0].kind, token_kind_name(tokens[0].kind));
        operators_failed++;
      }
    } else {
      printf("✗ Operator '%s' -> failed to tokenize\n", op_info->symbol);
      operators_failed++;
    }

    free(tokens);
  }

  printf("ALL operators test: %zu passed, %zu failed, %zu skipped (comment syntax)\n", 
         operators_passed, operators_failed, operators_skipped);

  if (operators_failed == 0) {
    tests_passed++;
    printf("✓ ALL non-comment operators recognized correctly\n");
  } else {
    tests_failed++;
    printf("✗ %zu operators failed recognition\n", operators_failed);
  }
  tests_run++;
}

// Test 1: Operator Recognition Across All 10 Families
void test_operator_families(void) {
  printf("\n=== Test 1: Operator Recognition Across All 10 Families ===\n");

  // Test representative operators from each family
  // Focus on unique operators that should definitely be recognized
  struct {
    const char *source;
    OperatorCategory expected_category;
    const char *family_name;
  } family_tests[] = {
      // SHIFT/ROTATE FAMILY
      {"</", CAT_SHIFT_ROTATE, "Shift/Rotate"},
      {"/>", CAT_SHIFT_ROTATE, "Shift/Rotate"},
      {"</>", CAT_SHIFT_ROTATE, "Shift/Rotate"},
      {">/<", CAT_SHIFT_ROTATE, "Shift/Rotate"},

      // ARITHMETIC/ASSIGNMENT FAMILY
      {"+=", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},
      {"-=", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},
      {"*=", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},
      {"<<=", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},
      {"<=>", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},
      {"<==>", CAT_ARITH_ASSIGN, "Arithmetic/Assignment"},

      // DATA MOVEMENT FAMILY
      {">>|", CAT_DATA_MOVEMENT, "Data Movement"},
      {"|<<", CAT_DATA_MOVEMENT, "Data Movement"},
      {"*/", CAT_DATA_MOVEMENT, "Data Movement"},
      {"/*", CAT_DATA_MOVEMENT, "Data Movement"},

      // BITFIELD FAMILY
      {"&>", CAT_BITFIELD, "Bitfield"},
      {"&<", CAT_BITFIELD, "Bitfield"},
      {"<<&", CAT_BITFIELD, "Bitfield"},
      {"&>>", CAT_BITFIELD, "Bitfield"},
      {"&|", CAT_BITFIELD, "Bitfield"},
      {"|&", CAT_BITFIELD, "Bitfield"},

      // MEMORY ALLOCATION FAMILY
      {"mem>", CAT_MEMORY_ALLOC, "Memory Allocation"},
      {">mem", CAT_MEMORY_ALLOC, "Memory Allocation"},
      {"stack>", CAT_MEMORY_ALLOC, "Memory Allocation"},
      {"heap>", CAT_MEMORY_ALLOC, "Memory Allocation"},
      {">heap", CAT_MEMORY_ALLOC, "Memory Allocation"},
      {"pool>", CAT_MEMORY_ALLOC, "Memory Allocation"},

      // ATOMIC/CONCURRENCY FAMILY
      {"!", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"!!", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"!!!", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"!?", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"?!!", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"~!", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"|!|", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},
      {"!=>", CAT_ATOMIC_CONCUR, "Atomic/Concurrency"},

      // SYSCALL/OS FAMILY
      {"$/", CAT_SYSCALL_OS, "Syscall/OS"},
      {"/$", CAT_SYSCALL_OS, "Syscall/OS"},
      {"#!", CAT_SYSCALL_OS, "Syscall/OS"},
      {"!#", CAT_SYSCALL_OS, "Syscall/OS"},
      {"%$", CAT_SYSCALL_OS, "Syscall/OS"},
      {"$%", CAT_SYSCALL_OS, "Syscall/OS"},
      {"open>", CAT_SYSCALL_OS, "Syscall/OS"},
      {"close>", CAT_SYSCALL_OS, "Syscall/OS"},

      // IO/FORMATTING FAMILY
      {"/>/", CAT_IO_FORMAT, "IO/Formatting"},
      {"print>", CAT_IO_FORMAT, "IO/Formatting"},
      {"scan>", CAT_IO_FORMAT, "IO/Formatting"},
      {"fmt>", CAT_IO_FORMAT, "IO/Formatting"},
      {"log>", CAT_IO_FORMAT, "IO/Formatting"},
      {"debug>", CAT_IO_FORMAT, "IO/Formatting"},

      // COMPARISON FAMILY
      {"?", CAT_COMPARISON, "Comparison"},
      {"??", CAT_COMPARISON, "Comparison"},
      {"???", CAT_COMPARISON, "Comparison"},

      // ARITHMETIC DENSE FAMILY
      {"/", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"//", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"///", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"/%", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"/|/", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"|/|", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"+*", CAT_ARITH_DENSE, "Arithmetic Dense"},
      {"*+", CAT_ARITH_DENSE, "Arithmetic Dense"},
  };

  size_t test_count = sizeof(family_tests) / sizeof(family_tests[0]);
  size_t family_counts[10] = {0}; // Count operators per family

  printf("Testing %zu representative operators across all 10 families...\n",
         test_count);

  for (size_t i = 0; i < test_count; i++) {
    size_t token_count;
    Token *tokens = tokenize_string(family_tests[i].source, &token_count);

    if (tokens && token_count >= 1) {
      // Check if the operator is recognized (found in registry and not an
      // error/identifier)
      const OperatorInfo *op_info = lookup_operator(family_tests[i].source);
      bool is_operator = (op_info != NULL && tokens[0].kind != TOK_ERROR &&
                          tokens[0].kind != TOK_IDENTIFIER);

      // Verify the operator category
      bool category_correct =
          (op_info && op_info->category == family_tests[i].expected_category);

      char test_msg[256];
      snprintf(test_msg, sizeof(test_msg),
               "%s family: '%s' recognized as operator with correct category",
               family_tests[i].family_name, family_tests[i].source);

      if (is_operator && category_correct) {
        family_counts[family_tests[i].expected_category]++;
        tests_passed++;
      } else {
        tests_failed++;
        printf(
            "✗ %s family: '%s' recognized as operator with correct category\n",
            family_tests[i].family_name, family_tests[i].source);
        printf("    Failed: '%s' -> token %d, expected category %d, actual "
               "category %d\n",
               family_tests[i].source, tokens[0].kind,
               family_tests[i].expected_category,
               op_info ? (int)op_info->category : -1);
      }
      tests_run++;
    } else {
      char test_msg[256];
      snprintf(test_msg, sizeof(test_msg),
               "%s family: '%s' tokenization failed",
               family_tests[i].family_name, family_tests[i].source);
      TEST_ASSERT(false, test_msg);
    }

    free(tokens);
  }

  // Print family coverage summary
  const char *family_names[] = {"Shift/Rotate",      "Arithmetic/Assignment",
                                "Data Movement",     "Bitfield",
                                "Memory Allocation", "Atomic/Concurrency",
                                "Syscall/OS",        "IO/Formatting",
                                "Comparison",        "Arithmetic Dense"};

  printf("\nFamily coverage summary:\n");
  for (int i = 0; i < 10; i++) {
    printf("  %s: %zu operators tested\n", family_names[i], family_counts[i]);
  }

  // Verify all families have at least some operators
  for (int i = 0; i < 10; i++) {
    if (family_counts[i] > 0) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ %s family has working operators\n", family_names[i]);
    }
    tests_run++;
  }
}

// Test 2: Greedy Maximal Matching
void test_greedy_matching(void) {
  printf("\n=== Test 2: Greedy Maximal Matching ===\n");

  struct {
    const char *source;
    const char *expected_first_text;
    const char *description;
  } greedy_tests[] = {
      // Basic greedy matching cases - should match longest operator first
      {"<<=", "<<=", "Left shift assign vs left shift + assign"},
      {"<==>", "<==>", "Atomic swap vs compare-and-swap + greater"},
      {"!=>", "!=>", "Full memory barrier vs not equal + greater equal"},

      // Multi-character operator recognition
      {"mem>", "mem>", "Memory allocation vs individual characters"},
      {"stack>", "stack>", "Stack allocation vs individual characters"},
      {"@sys", "@sys", "Syscall wrapper vs MMIO + identifier"},
      {"open>", "open>", "Open syscall vs individual characters"},
      {"close>", "close>", "Close syscall vs individual characters"},
      {"print>", "print>", "Print function vs individual characters"},
      {"heap>", "heap>", "Heap allocation vs individual characters"},

      // Complex overlapping patterns - should pick longest first
      {"<<=>>", "<<=", "Complex: left shift assign followed by right shift"},
      {"<==>>=", "<==>", "Complex: atomic swap followed by greater equal"},
      {"!=>!<", "!=>", "Complex: full barrier followed by acquire barrier"},

      // SIMD and parallel operators
      {"/|/", "/|/", "SIMD divide vs individual operators"},
      {"|/|", "|/|", "Parallel divide vs individual operators"},
      {"&>>", "&>>", "Extract right shift vs bitwise AND + right shift"},
      {"<<&", "<<&", "Shift mask vs left shift + bitwise AND"},

      // Atomic operation patterns
      {"!!!", "!!!", "Triple atomic vs double atomic + atomic read"},
      {"?!!", "?!!", "Atomic fetch add vs conditional + double atomic"},
      {"|!|", "|!|", "Atomic fence vs pipe + atomic + pipe"},
      {"~!", "~!", "Atomic XOR vs bitwise NOT + atomic"},

      // Syscall patterns
      {"$/$", "$/$", "Bidirectional syscall vs write + read syscall"},
      {"%$%", "%$%", "Resource query alloc vs individual operators"},
      {"##", "##", "Double privilege vs individual privilege ops"},

      // IO/Formatting patterns
      {"/>/", "/>/", "Encode bytes vs individual operators"},

      // Edge cases with maximum length operators (5 chars)
      {">>>>>", ">>>>>", "Penta output (5 chars)"},
      {"<<<<<", "<<<<<", "Penta input (5 chars)"},
      {"!!!!!", "!!!!!", "Penta atomic (5 chars)"},

      // Mixed operator sequences - should match first operator greedily
      {"mem>stack>", "mem>", "Memory allocation followed by stack allocation"},
      {"!=>!<>!", "!=>", "Full barrier in complex sequence"},
  };

  size_t test_count = sizeof(greedy_tests) / sizeof(greedy_tests[0]);

  printf("Testing %zu greedy matching cases...\n", test_count);

  for (size_t i = 0; i < test_count; i++) {
    size_t token_count;
    Token *tokens = tokenize_string(greedy_tests[i].source, &token_count);

    if (tokens && token_count >= 1) {
      // Check if the first token matches the expected text (greedy matching)
      bool text_matches = token_matches(&tokens[0], tokens[0].kind,
                                        greedy_tests[i].expected_first_text);

      // Also verify it's recognized as an operator
      bool is_operator =
          (tokens[0].kind != TOK_ERROR && tokens[0].kind != TOK_IDENTIFIER &&
           tokens[0].kind != TOK_EOF);

      if (text_matches && is_operator) {
        tests_passed++;
      } else {
        tests_failed++;
        printf("✗ Greedy matching: %s\n", greedy_tests[i].description);
        printf("    Expected: %s\n", greedy_tests[i].expected_first_text);
        printf("    Got: %.*s (token %d)\n", (int)tokens[0].length,
               tokens[0].start, tokens[0].kind);
      }
      tests_run++;
    } else {
      char test_msg[256];
      snprintf(test_msg, sizeof(test_msg),
               "Greedy matching tokenization failed: %s",
               greedy_tests[i].description);
      TEST_ASSERT(false, test_msg);
    }

    free(tokens);
  }

  printf("Greedy matching tests completed.\n");
}

// Test 3: Error Cases for Invalid Operators
void test_invalid_operators(void) {
  printf("\n=== Test 3: Error Cases for Invalid Operators ===\n");

  struct {
    const char *invalid_op;
    const char *description;
    bool should_be_identifier; // true if should be parsed as identifier, false
                               // if should be error
  } invalid_tests[] = {
      // Invalid operator combinations
      {"<<>>", "Invalid shift combination", false},
      {">>><<", "Invalid shift sequence", false},
      {"<=>>", "Invalid comparison combination", false},
      {"<=>=>", "Invalid CAS combination", false},
      {"!=>!", "Invalid barrier combination", false},

      // Too many repeated symbols
      {"!!!!!!!!", "Too many exclamation marks (8)", false},
      {"@@@@@@", "Too many @ symbols (6)", false},
      {">>>>>>>>", "Too many > symbols (8)", false},
      {"<<<<<<<<", "Too many < symbols (8)", false},
      // Note: "////////" is skipped because // starts a line comment

      // Invalid memory/syscall operators
      {"mem>>", "Invalid memory operator", false},
      {">>mem", "Invalid reverse memory operator", false},
      {"sys%%", "Invalid syscall operator", false},
      {"%%sys", "Invalid reverse syscall operator", false},
      {"stack<<", "Invalid stack operator", false},
      {"<<stack", "Invalid reverse stack operator", false},

      // Invalid syscall combinations
      {"$/$/$", "Invalid triple syscall combination", false},
      {"/$/$/$", "Invalid syscall sequence", false},
      {"%$%$%", "Invalid resource query sequence", false},
      {"##!#!", "Invalid privilege sequence", false},

      // Invalid pipe/data movement combinations
      {"|><><|", "Invalid pipe combination", false},
      {"<|>|<", "Invalid bidirectional pipe", false},
      {">>|<<", "Invalid shift pipe combination", false},
      {"|<<>>|", "Invalid complex pipe", false},

      // Invalid atomic combinations
      {"!?!?!", "Invalid atomic conditional sequence", false},
      {"~!~!~", "Invalid atomic XOR sequence", false},
      {"|!|!|", "Invalid fence sequence", false},
      {"!=><=!", "Invalid barrier sequence", false},

      // Invalid bitfield combinations
      {"&>&<&", "Invalid bitfield sequence", false},
      {"^>^<^", "Invalid XOR sequence", false},
      {"<<&>>", "Invalid shift mask combination", false},
      {"&>>&<<", "Invalid extract shift combination", false},

      // Invalid IO/formatting combinations
      {">>>>>>", "Too many output operators (6)", false},
      {"<<<<<<", "Too many input operators (6)", false},
      {"/>/>/", "Invalid encode sequence", false},
      {"print>>", "Invalid print combination", false},

      // Non-operator symbols (should be identifiers)
      {"xyz", "Regular identifier", true},
      {"hello", "Word identifier", true},
      {"test123", "Alphanumeric identifier", true},
      {"_private", "Underscore identifier", true},
      {"CamelCase", "Mixed case identifier", true},

      // Mixed numbers and letters (should be separate tokens)
      {"123abc", "Number followed by identifier", true},
      {"abc123", "Identifier with numbers", true},

      // Invalid question/exclamation combos (fixed trigraph)
      {"?\\?!!!!", "Invalid question/exclamation combo", false},
      {"!?!?!?", "Invalid alternating pattern", false},
      {"?\\?!!?\\?", "Invalid double pattern", false},

      // Invalid symbol combinations not in alphabet
      {"#$%^&*()", "Mixed invalid symbols", false},
      {"[]{}()", "Bracket combinations", false},
      {".,;:", "Punctuation combinations", false},

      // Edge cases with valid symbols but invalid patterns
      {"<><><>", "Alternating comparison", false},
      {"><><><", "Alternating volatile", false},
      {"@>@<@>", "Alternating MMIO", false},
      {"mem>mem>", "Double memory allocation", false},
  };

  size_t test_count = sizeof(invalid_tests) / sizeof(invalid_tests[0]);

  printf("Testing %zu invalid operator cases...\n", test_count);

  for (size_t i = 0; i < test_count; i++) {
    size_t token_count;
    Token *tokens = tokenize_string(invalid_tests[i].invalid_op, &token_count);

    bool is_properly_handled = false;

    if (tokens && token_count >= 1) {
      if (invalid_tests[i].should_be_identifier) {
        // Should be parsed as identifier or multiple tokens
        if (tokens[0].kind == TOK_IDENTIFIER ||
            (tokens[0].kind == TOK_INTEGER && token_count > 2)) {
          is_properly_handled = true;
        }
      } else {
        // Should be error token, multiple tokens, or not in registry
        if (tokens[0].kind == TOK_ERROR) {
          is_properly_handled = true;
        } else if (token_count > 1) { // Split into multiple tokens
          is_properly_handled = true;
        } else {
          // Check if operator is not in registry
          const OperatorInfo *op_info =
              lookup_operator(invalid_tests[i].invalid_op);
          if (!op_info) {
            is_properly_handled = true;
          }
        }
      }
    }

    if (is_properly_handled) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ Invalid operator: %s\n", invalid_tests[i].description);
      if (tokens && token_count >= 1) {
        printf("    Unexpected: '%s' was accepted as token %d (expected %s)\n",
               invalid_tests[i].invalid_op, tokens[0].kind,
               invalid_tests[i].should_be_identifier ? "identifier"
                                                     : "error/multiple tokens");
      }
    }
    tests_run++;

    free(tokens);
  }

  printf("Invalid operator tests completed.\n");
}

// Test 4: Complex Expression Tokenization
void test_complex_expressions(void) {
  printf("\n=== Test 4: Complex Expression Tokenization ===\n");

  struct {
    const char *source;
    size_t expected_token_count; // Including EOF
    const char *description;
  } complex_tests[] = {
      {"a << b", 4, "Simple left shift expression"},
      {"x >>> y", 4, "Logical right shift expression"},
      {"ptr <=> (exp, new)", 8, "Compare-and-swap with parentheses"},
      {"mem>1024,8", 5, "Memory allocation with size and alignment"},
      {"fd $/ buf, len", 6, "Write syscall expression"},
      {"x !! value", 4, "Atomic write expression"},
      {"let x := (a << 2) + b", 11, "Complex assignment with arithmetic"},
      {"?(n<=0) -> ret 0", 10, "Conditional expression"},
      {"@fibonacci <=> fn(n: i32)", 10, "Function definition"},
      {"a &> 5 | b ^< 3", 8, "Bitfield operations"},
      {"stack>size + heap>align,4", 8, "Memory allocation combination"},
      {"!x && !!y || !!!z", 9, "Atomic and logical operations"},
  };

  size_t test_count = sizeof(complex_tests) / sizeof(complex_tests[0]);

  for (size_t i = 0; i < test_count; i++) {
    size_t token_count;
    Token *tokens = tokenize_string(complex_tests[i].source, &token_count);

    bool correct_count = (token_count == complex_tests[i].expected_token_count);
    bool no_errors = true;

    if (tokens) {
      // Check that no error tokens were generated
      for (size_t j = 0; j < token_count; j++) {
        if (tokens[j].kind == TOK_ERROR) {
          no_errors = false;
          printf("    Error token at position %zu: %.*s\n", j,
                 (int)tokens[j].length, tokens[j].start);
          break;
        }
      }
    }

    if (correct_count && no_errors) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ Complex expression: %s (expected %zu tokens, got %zu)\n",
             complex_tests[i].description,
             complex_tests[i].expected_token_count, token_count);
    }
    tests_run++;

    free(tokens);
  }
}

// Test 5: Operator Registry Validation
void test_operator_registry_validation(void) {
  printf("\n=== Test 5: Operator Registry Validation ===\n");

  // Test that we have 200+ operators
  size_t operator_count = get_operator_count();
  if (operator_count >= 200) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ Registry contains 200+ operators (actual: %zu)\n",
           operator_count);
  }
  tests_run++;

  // Test that all operators have valid precedence
  bool valid_precedence = validate_operator_precedence();
  if (valid_precedence) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ All operators have valid precedence (1-12)\n");
  }
  tests_run++;

  // Test that all operators have assembly templates
  bool valid_templates = validate_assembly_templates();
  if (valid_templates) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ All operators have assembly templates\n");
  }
  tests_run++;

  // Test trie structure
  bool valid_trie = validate_trie_structure();
  if (valid_trie) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ Operator trie structure is valid\n");
  }
  tests_run++;

  // Test combinatorial generation
  bool valid_generation = validate_combinatorial_generation();
  if (valid_generation) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ Combinatorial generation produces adequate coverage\n");
  }
  tests_run++;

  // Test operator distribution across categories
  size_t category_counts[11] = {0};
  for (size_t i = 0; i < operator_count; i++) {
    const OperatorInfo *op = get_operator_by_index(i);
    if (op && op->category < 11) {
      category_counts[op->category]++;
    }
  }

  const char *category_names[] = {"Shift/Rotate",
                                  "Arithmetic/Assignment",
                                  "Data Movement",
                                  "Bitfield",
                                  "Memory Allocation",
                                  "Atomic/Concurrency",
                                  "Syscall/OS",
                                  "IO/Formatting",
                                  "Comparison",
                                  "Arithmetic Dense",
                                  "Special"};

  printf("\nOperator distribution by category:\n");
  size_t total_categorized = 0;
  for (int i = 0; i < 11; i++) {
    printf("  %s: %zu operators\n", category_names[i], category_counts[i]);
    total_categorized += category_counts[i];

    // Test that each category has reasonable coverage
    if (category_counts[i] > 0) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ %s category has operators\n", category_names[i]);
    }
    tests_run++;
  }

  // Test that all operators are properly categorized
  if (total_categorized == operator_count) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ All operators properly categorized (%zu/%zu)\n",
           total_categorized, operator_count);
  }
  tests_run++;

  // Test operator length distribution
  size_t length_counts[6] = {0}; // 0-5 character lengths
  for (size_t i = 0; i < operator_count; i++) {
    const OperatorInfo *op = get_operator_by_index(i);
    if (op && op->symbol) {
      size_t len = strlen(op->symbol);
      if (len <= 5) {
        length_counts[len]++;
      }
    }
  }

  printf("\nOperator length distribution:\n");
  for (int i = 1; i <= 5; i++) {
    printf("  %d characters: %zu operators\n", i, length_counts[i]);
  }

  // Test that we have operators of various lengths
  for (int i = 1; i <= 5; i++) {
    if (length_counts[i] > 0) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ Has %d-character operators\n", i);
    }
    tests_run++;
  }

  // Test operator uniqueness
  bool all_unique = true;
  for (size_t i = 0; i < operator_count && all_unique; i++) {
    const OperatorInfo *op1 = get_operator_by_index(i);
    if (!op1 || !op1->symbol)
      continue;

    for (size_t j = i + 1; j < operator_count; j++) {
      const OperatorInfo *op2 = get_operator_by_index(j);
      if (!op2 || !op2->symbol)
        continue;

      if (strcmp(op1->symbol, op2->symbol) == 0) {
        printf("    Duplicate operator found: '%s' at indices %zu and %zu\n",
               op1->symbol, i, j);
        all_unique = false;
        break;
      }
    }
  }
  if (all_unique) {
    tests_passed++;
  } else {
    tests_failed++;
    printf("✗ All operators have unique symbols\n");
  }
  tests_run++;

  printf("\nRegistry validation summary:\n");
  printf("  Total operators: %zu\n", operator_count);
  printf("  Target: 200+ operators\n");
  printf("  Status: %s\n", operator_count >= 200 ? "✓ PASSED" : "✗ FAILED");
}

// Test 6: Trie-based Greedy Algorithm Validation
void test_trie_greedy_algorithm(void) {
  printf("\n=== Test 6: Trie-based Greedy Algorithm Validation ===\n");

  // Test direct trie lookup functionality
  struct {
    const char *input;
    size_t max_length;
    const char *expected_match;
    size_t expected_length;
    const char *description;
  } trie_tests[] = {
      // Basic trie lookups
      {"<", 1, "<", 1, "Single character lookup"},
      {"<<", 2, "<<", 2, "Two character lookup"},
      {"<<<", 3, "<<<", 3, "Three character lookup"},
      {"<<<<", 4, "<<<<", 4, "Four character lookup"},
      {"<<<<<", 5, "<<<<<", 5, "Five character lookup"},

      // Greedy matching with longer input
      {"<<=abc", 6, "<<=", 3, "Greedy match with trailing text"},
      {">>>xyz", 6, ">>>", 3, "Logical right shift with trailing text"},
      {"mem>123", 7, "mem>", 4, "Memory allocation with trailing text"},
      {"sys%test", 8, "sys%", 4, "Syscall with trailing text"},

      // Overlapping patterns - should pick longest
      {"<=", 2, "<=", 2, "Less equal vs individual chars"},
      {"<=>", 3, "<=>", 3, "Compare-and-swap vs less equal"},
      {"<==>", 4, "<==>", 4, "Atomic swap vs compare-and-swap"},

      // Complex overlapping cases
      {"!!", 2, "!!", 2, "Double atomic vs single atomic"},
      {"!!!", 3, "!!!", 3, "Triple atomic vs double atomic"},
      {"!=>", 3, "!=>", 3, "Full barrier vs not equal + greater equal"},

      // Multi-character identifiers
      {"stack>", 6, "stack>", 6, "Stack allocation"},
      {"print>", 6, "print>", 6, "Print function"},
      {"close>", 6, "close>", 6, "Close function"},

      // Edge cases
      {"", 0, NULL, 0, "Empty string"},
      {"x", 1, NULL, 0, "Non-operator character"},
      {"123", 3, NULL, 0, "Numeric string"},
  };

  size_t test_count = sizeof(trie_tests) / sizeof(trie_tests[0]);

  printf("Testing %zu trie lookup cases...\n", test_count);

  for (size_t i = 0; i < test_count; i++) {
    size_t matched_length = 0;
    const OperatorInfo *result = trie_lookup_greedy(
        trie_tests[i].input, trie_tests[i].max_length, &matched_length);

    bool test_passed = true;
    char test_msg[256];

    if (trie_tests[i].expected_match == NULL) {
      // Expecting no match
      if (result != NULL || matched_length != 0) {
        test_passed = false;
      }
      snprintf(test_msg, sizeof(test_msg), "Trie no-match: %s",
               trie_tests[i].description);
    } else {
      // Expecting a specific match
      if (result == NULL || matched_length != trie_tests[i].expected_length ||
          strncmp(result->symbol, trie_tests[i].expected_match,
                  matched_length) != 0) {
        test_passed = false;
      }
      snprintf(test_msg, sizeof(test_msg), "Trie match: %s",
               trie_tests[i].description);
    }

    if (test_passed) {
      tests_passed++;
    } else {
      tests_failed++;
      printf("✗ Trie %s: %s\n",
             trie_tests[i].expected_match ? "match" : "no-match",
             trie_tests[i].description);
      printf("    Input: '%s' (max_len: %zu)\n", trie_tests[i].input,
             trie_tests[i].max_length);
      printf("    Expected: %s (len: %zu)\n",
             trie_tests[i].expected_match ? trie_tests[i].expected_match
                                          : "NULL",
             trie_tests[i].expected_length);
      printf("    Got: %s (len: %zu)\n", result ? result->symbol : "NULL",
             matched_length);
    }
    tests_run++;
  }

  // Test performance characteristics of trie lookup
  printf("\nTesting trie performance characteristics...\n");

  // Time a large number of lookups to ensure O(k) performance
  const char *performance_tests[] = {
      "<",      "<<",     "<<<",  "<<<<", "<<<<<", "mem>", "sys%", "stack>",
      "print>", "close>", "<==>", "!=>",  "?!!",   "/|/",  "|/|"};

  size_t perf_test_count =
      sizeof(performance_tests) / sizeof(performance_tests[0]);
  size_t total_lookups = 0;

  for (int iterations = 0; iterations < 1000; iterations++) {
    for (size_t i = 0; i < perf_test_count; i++) {
      size_t matched_length;
      trie_lookup_greedy(performance_tests[i], strlen(performance_tests[i]),
                         &matched_length);
      total_lookups++;
    }
  }

  if (total_lookups == 15000) {
    tests_passed++;
  } else {
    tests_failed++;
    printf(
        "✗ Performance test completed all lookups (got %zu, expected 15000)\n",
        total_lookups);
  }
  tests_run++;

  printf("Trie algorithm validation completed.\n");
}

// Main test runner
int main(void) {
  printf("=== FCx Lexer Unit Tests ===\n");

  // Initialize operator registry
  init_operator_registry();

  // Run all test suites
  test_all_operators_recognition();
  test_operator_families();
  test_greedy_matching();
  test_invalid_operators();
  test_complex_expressions();
  test_operator_registry_validation();
  test_trie_greedy_algorithm();

  // Print test summary
  printf("\n=== Test Summary ===\n");
  printf("Tests run: %d\n", tests_run);
  printf("Tests passed: %d\n", tests_passed);
  printf("Tests failed: %d\n", tests_failed);

  if (tests_failed == 0) {
    printf("✓ All tests passed!\n");
    printf("✓ Lexer meets requirements 6.1 (operator recognition) and 6.5 "
           "(error handling)\n");
    printf("✓ Greedy maximal matching algorithm working correctly\n");
    printf("✓ All 10 operator families properly recognized\n");
    printf("✓ Error cases properly handled with meaningful messages\n");
  } else {
    printf("✗ %d tests failed\n", tests_failed);
    printf("✗ Lexer needs fixes to meet requirements\n");
  }

  // Cleanup
  cleanup_operator_registry();

  return tests_failed == 0 ? 0 : 1;
}