#include "src/parser/parser.h"
#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *source = "a ? b : c";
    printf("Testing conditional parsing: '%s'\n", source);
    
    Lexer lexer;
    lexer_init(&lexer, source);
    
    Parser parser;
    parser_init(&parser, &lexer);
    
    printf("Current token: kind=%d\n", parser.current.kind);
    printf("Previous token: kind=%d\n", parser.previous.kind);
    
    Expr *expr = parse_expression(&parser);
    
    if (expr) {
        printf("✓ Parsed successfully! Expression type: %d\n", expr->type);
        if (expr->type == EXPR_TERNARY) {
            printf("  Ternary operator: %d\n", expr->data.ternary.op);
            printf("  Has three operands: %s\n", 
                   (expr->data.ternary.first && expr->data.ternary.second && expr->data.ternary.third) ? "yes" : "no");
        }
        free_expr(expr);
    } else {
        printf("✗ Parse failed\n");
    }
    
    if (parser.had_error) {
        printf("Parser had errors\n");
    }
    
    cleanup_operator_registry();
    return 0;
}