#include "src/lexer/lexer.h"
#include <stdio.h>

int main() {
    printf("OP_ATOMIC_WRITE = %d\n", OP_ATOMIC_WRITE);
    printf("OP_WRITE_SYSCALL = %d\n", OP_WRITE_SYSCALL);
    printf("OP_READ_SYSCALL = %d\n", OP_READ_SYSCALL);
    printf("OP_PRIV_ESCALATE = %d\n", OP_PRIV_ESCALATE);
    printf("OP_CAPABILITY_CHECK = %d\n", OP_CAPABILITY_CHECK);
    
    return 0;
}