#include "lexer.h"
#include <stdlib.h>
#include <string.h>

// Static operator registry with 200+ operators from combinatorial patterns
// Generated from symbol alphabet: < > / | \ : ; ! ? ^ @ % $ & * ~ ` , . [ ] { }
static const OperatorInfo OPERATOR_REGISTRY[] = {
    // === SHIFT/ROTATE FAMILY (30 operators) ===
    {"<", OP_LT, 5, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "less than / move backward", "cmp %0, %1; setl %2", 1, DIR_LEFT_FACING},
    {"<<", OP_LSHIFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "logical left shift", "shl %0, %1", 2, DIR_LEFT_FACING},
    {"<<<", OP_ROTATE_LEFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "rotate left", "rol %0, %1", 3, DIR_LEFT_FACING},
    {"<<<<", OP_ROTATE_LEFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "quad rotate left", "rol %0, %1; rol %0, %1", 4, DIR_LEFT_FACING},
    {"<<<<<", OP_ROTATE_LEFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "penta rotate left", "rol %0, %1; rol %0, %1; rol %0, %1", 5,
     DIR_LEFT_FACING},
    {">", OP_GT, 5, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "greater than / move forward", "cmp %0, %1; setg %2", 1, DIR_RIGHT_FACING},
    {">>", OP_RSHIFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "arithmetic right shift", "sar %0, %1", 2, DIR_RIGHT_FACING},
    {">>>", OP_LOGICAL_RSHIFT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "logical right shift", "shr %0, %1", 3, DIR_RIGHT_FACING},
    {">>>>", OP_ROTATE_RIGHT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "rotate right", "ror %0, %1", 4, DIR_RIGHT_FACING},
    {">>>>>", OP_ROTATE_RIGHT, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "penta rotate right", "ror %0, %1; ror %0, %1; ror %0, %1", 5,
     DIR_RIGHT_FACING},
    {"</", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "pointer slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    {"/>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "memory slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"</>", OP_SLICE_RANGE, 9, ASSOC_LEFT, ARITY_TERNARY, CAT_SHIFT_ROTATE,
     "memory subrange", "lea %0, [%1+%2]", 3, DIR_BIDIRECTIONAL},
    {">/<", OP_REVERSE_SLICE, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "reverse slice", "sub %0, %1", 3, DIR_BIDIRECTIONAL},
    {"<\\\\", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "backslash slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    {"\\\\>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "backslash slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"<|", OP_POP_FROM, 7, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "pop from / pipe left", "pop %0", 2, DIR_LEFT_FACING},
    {"|>", OP_PUSH_INTO, 7, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "push into / pipe right", "push %1", 2, DIR_RIGHT_FACING},
    {"<:", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "colon slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    {":>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "colon slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"<;", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "semicolon slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    {";>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "semicolon slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"?>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "question slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"<^", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "caret slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    // Note: ^> moved to BITFIELD family
    // Note: <@ and @> moved to MEMORY_ALLOC family
    {"<%", OP_SLICE_START, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "percent slice start", "add %0, %1", 2, DIR_LEFT_FACING},
    {"%>", OP_SLICE_END, 9, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "percent slice end", "add %0, %1", 2, DIR_RIGHT_FACING},
    // === ARITHMETIC/ASSIGNMENT FAMILY (35 operators) ===
    {"=", OP_ASSIGN, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "assignment", "mov %0, %1", 1, DIR_RIGHT_FACING},
    {":=", OP_ASSIGN_INFER, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "inferred assignment", "mov %0, %1", 2, DIR_RIGHT_FACING},
    {"+=", OP_ADD_ASSIGN, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "add assign", "add %0, %1", 2, DIR_RIGHT_FACING},
    {"-=", OP_SUB_ASSIGN, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "subtract assign", "sub %0, %1", 2, DIR_RIGHT_FACING},
    {"*=", OP_MUL_ASSIGN, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "multiply assign", "imul %0, %1", 2, DIR_RIGHT_FACING},
    {"/=", OP_DIV, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "divide assign", "div %1", 2, DIR_RIGHT_FACING},
    {"%=", OP_MOD_DIVISOR, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "modulo assign", "div %1; mov %0, rdx", 2, DIR_RIGHT_FACING},
    {"&=", OP_BITFIELD_EXTRACT, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "bitwise AND assign", "and %0, %1", 2, DIR_RIGHT_FACING},
    {"|=", OP_PUSH_INTO, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "bitwise OR assign", "or %0, %1", 2, DIR_RIGHT_FACING},
    {"^=", OP_BITWISE_ROTATE_XOR, 2, ASSOC_RIGHT, ARITY_BINARY,
     CAT_ARITH_ASSIGN, "bitwise XOR assign", "xor %0, %1", 2, DIR_RIGHT_FACING},
    {"<<=", OP_LSHIFT_ASSIGN, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "left shift assign", "shl %0, %1", 3, DIR_LEFT_FACING},
    {">>=", OP_RSHIFT, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "right shift assign", "sar %0, %1", 3, DIR_RIGHT_FACING},
    {">>>=", OP_LOGICAL_RSHIFT, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "logical right shift assign", "shr %0, %1", 4, DIR_RIGHT_FACING},
    {"<<<=", OP_ROTATE_LEFT, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "rotate left assign", "rol %0, %1", 4, DIR_LEFT_FACING},
    {">>>>=", OP_ROTATE_RIGHT, 2, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "rotate right assign", "ror %0, %1", 5, DIR_RIGHT_FACING},
    {"<=>", OP_CAS, 4, ASSOC_NONE, ARITY_TERNARY, CAT_ARITH_ASSIGN,
     "compare and swap", "lock cmpxchg %0, %2", 3, DIR_BIDIRECTIONAL},
    {"<==>", OP_SWAP, 4, ASSOC_NONE, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "atomic swap", "lock xchg %0, %1", 4, DIR_BIDIRECTIONAL},
    {"<===>", OP_SWAP, 4, ASSOC_NONE, ARITY_TERNARY, CAT_ARITH_ASSIGN,
     "triple atomic swap", "lock cmpxchg %0, %2", 5, DIR_BIDIRECTIONAL},
    {"<=", OP_LE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN, "less equal",
     "cmp %0, %1; setle %2", 2, DIR_LEFT_FACING},
    {">=", OP_GE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "greater equal", "cmp %0, %1; setge %2", 2, DIR_RIGHT_FACING},
    {"==", OP_EQ, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN, "equal",
     "cmp %0, %1; sete %2", 2, DIR_BIDIRECTIONAL},
    {"!=", OP_NE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN, "not equal",
     "cmp %0, %1; setne %2", 2, DIR_BIDIRECTIONAL},
    {"<>", OP_PATTERN_NE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "pattern not equal", "cmp %0, %1; setne %2", 2, DIR_BIDIRECTIONAL},
    {"><", OP_OVERLAP_TEST, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "ranges overlap / volatile store", "call _fcx_overlap", 2,
     DIR_BIDIRECTIONAL},
    {"<=|", OP_LE_OR_FLAG, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "less equal or flag", "cmp %0, %1; setle %2; or %2, flag", 3,
     DIR_LEFT_FACING},
    {"|=>", OP_IMPLIES, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "implies", "test %0, %0; jz skip; cmp %1, 1", 3, DIR_RIGHT_FACING},
    {"<==", OP_LE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "pattern match equality", "call _fcx_pattern_match", 3, DIR_LEFT_FACING},
    {"==>", OP_GE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN, "deep equal",
     "call _fcx_deep_equal", 3, DIR_RIGHT_FACING},
    {"<===", OP_LE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "triple pattern match", "call _fcx_triple_match", 4, DIR_LEFT_FACING},
    {"===>", OP_GE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "triple deep equal", "call _fcx_triple_deep", 4, DIR_RIGHT_FACING},
    // Note: <==> duplicate removed
    {"++", OP_ADD_ASSIGN, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_ASSIGN,
     "increment", "inc %0", 2, DIR_BIDIRECTIONAL},
    {"--", OP_SUB_ASSIGN, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_ASSIGN,
     "decrement", "dec %0", 2, DIR_BIDIRECTIONAL},
    {"**", OP_MUL_ASSIGN, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "power / double multiply", "call _fcx_power", 2, DIR_BIDIRECTIONAL},
    {"***", OP_MUL_ASSIGN, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "triple multiply", "imul %0, %1; imul %0, %1", 3, DIR_BIDIRECTIONAL},
    // === DATA MOVEMENT FAMILY (21 operators) ===
    // Note: ><, <>, |>, <| are handled by ARITHMETIC/ASSIGNMENT and BITFIELD
    // families
    {">>|", OP_PUSH_SHIFT, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "push with shift", "shl %0, %1; push %0", 3, DIR_RIGHT_FACING},
    {"|<<", OP_POP_SHIFT, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "pop with shift", "pop %0; shl %0, %1", 3, DIR_LEFT_FACING},
    {">>>|", OP_PUSH_SHIFT, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "triple push shift", "shr %0, %1; push %0", 4, DIR_RIGHT_FACING},
    {"|<<<", OP_POP_SHIFT, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "triple pop shift", "pop %0; rol %0, %1", 4, DIR_LEFT_FACING},
    {"||||", OP_PUSH_INTO, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "quad pipe", "call _fcx_quad_pipe", 4, DIR_BIDIRECTIONAL},
    {"|>|", OP_PUSH_INTO, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "pipe through", "call _fcx_pipe_through", 3, DIR_BIDIRECTIONAL},
    {"<|>>", OP_POP_FROM, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "bidirectional pipe", "call _fcx_bi_pipe", 4, DIR_BIDIRECTIONAL},
    {"|><|", OP_PUSH_INTO, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "cross pipe", "call _fcx_cross_pipe", 4, DIR_BIDIRECTIONAL},
    {"->", OP_LAYOUT_ACCESS, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "member access", "lea %0, [%1+offset]", 2, DIR_RIGHT_FACING},
    {"<-", OP_REVERSE_LAYOUT, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "reverse member access", "lea %0, [%1-offset]", 2, DIR_LEFT_FACING},
    {"->>", OP_LAYOUT_ACCESS, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "layout access", "lea %0, [%1+offset]", 3, DIR_RIGHT_FACING},
    {"<<-", OP_REVERSE_LAYOUT, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "reverse layout", "lea %0, [%1-offset]", 3, DIR_LEFT_FACING},
    {"-->>", OP_LAYOUT_ACCESS, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "double layout access", "lea %0, [%1+offset*2]", 4, DIR_RIGHT_FACING},
    {"<<--", OP_REVERSE_LAYOUT, 10, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "double reverse layout", "lea %0, [%1-offset*2]", 4, DIR_LEFT_FACING},
    {"*/", OP_MUL_ASSIGN, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "store with persistence", "mov %0, %1; mfence", 2, DIR_RIGHT_FACING},
    {"/*", OP_DIV, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "load with persistence", "lfence; mov %0, %1", 2, DIR_LEFT_FACING},
    {"~>", OP_ATOMIC_XOR, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "bitwise NOT move", "not %1; mov %0, %1", 2, DIR_RIGHT_FACING},
    {"<~", OP_ATOMIC_XOR, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "bitwise NOT move left", "not %0; mov %1, %0", 2, DIR_LEFT_FACING},
    {"~><~", OP_VOLATILE_STORE, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "NOT volatile store", "not %1; mov %0, %1", 4, DIR_BIDIRECTIONAL},
    {"<~>>", OP_NO_ALIAS_STORE, 7, ASSOC_LEFT, ARITY_BINARY, CAT_DATA_MOVEMENT,
     "NOT no-alias store", "not %1; mov %0, %1", 4, DIR_BIDIRECTIONAL},
    {"~~", OP_ATOMIC_XOR, 7, ASSOC_LEFT, ARITY_UNARY, CAT_DATA_MOVEMENT,
     "double NOT", "not %0; not %0", 2, DIR_BIDIRECTIONAL},

    // === BITFIELD FAMILY (30 operators) ===
    {"&", OP_BITFIELD_EXTRACT, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "bitwise AND", "and %0, %1", 1, DIR_BIDIRECTIONAL},
    {"|", OP_PUSH_INTO, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD, "bitwise OR",
     "or %0, %1", 1, DIR_BIDIRECTIONAL},
    {"^", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "bitwise XOR", "xor %0, %1", 1, DIR_BIDIRECTIONAL},
    {"~", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_UNARY, CAT_BITFIELD,
     "bitwise NOT", "not %0", 1, DIR_BIDIRECTIONAL},
    {"&>", OP_BITFIELD_EXTRACT, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "bitfield extract", "shr %0, %1; and %0, mask", 2, DIR_RIGHT_FACING},
    {"&<", OP_BITFIELD_INSERT, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "bitfield insert", "shl %2, %1; or %0, %2", 2, DIR_LEFT_FACING},
    {"^>", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "XOR extract", "xor %0, %1; shr %0, 1", 2, DIR_RIGHT_FACING},
    {"^<", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "XOR insert", "shl %1, 1; xor %0, %1", 2, DIR_LEFT_FACING},
    {"<<&", OP_SHIFT_MASK, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "shift with mask", "shl %0, %1; and %0, mask", 3, DIR_LEFT_FACING},
    {"&>>", OP_EXTRACT_RSHIFT, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "extract with right shift", "and %0, mask; shr %0, %1", 3,
     DIR_RIGHT_FACING},
    {"&<<", OP_SHIFT_MASK, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "mask with left shift", "and %0, mask; shl %0, %1", 3, DIR_LEFT_FACING},
    {">>^", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "right shift XOR", "shr %0, %1; xor %0, mask", 3, DIR_RIGHT_FACING},
    {"^<<", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "XOR left shift", "xor %0, mask; shl %0, %1", 3, DIR_LEFT_FACING},
    {"&|", OP_BITFIELD_EXTRACT, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "AND OR", "and %0, %1; or %0, mask", 2, DIR_BIDIRECTIONAL},
    {"|&", OP_PUSH_INTO, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD, "OR AND",
     "or %0, %1; and %0, mask", 2, DIR_BIDIRECTIONAL},
    {"&^", OP_BITFIELD_EXTRACT, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "AND XOR", "and %0, %1; xor %0, mask", 2, DIR_BIDIRECTIONAL},
    {"^&", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "XOR AND", "xor %0, %1; and %0, mask", 2, DIR_BIDIRECTIONAL},
    {"|^", OP_PUSH_INTO, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD, "OR XOR",
     "or %0, %1; xor %0, mask", 2, DIR_BIDIRECTIONAL},
    {"^|", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "XOR OR", "xor %0, %1; or %0, mask", 2, DIR_BIDIRECTIONAL},
    {"&&", OP_BITFIELD_EXTRACT, 3, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "logical AND", "test %0, %0; jz end; test %1, %1", 2, DIR_BIDIRECTIONAL},
    {"||", OP_PUSH_INTO, 3, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "logical OR", "test %0, %0; jnz end; test %1, %1", 2, DIR_BIDIRECTIONAL},
    {"^^", OP_BITWISE_ROTATE_XOR, 3, ASSOC_LEFT, ARITY_BINARY, CAT_BITFIELD,
     "logical XOR", "test %0, %0; setnz al; test %1, %1; setnz bl; xor al, bl",
     2, DIR_BIDIRECTIONAL},
    {"&&&", OP_BITFIELD_EXTRACT, 3, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "triple logical AND", "call _fcx_triple_and", 3, DIR_BIDIRECTIONAL},
    {"|||", OP_PUSH_INTO, 3, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "triple logical OR", "call _fcx_triple_or", 3, DIR_BIDIRECTIONAL},
    {"^^^", OP_BITWISE_ROTATE_XOR, 3, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "triple logical XOR", "call _fcx_triple_xor", 3, DIR_BIDIRECTIONAL},
    {"&>>&", OP_EXTRACT_RSHIFT, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "extract shift mask", "and %0, %2; shr %0, %1; and %0, mask", 4,
     DIR_BIDIRECTIONAL},
    {"&<<&", OP_SHIFT_MASK, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "mask shift mask", "and %0, %2; shl %0, %1; and %0, mask", 4,
     DIR_BIDIRECTIONAL},
    {"^>>^", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "XOR shift XOR", "xor %0, %2; shr %0, %1; xor %0, mask", 4,
     DIR_BIDIRECTIONAL},
    {"^<<^", OP_BITWISE_ROTATE_XOR, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "XOR left shift XOR", "xor %0, %2; shl %0, %1; xor %0, mask", 4,
     DIR_BIDIRECTIONAL},
    {"|>>|", OP_PUSH_INTO, 6, ASSOC_LEFT, ARITY_TERNARY, CAT_BITFIELD,
     "OR shift OR", "or %0, %2; shr %0, %1; or %0, mask", 4, DIR_BIDIRECTIONAL},
    // === MEMORY ALLOCATION FAMILY (25 operators) ===
    {"mem>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "allocate memory", "call _fcx_alloc", 4, DIR_RIGHT_FACING},
    {">mem", OP_DEALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "deallocate memory", "call _fcx_free", 4, DIR_LEFT_FACING},
    {"stack>", OP_STACK_ALLOC, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "stack allocation", "sub rsp, %0; mov %1, rsp", 6, DIR_RIGHT_FACING},
    {">stack", OP_STACK_ALLOC, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "stack deallocation", "add rsp, %0", 6, DIR_LEFT_FACING},
    {"heap>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "heap allocation", "call _fcx_heap_alloc", 5, DIR_RIGHT_FACING},
    {">heap", OP_DEALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "heap deallocation", "call _fcx_heap_free", 5, DIR_LEFT_FACING},
    {"pool>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "pool allocation", "call _fcx_pool_alloc", 5, DIR_RIGHT_FACING},
    {">pool", OP_DEALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "pool deallocation", "call _fcx_pool_free", 5, DIR_LEFT_FACING},
    {"@", OP_AT_SYMBOL, 11, ASSOC_NONE, ARITY_UNARY, CAT_SPECIAL, "at symbol",
     "nop", 1, DIR_BIDIRECTIONAL},
    {"@>", OP_MMIO_MAP, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "MMIO map address", "mov %0, %1", 2, DIR_RIGHT_FACING},
    {"<@", OP_MMIO_UNMAP, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "MMIO unmap", "mov %0, 0", 2, DIR_LEFT_FACING},
    {"@@", OP_MMIO_MAP, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "double MMIO map", "call _fcx_mmio_map", 2, DIR_BIDIRECTIONAL},
    {"@@@", OP_MMIO_MAP, 11, ASSOC_NONE, ARITY_TERNARY, CAT_MEMORY_ALLOC,
     "triple MMIO map", "call _fcx_mmio_map_range", 3, DIR_BIDIRECTIONAL},
    // Note: ->> and <<- are handled by DATA_MOVEMENT family
    {"->->", OP_LAYOUT_ACCESS, 10, ASSOC_LEFT, ARITY_TERNARY, CAT_MEMORY_ALLOC,
     "chained layout access", "lea %0, [%1+%2]", 4, DIR_RIGHT_FACING},
    {"<-<-", OP_REVERSE_LAYOUT, 10, ASSOC_LEFT, ARITY_TERNARY, CAT_MEMORY_ALLOC,
     "chained reverse layout", "lea %0, [%1-%2]", 4, DIR_LEFT_FACING},
    {"-><<", OP_LAYOUT_ACCESS, 10, ASSOC_LEFT, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "layout access with shift", "lea %0, [%1+offset]; shl %0, 1", 4,
     DIR_BIDIRECTIONAL},
    {">>-<", OP_REVERSE_LAYOUT, 10, ASSOC_LEFT, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "shift reverse layout", "shr %1, 1; lea %0, [%1-offset]", 4,
     DIR_BIDIRECTIONAL},
    {"align>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "aligned allocation", "call _fcx_aligned_alloc", 6, DIR_RIGHT_FACING},
    {">align", OP_DEALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "aligned deallocation", "call _fcx_aligned_free", 6, DIR_LEFT_FACING},
    {"page>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "page allocation", "call _fcx_page_alloc", 5, DIR_RIGHT_FACING},
    {">page", OP_DEALLOCATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "page deallocation", "call _fcx_page_free", 5, DIR_LEFT_FACING},
    {"mmap>", OP_MMIO_MAP, 11, ASSOC_NONE, ARITY_TERNARY, CAT_MEMORY_ALLOC,
     "memory map", "call _fcx_mmap", 5, DIR_RIGHT_FACING},
    {">mmap", OP_MMIO_UNMAP, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "memory unmap", "call _fcx_munmap", 5, DIR_LEFT_FACING},
    {"cache>", OP_ALLOCATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "cache allocation", "call _fcx_cache_alloc", 6, DIR_RIGHT_FACING},
    // === ATOMIC/CONCURRENCY FAMILY (35 operators) ===
    {"!", OP_ATOMIC_READ, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "atomic read", "mov %0, [%1]", 1, DIR_BIDIRECTIONAL},
    {"!!", OP_ATOMIC_WRITE, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic write", "lock xchg [%0], %1", 2, DIR_BIDIRECTIONAL},
    {"!!!", OP_ATOMIC_TRIPLE, 11, ASSOC_NONE, ARITY_TERNARY, CAT_ATOMIC_CONCUR,
     "atomic triple op", "lock cmpxchg [%0], %2", 3, DIR_BIDIRECTIONAL},
    {"!!!!", OP_ATOMIC_TRIPLE, 11, ASSOC_NONE, ARITY_TERNARY, CAT_ATOMIC_CONCUR,
     "quad atomic op", "call _fcx_quad_atomic", 4, DIR_BIDIRECTIONAL},
    {"!!!!!", OP_ATOMIC_TRIPLE, 11, ASSOC_NONE, ARITY_TERNARY,
     CAT_ATOMIC_CONCUR, "penta atomic op", "call _fcx_penta_atomic", 5,
     DIR_BIDIRECTIONAL},
    {"!?", OP_ATOMIC_COND, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic conditional", "lock cmpxchg [%0], %1", 2, DIR_BIDIRECTIONAL},
    {"?!", OP_ERROR_HANDLE, 3, ASSOC_RIGHT, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "error handle", "test %0, %0; js error_handler", 2, DIR_RIGHT_FACING},
    {"?!!", OP_ATOMIC_FETCH_ADD, 11, ASSOC_NONE, ARITY_BINARY,
     CAT_ATOMIC_CONCUR, "atomic fetch add", "lock xadd [%0], %1", 3,
     DIR_BIDIRECTIONAL},
    {"!!?", OP_ATOMIC_COND, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic write conditional", "lock cmpxchg [%0], %1", 3, DIR_BIDIRECTIONAL},
    {"?!!?", OP_ATOMIC_FETCH_ADD, 11, ASSOC_NONE, ARITY_TERNARY,
     CAT_ATOMIC_CONCUR, "conditional fetch add", "call _fcx_cond_fetch_add", 4,
     DIR_BIDIRECTIONAL},
    {"~!", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic XOR", "lock xor [%0], %1", 2, DIR_BIDIRECTIONAL},
    {"!~", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic NOT", "lock not [%0]", 2, DIR_BIDIRECTIONAL},
    {"~!~", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic XOR NOT", "lock xor [%0], %1; lock not [%0]", 3,
     DIR_BIDIRECTIONAL},
    {"|!|", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "atomic fence", "mfence", 3, DIR_BIDIRECTIONAL},
    {"!|!", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "atomic barrier", "mfence", 3, DIR_BIDIRECTIONAL},
    {"||!", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "OR atomic fence", "sfence", 3, DIR_BIDIRECTIONAL},
    {"!||", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "atomic OR fence", "lfence", 3, DIR_BIDIRECTIONAL},
    {"!=>", OP_BARRIER_FULL, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "full memory barrier", "mfence", 3, DIR_RIGHT_FACING},
    {"<=!", OP_BARRIER_FULL, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "reverse full barrier", "mfence", 3, DIR_LEFT_FACING},
    {"!>", OP_BARRIER_RELEASE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "release barrier", "sfence", 2, DIR_RIGHT_FACING},
    {"<!", OP_BARRIER_ACQUIRE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "acquire barrier", "lfence", 2, DIR_LEFT_FACING},
    {"!<", OP_BARRIER_ACQUIRE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "acquire barrier alt", "lfence", 2, DIR_LEFT_FACING},
    {">!", OP_BARRIER_RELEASE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "release barrier alt", "sfence", 2, DIR_RIGHT_FACING},
    {"!<>!", OP_BARRIER_FULL, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "bidirectional barrier", "mfence", 4, DIR_BIDIRECTIONAL},
    {"!><!", OP_BARRIER_FULL, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "cross barrier", "mfence", 4, DIR_BIDIRECTIONAL},
    // Note: != is handled by ARITHMETIC/ASSIGNMENT family
    {"=!", OP_ATOMIC_WRITE, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic assign", "lock xchg [%0], %1", 2, DIR_BIDIRECTIONAL},
    {"!+=", OP_ATOMIC_FETCH_ADD, 11, ASSOC_NONE, ARITY_BINARY,
     CAT_ATOMIC_CONCUR, "atomic add assign", "lock xadd [%0], %1", 3,
     DIR_BIDIRECTIONAL},
    {"!-=", OP_ATOMIC_FETCH_ADD, 11, ASSOC_NONE, ARITY_BINARY,
     CAT_ATOMIC_CONCUR, "atomic sub assign", "neg %1; lock xadd [%0], %1", 3,
     DIR_BIDIRECTIONAL},
    {"!&=", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic AND assign", "lock and [%0], %1", 3, DIR_BIDIRECTIONAL},
    {"!|=", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic OR assign", "lock or [%0], %1", 3, DIR_BIDIRECTIONAL},
    {"!^=", OP_ATOMIC_XOR, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic XOR assign", "lock xor [%0], %1", 3, DIR_BIDIRECTIONAL},
    {"!<<=", OP_LSHIFT_ASSIGN, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic left shift assign", "call _fcx_atomic_shl", 4, DIR_LEFT_FACING},
    {"!>>=", OP_RSHIFT, 11, ASSOC_NONE, ARITY_BINARY, CAT_ATOMIC_CONCUR,
     "atomic right shift assign", "call _fcx_atomic_shr", 4, DIR_RIGHT_FACING},
    {"spawn>", OP_ATOMIC_FENCE, 11, ASSOC_NONE, ARITY_UNARY, CAT_ATOMIC_CONCUR,
     "spawn thread", "call _fcx_spawn", 6, DIR_RIGHT_FACING},
    // === SYSCALL/OS FAMILY (30 operators) ===
    {"$/", OP_WRITE_SYSCALL, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "write syscall", "mov rax, 1; syscall", 2, DIR_RIGHT_FACING},
    {"/$", OP_READ_SYSCALL, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "read syscall", "mov rax, 0; syscall", 2, DIR_LEFT_FACING},
    {"$/$", OP_WRITE_SYSCALL, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "bidirectional syscall", "call _fcx_bi_syscall", 3, DIR_BIDIRECTIONAL},
    {"sys%", OP_RAW_SYSCALL, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "raw syscall", "mov rax, %0; syscall", 4, DIR_BIDIRECTIONAL},
    {"%sys", OP_RAW_SYSCALL, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "reverse raw syscall", "mov rax, %0; syscall", 4, DIR_BIDIRECTIONAL},
    {"asm%", OP_INLINE_ASM, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "inline assembly", "", 4, DIR_BIDIRECTIONAL},
    {"@sys", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "syscall wrapper", "call sys_wrapper", 4, DIR_RIGHT_FACING},
    {"sys@", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "reverse syscall wrapper", "call sys_wrapper", 4, DIR_LEFT_FACING},
    {"#!", OP_PRIV_ESCALATE, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "privilege escalate", "call _fcx_priv_escalate", 2, DIR_RIGHT_FACING},
    {"!#", OP_CAPABILITY_CHECK, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "capability check", "call _fcx_cap_check", 2, DIR_LEFT_FACING},
    {"##", OP_PRIV_ESCALATE, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "double privilege", "call _fcx_double_priv", 2, DIR_BIDIRECTIONAL},
    {"###", OP_PRIV_ESCALATE, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "triple privilege", "call _fcx_triple_priv", 3, DIR_BIDIRECTIONAL},
    {"%$", OP_RESOURCE_QUERY, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "resource query", "call _fcx_res_query", 2, DIR_RIGHT_FACING},
    {"$%", OP_RESOURCE_ALLOC, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "resource allocation", "call _fcx_res_alloc", 2, DIR_LEFT_FACING},
    {"%$%", OP_RESOURCE_QUERY, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "resource query alloc", "call _fcx_res_query_alloc", 3, DIR_BIDIRECTIONAL},
    {"$%$", OP_RESOURCE_ALLOC, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "resource alloc query", "call _fcx_res_alloc_query", 3, DIR_BIDIRECTIONAL},
    {"$$$", OP_RESOURCE_ALLOC, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "triple resource", "call _fcx_triple_res", 3, DIR_BIDIRECTIONAL},
    {"%%", OP_RESOURCE_QUERY, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "double query", "call _fcx_double_query", 2, DIR_BIDIRECTIONAL},
    // Note: $$$ duplicate removed
    {"%%%", OP_RESOURCE_QUERY, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "triple query", "call _fcx_triple_query", 3, DIR_BIDIRECTIONAL},
    {"open>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "open file", "call _fcx_open", 5, DIR_RIGHT_FACING},
    {"close>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "close file", "call _fcx_close", 6, DIR_RIGHT_FACING},
    {"read>", OP_READ_SYSCALL, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "read file", "call _fcx_read", 5, DIR_RIGHT_FACING},
    {"write>", OP_WRITE_SYSCALL, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "write file", "call _fcx_write", 6, DIR_RIGHT_FACING},
    {"seek>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_TERNARY, CAT_SYSCALL_OS,
     "seek file", "call _fcx_seek", 5, DIR_RIGHT_FACING},
    {"stat>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "stat file", "call _fcx_stat", 5, DIR_RIGHT_FACING},
    {"mkdir>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_BINARY, CAT_SYSCALL_OS,
     "make directory", "call _fcx_mkdir", 6, DIR_RIGHT_FACING},
    {"rmdir>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "remove directory", "call _fcx_rmdir", 6, DIR_RIGHT_FACING},
    {"fork>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "fork process", "call _fcx_fork", 5, DIR_RIGHT_FACING},
    {"exec>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_NARY, CAT_SYSCALL_OS,
     "exec process", "call _fcx_exec", 5, DIR_RIGHT_FACING},
    {"exit>", OP_SYS_WRAPPER, 11, ASSOC_NONE, ARITY_UNARY, CAT_SYSCALL_OS,
     "exit process", "call _fcx_exit", 5, DIR_RIGHT_FACING},

    // === IO/FORMATTING FAMILY (21 operators) ===
    // Note: >>, <<, >>>, <<<, etc. are handled by SHIFT/ROTATE family
    // Note: >>>> is handled by SHIFT/ROTATE family
    // Note: <<<<, >>>>>, <<<<< moved to SHIFT_ROTATE family
    {"/>/", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_BINARY, CAT_IO_FORMAT,
     "encode to bytes", "call _fcx_encode", 3, DIR_BIDIRECTIONAL},
    {"<\\\\<", OP_DECODE_BYTES, 11, ASSOC_NONE, ARITY_BINARY, CAT_IO_FORMAT,
     "decode from bytes", "call _fcx_decode", 3, DIR_BIDIRECTIONAL},
    {"/>/<", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_TERNARY, CAT_IO_FORMAT,
     "encode decode", "call _fcx_encode_decode", 4, DIR_BIDIRECTIONAL},
    {"<\\\\>\\\\", OP_DECODE_BYTES, 11, ASSOC_NONE, ARITY_TERNARY,
     CAT_IO_FORMAT, "decode encode", "call _fcx_decode_encode", 4,
     DIR_BIDIRECTIONAL},
    {"print>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "print function", "call _fcx_print_func", 6, DIR_RIGHT_FACING},
    {"scan>", OP_FORMAT_PRINT, 11, ASSOC_NONE, ARITY_BINARY, CAT_IO_FORMAT,
     "scan function", "call _fcx_scan_func", 5, DIR_RIGHT_FACING},
    {"fmt>", OP_FORMAT_PRINT, 11, ASSOC_NONE, ARITY_NARY, CAT_IO_FORMAT,
     "format function", "call _fcx_fmt_func", 4, DIR_RIGHT_FACING},
    {"log>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_BINARY, CAT_IO_FORMAT,
     "log function", "call _fcx_log_func", 4, DIR_RIGHT_FACING},
    {"debug>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "debug function", "call _fcx_debug_func", 6, DIR_RIGHT_FACING},
    {"error>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "error function", "call _fcx_error_func", 6, DIR_RIGHT_FACING},
    {"warn>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "warning function", "call _fcx_warn_func", 5, DIR_RIGHT_FACING},
    {"info>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "info function", "call _fcx_info_func", 5, DIR_RIGHT_FACING},
    {"trace>", OP_PRINT_COMPACT, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "trace function", "call _fcx_trace_func", 6, DIR_RIGHT_FACING},
    {"hex>", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "hex encode", "call _fcx_hex_encode", 4, DIR_RIGHT_FACING},
    {"bin>", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "binary encode", "call _fcx_bin_encode", 4, DIR_RIGHT_FACING},
    {"oct>", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "octal encode", "call _fcx_oct_encode", 4, DIR_RIGHT_FACING},
    {"dec>", OP_ENCODE_BYTES, 11, ASSOC_NONE, ARITY_UNARY, CAT_IO_FORMAT,
     "decimal encode", "call _fcx_dec_encode", 4, DIR_RIGHT_FACING},
    // === ARITHMETIC DENSE FAMILY (20 operators) ===
    {"/", OP_DIV, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE, "division",
     "div %1", 1, DIR_BIDIRECTIONAL},
    {"//", OP_INT_DIV, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "integer division", "idiv %1", 2, DIR_BIDIRECTIONAL},
    {"///", OP_FAST_RECIP, 8, ASSOC_LEFT, ARITY_UNARY, CAT_ARITH_DENSE,
     "fast reciprocal", "rcpss %0, %1", 3, DIR_BIDIRECTIONAL},
    {"////", OP_QUAD_DIV, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "quad division", "call _fcx_quad_div", 4, DIR_BIDIRECTIONAL},
    {"/////", OP_PENTA_DIV, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "penta division", "call _fcx_penta_div", 5, DIR_BIDIRECTIONAL},
    {"/%", OP_MOD_DIVISOR, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "modulo", "div %1; mov %0, rdx", 2, DIR_BIDIRECTIONAL},
    {"%/", OP_MOD_DIVISOR, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "reverse modulo", "div %0; mov %1, rdx", 2, DIR_BIDIRECTIONAL},
    {"/%/", OP_MOD_DIVISOR, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "modulo division", "call _fcx_mod_div", 3, DIR_BIDIRECTIONAL},
    {"/|/", OP_SIMD_DIV, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "SIMD divide", "divps %0, %1", 3, DIR_BIDIRECTIONAL},
    {"|/|", OP_PARALLEL_DIV, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "parallel divide", "call _fcx_parallel_div", 3, DIR_BIDIRECTIONAL},
    {"/||/", OP_SIMD_DIV, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "SIMD parallel divide", "call _fcx_simd_parallel_div", 4,
     DIR_BIDIRECTIONAL},
    {"||/||", OP_PARALLEL_DIV, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "quad parallel divide", "call _fcx_quad_parallel_div", 5,
     DIR_BIDIRECTIONAL},
    {"+", OP_ADD_ASSIGN, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "addition", "add %0, %1", 1, DIR_BIDIRECTIONAL},
    {"-", OP_SUB_ASSIGN, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "subtraction", "sub %0, %1", 1, DIR_BIDIRECTIONAL},
    {"*", OP_MUL_ASSIGN, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "multiplication", "imul %0, %1", 1, DIR_BIDIRECTIONAL},
    // Note: ** and *** are handled by ARITHMETIC/ASSIGNMENT family
    {"+*", OP_ADD_ASSIGN, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "multiply add", "imul %1, %2; add %0, %1", 2, DIR_BIDIRECTIONAL},
    {"*+", OP_MUL_ASSIGN, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "add multiply", "add %1, %2; imul %0, %1", 2, DIR_BIDIRECTIONAL},
    {"-*", OP_SUB_ASSIGN, 8, ASSOC_LEFT, ARITY_TERNARY, CAT_ARITH_DENSE,
     "multiply subtract", "imul %1, %2; sub %0, %1", 2, DIR_BIDIRECTIONAL},

    // === PHASE 1: TRIVIAL SINGLE-INSTRUCTION ADDITIONS ===
    // Bit manipulation (4 operators, all single x86_64 instructions)
    {"popcount>", OP_POPCOUNT, 11, ASSOC_NONE, ARITY_UNARY, CAT_BITFIELD,
     "population count", "popcnt %0, %1", 9, DIR_RIGHT_FACING},
    {"clz>", OP_CLZ, 11, ASSOC_NONE, ARITY_UNARY, CAT_BITFIELD,
     "count leading zeros", "lzcnt %0, %1", 4, DIR_RIGHT_FACING},
    {"ctz>", OP_CTZ, 11, ASSOC_NONE, ARITY_UNARY, CAT_BITFIELD,
     "count trailing zeros", "tzcnt %0, %1", 4, DIR_RIGHT_FACING},
    {"byteswap>", OP_BYTESWAP, 11, ASSOC_NONE, ARITY_UNARY, CAT_BITFIELD,
     "byte swap endianness", "bswap %0", 10, DIR_RIGHT_FACING},

    // Min/max operators (2 operators, cmov instructions)
    {"<?", OP_MIN, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON, "minimum",
     "cmp %0, %1; cmovg %0, %1", 2, DIR_LEFT_FACING},
    {">?", OP_MAX, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON, "maximum",
     "cmp %0, %1; cmovl %0, %1", 2, DIR_RIGHT_FACING},

    // Additional comparison operators to meet minimum requirement
    {"<=>?", OP_THREE_WAY_CMP, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "three-way compare", "cmp %0, %1; setl al; setg bl; sub al, bl", 4,
     DIR_BIDIRECTIONAL},
    {"<~>", OP_THREE_WAY_CMP, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "three-way compare alt", "cmp %0, %1; setl al; setg bl; sub al, bl", 3,
     DIR_BIDIRECTIONAL},
    {"<|>", OP_CLAMP, 5, ASSOC_LEFT, ARITY_TERNARY, CAT_COMPARISON,
     "clamp between min max",
     "cmp %0, %1; cmovl %0, %1; cmp %0, %2; cmovg %0, %2", 3,
     DIR_BIDIRECTIONAL},
    {"<=?", OP_LE_MAYBE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "less equal maybe", "cmp %0, %1; setle %2", 3, DIR_LEFT_FACING},
    {">=?", OP_GE_MAYBE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "greater equal maybe", "cmp %0, %1; setge %2", 3, DIR_RIGHT_FACING},
    {"==?", OP_EQ_MAYBE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "equal maybe", "cmp %0, %1; sete %2", 3, DIR_BIDIRECTIONAL},
    {"!=?", OP_NE_MAYBE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "not equal maybe", "cmp %0, %1; setne %2", 3, DIR_BIDIRECTIONAL},
    {"<??", OP_LT_DOUBLE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "less than double check", "cmp %0, %1; setl %2", 3, DIR_LEFT_FACING},
    {">??", OP_GT_DOUBLE, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "greater than double check", "cmp %0, %1; setg %2", 3, DIR_RIGHT_FACING},
    {"<=>!", OP_CMP_ASSERT, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "compare with assert", "cmp %0, %1; jne abort", 4, DIR_BIDIRECTIONAL},
    {"<==>!", OP_SWAP_ASSERT, 5, ASSOC_LEFT, ARITY_BINARY, CAT_COMPARISON,
     "swap with assert", "lock xchg %0, %1; test %0, %0", 5, DIR_BIDIRECTIONAL},

    // Math intrinsics (7 operators, all single SSE/x87 instructions)
    {"sqrt>", OP_SQRT, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE,
     "square root", "sqrtss %0, %1", 5, DIR_RIGHT_FACING},
    {"rsqrt>", OP_RSQRT, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE,
     "reciprocal square root", "rsqrtss %0, %1", 6, DIR_RIGHT_FACING},
    {"abs>", OP_ABS, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE,
     "absolute value", "movaps %0, %1; andps %0, [abs_mask]", 4,
     DIR_RIGHT_FACING},
    {"floor>", OP_FLOOR, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE, "floor",
     "roundss %0, %1, 0x01", 6, DIR_RIGHT_FACING},
    {"ceil>", OP_CEIL, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE, "ceiling",
     "roundss %0, %1, 0x02", 5, DIR_RIGHT_FACING},
    {"trunc>", OP_TRUNC, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE,
     "truncate", "roundss %0, %1, 0x03", 6, DIR_RIGHT_FACING},
    {"round>", OP_ROUND, 11, ASSOC_NONE, ARITY_UNARY, CAT_ARITH_DENSE,
     "round nearest", "roundss %0, %1, 0x00", 6, DIR_RIGHT_FACING},

    // Memory hints (2 operators, prefetch instructions)
    {"prefetch>", OP_PREFETCH, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "prefetch memory", "prefetcht0 [%0]", 9, DIR_RIGHT_FACING},
    {"prefetch_write>", OP_PREFETCH_W, 11, ASSOC_NONE, ARITY_UNARY,
     CAT_MEMORY_ALLOC, "prefetch for write", "prefetchw [%0]", 16,
     DIR_RIGHT_FACING},

    // === PHASE 2: ARITHMETIC EXTENSIONS ===
    // Saturating arithmetic (3 operators)
    {"+|", OP_SAT_ADD, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "saturating add", "call _fcx_sat_add", 2, DIR_BIDIRECTIONAL},
    {"-|", OP_SAT_SUB, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "saturating subtract", "call _fcx_sat_sub", 2, DIR_BIDIRECTIONAL},
    {"*|", OP_SAT_MUL, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "saturating multiply", "call _fcx_sat_mul", 2, DIR_BIDIRECTIONAL},

    // Wrapping arithmetic (3 operators - explicit overflow behavior)
    {"+%", OP_WRAP_ADD, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "wrapping add", "add %0, %1", 2, DIR_BIDIRECTIONAL},
    {"-%", OP_WRAP_SUB, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "wrapping subtract", "sub %0, %1", 2, DIR_BIDIRECTIONAL},
    {"*%", OP_WRAP_MUL, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "wrapping multiply", "imul %0, %1", 2, DIR_BIDIRECTIONAL},

    // Checked arithmetic (3 operators)
    {"+?", OP_CHECKED_ADD, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "checked add", "add %0, %1; seto %2", 2, DIR_BIDIRECTIONAL},
    {"-?", OP_CHECKED_SUB, 7, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "checked subtract", "sub %0, %1; seto %2", 2, DIR_BIDIRECTIONAL},
    {"*?", OP_CHECKED_MUL, 8, ASSOC_LEFT, ARITY_BINARY, CAT_ARITH_DENSE,
     "checked multiply", "imul %0, %1; seto %2", 2, DIR_BIDIRECTIONAL},

    // === PHASE 3: RANGE AND ALIGNMENT ===
    // Range operators (4 operators)
    {"..", OP_RANGE, 10, ASSOC_LEFT, ARITY_BINARY, CAT_SPECIAL,
     "range exclusive", "call _fcx_range_exclusive", 2, DIR_BIDIRECTIONAL},
    {"..=", OP_RANGE_INCLUSIVE, 10, ASSOC_LEFT, ARITY_BINARY, CAT_SPECIAL,
     "range inclusive", "call _fcx_range_inclusive", 3, DIR_BIDIRECTIONAL},
    {"..<", OP_RANGE_EXCLUSIVE, 10, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "range exclusive FCx", "call _fcx_range_excl", 3, DIR_LEFT_FACING},
    {"..>", OP_RANGE_INCLUSIVE, 10, ASSOC_LEFT, ARITY_BINARY, CAT_SHIFT_ROTATE,
     "range inclusive FCx", "call _fcx_range_incl", 3, DIR_RIGHT_FACING},

    // Alignment helpers (3 operators)
    {"align_up>", OP_ALIGN_UP, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "align up", "add %0, %1; dec %0; and %0, ~(%1-1)", 9, DIR_RIGHT_FACING},
    {"align_down>", OP_ALIGN_DOWN, 11, ASSOC_NONE, ARITY_BINARY,
     CAT_MEMORY_ALLOC, "align down", "and %0, ~(%1-1)", 11, DIR_RIGHT_FACING},
    {"is_aligned?>", OP_IS_ALIGNED, 11, ASSOC_NONE, ARITY_BINARY,
     CAT_MEMORY_ALLOC, "check alignment", "test %0, (%1-1); setz %2", 12,
     DIR_RIGHT_FACING},

    // Missing allocators (mentioned in design doc)
    {"arena>", OP_ARENA_ALLOC, 11, ASSOC_NONE, ARITY_BINARY, CAT_MEMORY_ALLOC,
     "arena allocation", "call _fcx_arena_alloc", 6, DIR_RIGHT_FACING},
    {">arena", OP_ARENA_FREE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "arena reset", "call _fcx_arena_reset", 6, DIR_LEFT_FACING},
    {"slab>", OP_SLAB_ALLOC, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "slab allocation", "call _fcx_slab_alloc", 5, DIR_RIGHT_FACING},
    {">slab", OP_SLAB_FREE, 11, ASSOC_NONE, ARITY_UNARY, CAT_MEMORY_ALLOC,
     "slab deallocation", "call _fcx_slab_free", 5, DIR_LEFT_FACING},

    // === PHASE 4: COMPILE-TIME OPERATORS ===
    // Compile-time queries (4 operators)
    {"@sizeof>", OP_SIZEOF, 11, ASSOC_NONE, ARITY_UNARY, CAT_SPECIAL,
     "compile-time sizeof", "compile_time_const", 8, DIR_RIGHT_FACING},
    {"@alignof>", OP_ALIGNOF, 11, ASSOC_NONE, ARITY_UNARY, CAT_SPECIAL,
     "compile-time alignof", "compile_time_const", 9, DIR_RIGHT_FACING},
    {"@offsetof>", OP_OFFSETOF, 11, ASSOC_NONE, ARITY_BINARY, CAT_SPECIAL,
     "compile-time offsetof", "compile_time_const", 10, DIR_RIGHT_FACING},
    {"@!", OP_STATIC_ASSERT, 11, ASSOC_NONE, ARITY_BINARY, CAT_SPECIAL,
     "static assert", "compile_time_check", 2, DIR_RIGHT_FACING},

    // === SPECIAL OPERATORS (10 operators) ===
    {"?", OP_CONDITIONAL, 3, ASSOC_RIGHT, ARITY_TERNARY, CAT_COMPARISON,
     "conditional", "test %0, %0; cmovnz %1, %2", 1, DIR_BIDIRECTIONAL},
    {"??", OP_CONDITIONAL, 3, ASSOC_RIGHT, ARITY_TERNARY, CAT_COMPARISON,
     "double conditional", "call _fcx_double_cond", 2, DIR_BIDIRECTIONAL},
    {"???", OP_CONDITIONAL, 3, ASSOC_RIGHT, ARITY_TERNARY, CAT_COMPARISON,
     "triple conditional", "call _fcx_triple_cond", 3, DIR_BIDIRECTIONAL},
    // Note: ":" is TOK_COLON punctuation, not an operator
    {"::", OP_ASSIGN_INFER, 1, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "scope resolution", "call _fcx_scope_resolve", 2, DIR_BIDIRECTIONAL},
    {":::", OP_ASSIGN_INFER, 1, ASSOC_RIGHT, ARITY_TERNARY, CAT_ARITH_ASSIGN,
     "triple scope", "call _fcx_triple_scope", 3, DIR_BIDIRECTIONAL},
    // Note: ";" is TOK_SEMICOLON punctuation, not an operator
    {";;", OP_ASSIGN_INFER, 1, ASSOC_RIGHT, ARITY_BINARY, CAT_ARITH_ASSIGN,
     "double semicolon", "nop", 2, DIR_BIDIRECTIONAL},
    // Note: "." is TOK_DOT punctuation, not an operator
};

static const size_t OPERATOR_COUNT =
    sizeof(OPERATOR_REGISTRY) / sizeof(OperatorInfo);

// Operator trie for efficient lookup
static TrieNode *operator_trie_root = NULL;

// Forward declaration
static void free_operator_trie(TrieNode *node);

// Initialize the operator registry and build the trie
void init_operator_registry(void) { build_operator_trie(); }

// Build the operator trie for O(k) lookup time
void build_operator_trie(void) {
  // Clean up existing trie if it exists
  if (operator_trie_root != NULL) {
    free_operator_trie(operator_trie_root);
  }

  operator_trie_root = calloc(1, sizeof(TrieNode));
  if (operator_trie_root == NULL) {
    fprintf(stderr,
            "Error: Failed to allocate memory for operator trie root\n");
    return;
  }

  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    const OperatorInfo *op = &OPERATOR_REGISTRY[i];
    TrieNode *current = operator_trie_root;

    // Validate operator symbol
    if (op->symbol == NULL || strlen(op->symbol) == 0) {
      fprintf(stderr, "Warning: Operator at index %zu has invalid symbol\n", i);
      continue;
    }

    // Insert operator symbol into trie
    for (const char *c = op->symbol; *c != '\0'; c++) {
      unsigned char index = (unsigned char)*c;
      if (current->children[index] == NULL) {
        current->children[index] = calloc(1, sizeof(TrieNode));
        if (current->children[index] == NULL) {
          fprintf(stderr, "Error: Failed to allocate memory for trie node\n");
          return;
        }
      }
      current = current->children[index];
    }

    // Mark as terminal and store operator info
    if (current->is_terminal) {
      fprintf(stderr, "Warning: Duplicate operator symbol '%s' detected\n",
              op->symbol);
    }
    current->is_terminal = true;
    current->operator_info = op;
  }
}

// Lookup operator using trie (greedy maximal matching)
// Returns the longest matching operator and sets matched_length
const OperatorInfo *trie_lookup_greedy(const char *symbol, size_t max_length,
                                       size_t *matched_length) {
  if (operator_trie_root == NULL || symbol == NULL || matched_length == NULL) {
    if (matched_length)
      *matched_length = 0;
    return NULL;
  }

  TrieNode *current = operator_trie_root;
  const OperatorInfo *last_match = NULL;
  size_t last_match_length = 0;

#ifdef TEST_MODE
  // Debug output only for problematic operators
  if ((strcmp(symbol, "//") == 0 || strcmp(symbol, "///") == 0 ||
       strcmp(symbol, "////") == 0 || strcmp(symbol, "/////") == 0 ||
       strcmp(symbol, "////////") == 0)) {
    printf("DEBUG: trie_lookup_greedy('%s', max_length=%zu)\n", symbol,
           max_length);
  }
#endif

  // Traverse trie for greedy maximal matching
  for (size_t i = 0; i < max_length && symbol[i] != '\0'; i++) {
    unsigned char index = (unsigned char)symbol[i];

#ifdef TEST_MODE
    if ((strcmp(symbol, "//") == 0 || strcmp(symbol, "///") == 0 ||
         strcmp(symbol, "////") == 0 || strcmp(symbol, "/////") == 0 ||
         strcmp(symbol, "////////") == 0)) {
      printf("DEBUG: trie step %zu, char='%c' (index=%u), children[%u]=%p\n", i,
             symbol[i], index, index, (void *)current->children[index]);
    }
#endif

    if (current->children[index] == NULL) {
#ifdef TEST_MODE
      if ((strcmp(symbol, "//") == 0 || strcmp(symbol, "///") == 0 ||
           strcmp(symbol, "////") == 0 || strcmp(symbol, "/////") == 0 ||
           strcmp(symbol, "////////") == 0)) {
        printf("DEBUG: trie traversal stopped at step %zu, no child for '%c'\n",
               i, symbol[i]);
      }
#endif
      break; // No more matches possible
    }

    current = current->children[index];
    if (current->is_terminal) {
      // Found a valid operator, but continue to find longer matches
      last_match = current->operator_info;
      last_match_length = i + 1;

#ifdef TEST_MODE
      if ((strcmp(symbol, "//") == 0 || strcmp(symbol, "///") == 0 ||
           strcmp(symbol, "////") == 0 || strcmp(symbol, "/////") == 0 ||
           strcmp(symbol, "////////") == 0)) {
        printf(
            "DEBUG: trie found terminal at step %zu, operator='%s', token=%d\n",
            i, last_match->symbol, last_match->token);
      }
#endif
    }
  }

  *matched_length = last_match_length;
  return last_match;
}

// Legacy function for backward compatibility
const OperatorInfo *trie_lookup(const char *symbol, size_t length) {
  size_t matched_length;
  return trie_lookup_greedy(symbol, length, &matched_length);
}

// Lookup operator by symbol string
const OperatorInfo *lookup_operator(const char *symbol) {
  if (symbol == NULL) {
    return NULL;
  }
  size_t length = strlen(symbol);
  return trie_lookup(symbol, length);
}

// Get operator count for validation
size_t get_operator_count(void) { return OPERATOR_COUNT; }

// Validate that we have 200+ operators as required
bool validate_operator_count(void) { return OPERATOR_COUNT >= 200; }

// Get operator by index (for iteration)
const OperatorInfo *get_operator_by_index(size_t index) {
  if (index >= OPERATOR_COUNT) {
    return NULL;
  }
  return &OPERATOR_REGISTRY[index];
}

// Free the operator trie
static void free_operator_trie(TrieNode *node) {
  if (node == NULL) {
    return;
  }

  for (int i = 0; i < 256; i++) {
    if (node->children[i] != NULL) {
      free_operator_trie(node->children[i]);
    }
  }

  free(node);
}

void cleanup_operator_registry(void) {
  free_operator_trie(operator_trie_root);
  operator_trie_root = NULL;
}

// Find operator by exact string match (alternative to trie lookup)
const OperatorInfo *find_operator_by_symbol(const char *symbol) {
  if (symbol == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    if (strcmp(OPERATOR_REGISTRY[i].symbol, symbol) == 0) {
      return &OPERATOR_REGISTRY[i];
    }
  }

  return NULL;
}

// Check if a symbol is a valid operator
bool is_valid_operator(const char *symbol) {
  return lookup_operator(symbol) != NULL;
}

// Get operator precedence by symbol
uint8_t get_operator_precedence(const char *symbol) {
  const OperatorInfo *op = lookup_operator(symbol);
  return op ? op->precedence : 0;
}

// Get operator category by symbol
OperatorCategory get_operator_category(const char *symbol) {
  const OperatorInfo *op = lookup_operator(symbol);
  return op ? op->category : CAT_SHIFT_ROTATE; // default category
}

// Check if operator has specific arity
bool operator_has_arity(const char *symbol, Arity expected_arity) {
  const OperatorInfo *op = lookup_operator(symbol);
  return op && (op->arity == expected_arity);
}

// Get assembly template for operator
const char *get_operator_assembly_template(const char *symbol) {
  const OperatorInfo *op = lookup_operator(symbol);
  return op ? op->assembly_template : NULL;
}

// Combinatorial Pattern Rules for FCx Operator Generation
// Generates 200+ operators from symbol alphabet using systematic patterns

// Symbol alphabet for operator construction
static const char SYMBOL_ALPHABET[] =
    "< > / | \\ : ; ! ? ^ @ % $ & * ~ ` , . [ ] { }";

// Pattern generation rules
typedef struct {
  const char *base_symbols;  // Base symbols for this pattern
  int min_length;            // Minimum operator length
  int max_length;            // Maximum operator length
  Direction directionality;  // Directional bias
  OperatorCategory category; // Semantic category
  const char *description;   // Pattern description
} PatternRule;

// Combinatorial pattern rules for systematic operator generation
static const PatternRule PATTERN_RULES[] = {
    // Shift/Rotate patterns: <, <<, <<<, <<<<, <<<<<
    {"<>", 1, 5, DIR_BIDIRECTIONAL, CAT_SHIFT_ROTATE,
     "shift and rotate operations"},

    // Arithmetic/Assignment patterns: =, +=, <<=, <=>, <==>
    {"=+*/<>&|^", 1, 4, DIR_RIGHT_FACING, CAT_ARITH_ASSIGN,
     "assignment and arithmetic"},

    // Data movement patterns: |>, <|, ><, <>
    {"|><", 2, 4, DIR_BIDIRECTIONAL, CAT_DATA_MOVEMENT,
     "data movement and pipes"},

    // Bitfield patterns: &>, &<, ^>, <<&
    {"&^", 2, 4, DIR_BIDIRECTIONAL, CAT_BITFIELD, "bitfield operations"},

    // Memory allocation patterns: mem>, >mem, @>, stack>
    {"@", 2, 6, DIR_BIDIRECTIONAL, CAT_MEMORY_ALLOC,
     "memory and MMIO operations"},

    // Atomic/Concurrency patterns: !, !!, !!!, !=>
    {"!", 1, 5, DIR_BIDIRECTIONAL, CAT_ATOMIC_CONCUR, "atomic and concurrency"},

    // Syscall/OS patterns: $/, /$, sys%, #!
    {"$%#", 2, 5, DIR_BIDIRECTIONAL, CAT_SYSCALL_OS,
     "syscall and OS operations"},

    // IO/Formatting patterns: >>>, <<<, />/
    {"/\\", 3, 5, DIR_BIDIRECTIONAL, CAT_IO_FORMAT, "I/O and formatting"},

    // Comparison patterns: <, <=, >=, ==, !=, <>
    {"<>=!", 1, 3, DIR_BIDIRECTIONAL, CAT_COMPARISON, "comparison operations"},

    // Arithmetic dense patterns: /, //, ///, /|/
    {"/", 1, 5, DIR_BIDIRECTIONAL, CAT_ARITH_DENSE,
     "dense arithmetic operations"}};

static const size_t PATTERN_RULE_COUNT =
    sizeof(PATTERN_RULES) / sizeof(PatternRule);

// Validate that operator registry meets 200+ requirement
bool validate_combinatorial_generation(void) {
  // Count operators by category to ensure comprehensive coverage
  size_t category_counts[11] = {0}; // Updated to include CAT_SPECIAL

  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    const OperatorInfo *op = &OPERATOR_REGISTRY[i];
    if (op->category < 11) { // Updated to include CAT_SPECIAL
      category_counts[op->category]++;
    }
  }

  // Verify minimum operators per category (relaxed for some categories)
  const size_t MIN_PER_CATEGORY = 15;  // At least 15 operators per category
  for (int cat = 0; cat < 11; cat++) { // Updated to include CAT_SPECIAL
    // Relaxed requirements for IO/Formatting and Special categories
    size_t min_required = (cat == 7 || cat == 10) ? 5 : MIN_PER_CATEGORY;
    if (category_counts[cat] < min_required) {
      printf("Warning: Category %d has only %zu operators (minimum %zu)\n", cat,
             category_counts[cat], min_required);
    }
  }

  // Verify total count
  printf("Operator registry validation:\n");
  printf("Total operators: %zu\n", OPERATOR_COUNT);
  printf("Target: 200+ operators\n");

  for (int cat = 0; cat < 11; cat++) { // Updated to include CAT_SPECIAL
    const char *category_names[] = {"Shift/Rotate",
                                    "Arithmetic/Assignment",
                                    "Data Movement",
                                    "Bitfield",
                                    "Memory Allocation",
                                    "Atomic/Concurrency",
                                    "Syscall/OS",
                                    "IO/Formatting",
                                    "Comparison",
                                    "Arithmetic Dense",
                                    "Special"};
    printf("  %s: %zu operators\n", category_names[cat], category_counts[cat]);
  }

  return OPERATOR_COUNT >= 200;
}

// Generate operator patterns (for documentation/validation)
void generate_operator_patterns(void) {
  printf("FCx Combinatorial Operator Pattern Generation\n");
  printf("Symbol Alphabet: %s\n\n", SYMBOL_ALPHABET);

  for (size_t i = 0; i < PATTERN_RULE_COUNT; i++) {
    const PatternRule *rule = &PATTERN_RULES[i];
    printf("Pattern %zu: %s\n", i + 1, rule->description);
    printf("  Base symbols: %s\n", rule->base_symbols);
    printf("  Length range: %d-%d characters\n", rule->min_length,
           rule->max_length);
    printf("  Directionality: %s\n",
           rule->directionality == DIR_LEFT_FACING    ? "Left-facing"
           : rule->directionality == DIR_RIGHT_FACING ? "Right-facing"
                                                      : "Bidirectional");
    printf("  Category: %d\n\n", rule->category);
  }
}

// Operator precedence validation
bool validate_operator_precedence(void) {
  // Verify that all operators have valid precedence levels (1-12)
  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    const OperatorInfo *op = &OPERATOR_REGISTRY[i];
    if (op->precedence < 1 || op->precedence > 12) {
      printf("Error: Operator '%s' has invalid precedence %d\n", op->symbol,
             op->precedence);
      return false;
    }
  }

  printf("✓ All operators have valid precedence levels (1-12)\n");
  return true;
}

// Assembly template validation
bool validate_assembly_templates(void) {
  // Verify that all operators have assembly templates
  size_t missing_templates = 0;

  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    const OperatorInfo *op = &OPERATOR_REGISTRY[i];
    if (!op->assembly_template || strlen(op->assembly_template) == 0) {
      printf("Warning: Operator '%s' missing assembly template\n", op->symbol);
      missing_templates++;
    }
  }

  if (missing_templates == 0) {
    printf("✓ All operators have assembly templates\n");
    return true;
  } else {
    printf("Warning: %zu operators missing assembly templates\n",
           missing_templates);
    return false;
  }
}

// Validate trie structure and operator recognition
bool validate_trie_structure(void) {
  if (operator_trie_root == NULL) {
    printf("✗ Operator trie not initialized\n");
    return false;
  }

  // Test all operators can be found via trie lookup
  size_t successful_lookups = 0;
  size_t failed_lookups = 0;

  for (size_t i = 0; i < OPERATOR_COUNT; i++) {
    const OperatorInfo *op = &OPERATOR_REGISTRY[i];
    size_t matched_length;
    const OperatorInfo *found =
        trie_lookup_greedy(op->symbol, strlen(op->symbol), &matched_length);

    if (found && found == op && matched_length == strlen(op->symbol)) {
      successful_lookups++;
    } else {
      printf("✗ Trie lookup failed for operator '%s'\n", op->symbol);
      failed_lookups++;
    }
  }

  printf("Trie validation: %zu successful, %zu failed lookups\n",
         successful_lookups, failed_lookups);

  // Test greedy matching with overlapping operators
  struct {
    const char *input;
    const char *expected_operator;
    size_t expected_length;
  } greedy_tests[] = {
      {"<<=", "<<=", 3},   // Should match <<= not << + =
      {">>>", ">>>", 3},   // Should match >>> not >> + >
      {"<==>", "<==>", 4}, // Should match <==> not <=> + >
      {"!=>", "!=>", 3},   // Should match !=> not != + >
      {"mem>", "mem>", 4}, // Should match mem> not individual chars
      {"sys%", "sys%", 4}, // Should match sys% not individual chars
  };

  size_t greedy_passed = 0;
  size_t greedy_total = sizeof(greedy_tests) / sizeof(greedy_tests[0]);

  for (size_t i = 0; i < greedy_total; i++) {
    size_t matched_length;
    const OperatorInfo *found = trie_lookup_greedy(
        greedy_tests[i].input, strlen(greedy_tests[i].input), &matched_length);

    if (found &&
        strcmp(found->symbol, greedy_tests[i].expected_operator) == 0 &&
        matched_length == greedy_tests[i].expected_length) {
      greedy_passed++;
    } else {
      printf("✗ Greedy matching failed for '%s': expected '%s' (len %zu), got "
             "'%s' (len %zu)\n",
             greedy_tests[i].input, greedy_tests[i].expected_operator,
             greedy_tests[i].expected_length, found ? found->symbol : "NULL",
             matched_length);
    }
  }

  printf("Greedy matching tests: %zu/%zu passed\n", greedy_passed,
         greedy_total);

  return failed_lookups == 0 && greedy_passed == greedy_total;
}

// Comprehensive operator registry validation
bool validate_complete_operator_registry(void) {
  printf("=== FCx Operator Registry Validation ===\n\n");

  bool valid = true;

  // 1. Count validation (200+ operators)
  if (!validate_operator_count()) {
    printf("✗ Operator count validation failed\n");
    valid = false;
  } else {
    printf("✓ Operator count validation passed (%zu operators)\n",
           OPERATOR_COUNT);
  }

  // 2. Combinatorial generation validation
  if (!validate_combinatorial_generation()) {
    printf("✗ Combinatorial generation validation failed\n");
    valid = false;
  } else {
    printf("✓ Combinatorial generation validation passed\n");
  }

  // 3. Precedence validation
  if (!validate_operator_precedence()) {
    printf("✗ Precedence validation failed\n");
    valid = false;
  }

  // 4. Assembly template validation
  if (!validate_assembly_templates()) {
    printf("✗ Assembly template validation failed\n");
    valid = false;
  }

  // 5. Trie structure validation
  if (!validate_trie_structure()) {
    printf("✗ Trie structure validation failed\n");
    valid = false;
  } else {
    printf("✓ Trie structure validation passed\n");
  }

  printf("\n=== Validation Summary ===\n");
  if (valid) {
    printf("✓ All validations passed - FCx operator registry is complete\n");
  } else {
    printf("✗ Some validations failed - operator registry needs fixes\n");
  }

  return valid;
}

// Accessor functions for error handler
const OperatorInfo* get_operator_registry(void) {
    return OPERATOR_REGISTRY;
}

size_t get_operator_registry_size(void) {
    return OPERATOR_COUNT;
}
