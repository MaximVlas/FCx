#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>
#include <stdlib.h>

// Forward declare the get_rule function
extern const ParseRule *get_rule(TokenKind type);

int main() {
    printf("=== Checking parse rules ===\n");
    
    // Check if the problematic tokens have parse rules
    TokenKind tokens[] = {66, 79, 80}; // OP_ATOMIC_WRITE, OP_PRIV_ESCALATE, OP_CAPABILITY_CHECK
    const char* names[] = {"OP_ATOMIC_WRITE", "OP_PRIV_ESCALATE", "OP_CAPABILITY_CHECK"};
    
    for (int i = 0; i < 3; i++) {
        const ParseRule* rule = get_rule(tokens[i]);
        printf("%s (token %d): prefix=%p, infix=%p, precedence=%d\n", 
               names[i], tokens[i], 
               (void*)rule->prefix, (void*)rule->infix, rule->precedence);
    }
    
    printf("\n=== Testing ptr!! step by step ===\n");
    
    const char* source = "ptr!!";
    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);
    
    printf("Initial state:\n");
    printf("  Current: kind=%d, text='%.*s'\n", 
           parser.current.kind, (int)parser.current.length, parser.current.start);
    
    // Try to parse first token (ptr)
    printf("\nParsing first token...\n");
    const ParseRule* first_rule = get_rule(parser.current.kind);
    printf("  Rule for token %d: prefix=%p\n", parser.current.kind, (void*)first_rule->prefix);
    
    if (first_rule->prefix) {
        parser_advance(&parser);
        printf("  After advance: kind=%d, text='%.*s'\n", 
               parser.current.kind, (int)parser.current.length, parser.current.start);
        
        // Check rule for !!
        const ParseRule* second_rule = get_rule(parser.current.kind);
        printf("  Rule for !! (token %d): infix=%p, precedence=%d\n", 
               parser.current.kind, (void*)second_rule->infix, second_rule->precedence);
    }
    
    return 0;
}