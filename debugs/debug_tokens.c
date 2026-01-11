#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    printf("TOK_IDENTIFIER = %d\n", TOK_IDENTIFIER);
    printf("TOK_COLON = %d\n", TOK_COLON);
    printf("OP_CONDITIONAL = %d\n", OP_CONDITIONAL);
    printf("KW_PTR = %d\n", KW_PTR);
    
    // Test what : actually tokenizes to
    Lexer lexer;
    lexer_init(&lexer, ":");
    Token token = lexer_next_token(&lexer);
    printf("':' tokenizes to: %d\n", token.kind);
    
    return 0;
}