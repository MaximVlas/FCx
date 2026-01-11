#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *test = "a ? b : c";
    printf("Testing: %s\n\n", test);
    
    // First, check tokens
    printf("=== TOKENS ===\n");
    Lexer lexer;
    lexer_init(&lexer, test);
    
    Token token;
    do {
        token = lexer_next_token(&lexer);
        printf("Token %d: '%.*s'\n", token.kind, (int)token.length, token.start);
    } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);
    
    // Now try parsing
    printf("\n=== PARSING ===\n");
    lexer_init(&lexer, test);
    Parser parser;
    parser_init(&parser, &lexer);
    
    Expr *expr = parse_expression(&parser);
    if (expr) {
        printf("Parse succeeded!\n");
        free_expr(expr);
    } else {
        printf("Parse failed\n");
    }
    
    if (parser.had_error) {
        printf("Parser had errors\n");
    }
    
    cleanup_operator_registry();
    return 0;
}
