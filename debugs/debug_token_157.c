#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    // Count through the enum to find what token 157 is
    printf("Checking tokens around 157:\n");
    printf("OP_ASSIGN = %d\n", OP_ASSIGN);
    printf("OP_ASSIGN_INFER = %d\n", OP_ASSIGN_INFER);
    printf("OP_FUNCTION_DEF = %d\n", OP_FUNCTION_DEF);
    printf("OP_CONDITIONAL = %d\n", OP_CONDITIONAL);
    printf("OP_ERROR_HANDLE = %d\n", OP_ERROR_HANDLE);
    
    // Test various colon-related inputs
    const char* tests[] = {":", ":=", ":::", "?", "="};
    for (int i = 0; i < 5; i++) {
        Lexer lexer;
        lexer_init(&lexer, tests[i]);
        Token token = lexer_next_token(&lexer);
        printf("'%s' -> token %d\n", tests[i], token.kind);
    }
    
    return 0;
}