#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *tests[] = {
        "let a := 42;",
        "ret 0;",
        "break;",
        "print>x;"
    };
    
    for (int i = 0; i < 4; i++) {
        printf("\n=== Testing: %s ===\n", tests[i]);
        Lexer lexer;
        lexer_init(&lexer, tests[i]);
        
        Token token;
        do {
            token = lexer_next_token(&lexer);
            printf("Token %d: '%.*s' (length=%zu)\n", 
                   token.kind, (int)token.length, token.start, token.length);
        } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);
    }
    
    cleanup_operator_registry();
    return 0;
}
