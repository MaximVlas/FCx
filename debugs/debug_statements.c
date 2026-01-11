#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
  const char *failing_statements[] = {"let a := 42;", "fn test() { ret 0; }",
                                      "if x > 0 { print>x; }",
                                      "loop { break; }"};

  for (int i = 0; i < 4; i++) {
    printf("=== Testing statement: '%s' ===\n", failing_statements[i]);

    Lexer lexer;
    lexer_init(&lexer, failing_statements[i]);

    // Show tokens
    Token token;
    int token_count = 0;
    do {
      token = lexer_next_token(&lexer);
      printf("Token %d: kind=%d, text='%.*s'\n", token_count++, token.kind,
             (int)token.length, token.start);
    } while (token.kind != TOK_EOF && token_count < 15);

    // Reset and test parser
    lexer_init(&lexer, failing_statements[i]);
    Parser parser;
    parser_init(&parser, &lexer);

    printf("Calling parse_statement...\n");
    Stmt *stmt = parse_statement(&parser);
    if (stmt) {
      printf("✓ Statement parsed successfully (type=%d)\n", stmt->type);
      free_stmt(stmt);
    } else {
      printf("✗ Statement parse failed\n");
    }

    if (parser.had_error) {
      printf("Parser had errors\n");
    }

    printf("\n");
  }

  return 0;
}