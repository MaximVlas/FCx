#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *test_sources[] = {
        "ret",
        "halt", 
        "let",
        "fn",
        "if",
        "loop",
        "while"
    };
    
    for (int i = 0; i < 7; i++) {
        Lexer lexer;
        lexer_init(&lexer, test_sources[i]);
        
        Token token = lexer_next_token(&lexer);
        printf("Source: '%s' -> Token: %d\n", test_sources[i], token.kind);
    }
    
    cleanup_operator_registry();
    return 0;
}