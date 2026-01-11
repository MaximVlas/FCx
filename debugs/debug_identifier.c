#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Test simple identifier parsing
    const char* test_cases[] = {
        "ptr",
        "!ptr", 
        "a",
        "a ? b : c"
    };
    
    for (int i = 0; i < 4; i++) {
        printf("Testing: '%s'\n", test_cases[i]);
        
        Lexer lexer;
        lexer_init(&lexer, test_cases[i]);
        
        // Show tokens
        Token token;
        int token_count = 0;
        do {
            token = lexer_next_token(&lexer);
            printf("Token %d: kind=%d, text='%.*s'\n", 
                   token_count++, token.kind, (int)token.length, token.start);
        } while (token.kind != TOK_EOF && token_count < 10);
        
        // Reset lexer and test parser
        lexer_init(&lexer, test_cases[i]);
        Parser parser;
        parser_init(&parser, &lexer);
        
        Expr* expr = parse_expression(&parser);
        if (expr) {
            printf("✓ Parsed successfully (type=%d)\n", expr->type);
            free_expr(expr);
        } else {
            printf("✗ Parse failed\n");
        }
        
        if (parser.had_error) {
            printf("Parser had errors\n");
        }
        
        printf("---\n");
    }
    
    return 0;
}