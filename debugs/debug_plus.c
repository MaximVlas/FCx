#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *tests[] = {"+", "+=", "+ ", "+="};
    
    for (int i = 0; i < 4; i++) {
        printf("\n=== Testing: '%s' ===\n", tests[i]);
        Lexer lexer;
        lexer_init(&lexer, tests[i]);
        
        Token token = lexer_next_token(&lexer);
        printf("Token %d: '%.*s' (length=%zu)\n", 
               token.kind, (int)token.length, token.start, token.length);
    }
    
    cleanup_operator_registry();
    return 0;
}
