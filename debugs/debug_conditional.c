#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    const char* source = "a ? b : c";
    
    printf("Testing conditional: '%s'\n", source);
    
    Lexer lexer;
    lexer_init(&lexer, source);
    
    // Show all tokens
    Token token;
    int token_count = 0;
    do {
        token = lexer_next_token(&lexer);
        printf("Token %d: kind=%d, text='%.*s'\n", 
               token_count++, token.kind, (int)token.length, token.start);
    } while (token.kind != TOK_EOF && token_count < 10);
    
    // Reset and parse step by step
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    
    printf("\nStep-by-step parsing:\n");
    printf("Current token: kind=%d, text='%.*s'\n", 
           parser.current.kind, (int)parser.current.length, parser.current.start);
    
    // Parse 'a'
    Expr* first = parse_precedence(&parser, PREC_SEQUENCE);
    if (first) {
        printf("✓ Parsed first expr (type=%d)\n", first->type);
        printf("Current token after first: kind=%d, text='%.*s'\n", 
               parser.current.kind, (int)parser.current.length, parser.current.start);
        
        // Check if current token is ?
        if (parser.current.kind == 159) { // OP_CONDITIONAL
            printf("✓ Found ? operator\n");
            parser_advance(&parser);
            printf("After advancing past ?: kind=%d, text='%.*s'\n", 
                   parser.current.kind, (int)parser.current.length, parser.current.start);
            
            // Parse 'b'
            Expr* second = parse_precedence(&parser, PREC_COMBINED_ASSIGN);
            if (second) {
                printf("✓ Parsed second expr (type=%d)\n", second->type);
                printf("Current token after second: kind=%d, text='%.*s'\n", 
                       parser.current.kind, (int)parser.current.length, parser.current.start);
                
                // Check for :
                if (parser.current.kind == 157) { // TOK_COLON
                    printf("✓ Found : token\n");
                } else {
                    printf("✗ Expected :, got token kind %d\n", parser.current.kind);
                }
                
                free_expr(second);
            }
        }
        
        free_expr(first);
    }
    
    return 0;
}