#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *test_sources[] = {
        "arena>",
        "slab>",
        "mem>",
        "stack>",
        "@>",
        "ptr->>",
        "!ptr"
    };
    
    for (int i = 0; i < 7; i++) {
        printf("\nTesting: '%s'\n", test_sources[i]);
        
        Lexer lexer;
        lexer_init(&lexer, test_sources[i]);
        
        Token token;
        int count = 0;
        do {
            token = lexer_next_token(&lexer);
            printf("  Token %d: kind=%d, text='%.*s'\n", 
                   count++, token.kind, (int)token.length, token.start);
        } while (token.kind != TOK_EOF && token.kind != TOK_ERROR && count < 5);
    }
    
    cleanup_operator_registry();
    return 0;
}