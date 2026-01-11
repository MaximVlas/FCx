#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *test = "a += b";
    printf("Testing: %s\n\n", test);
    
    // First, check tokens
    printf("=== TOKENS ===\n");
    Lexer lexer;
    lexer_init(&lexer, test);
    
    Token token;
    do {
        token = lexer_next_token(&lexer);
        printf("Token %d: '%.*s' (length=%zu)\n", 
               token.kind, (int)token.length, token.start, token.length);
    } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);
    
    // Now try parsing
    printf("\n=== PARSING ===\n");
    lexer_init(&lexer, test);
    Parser parser;
    parser_init(&parser, &lexer);
    
    Expr *expr = parse_expression(&parser);
    if (expr) {
        printf("Parse succeeded! Type: %d\n", expr->type);
        free_expr(expr);
    } else {
        printf("Parse failed\n");
    }
    
    cleanup_operator_registry();
    return 0;
}
