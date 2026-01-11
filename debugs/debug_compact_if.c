#include "src/lexer/lexer.h"
#include "src/parser/parser.h"
#include <stdio.h>

int main() {
    init_operator_registry();
    
    const char *test = "if x > 0 -> ret x;";
    printf("Testing: %s\n\n", test);
    
    Lexer lexer;
    lexer_init(&lexer, test);
    Parser parser;
    parser_init(&parser, &lexer);
    
    Stmt *stmt = parse_statement(&parser);
    if (stmt) {
        printf("Parse succeeded! Type: %d\n", stmt->type);
        free_stmt(stmt);
    } else {
        printf("Parse failed\n");
    }
    
    if (parser.had_error) {
        printf("Parser had errors\n");
    }
    
    cleanup_operator_registry();
    return 0;
}
