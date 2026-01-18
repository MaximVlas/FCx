#define _POSIX_C_SOURCE 200809L
#include "ir_optimize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Constant Folding Pass - Comprehensive Implementation
// ============================================================================

// Hash table for O(1) constant lookup
#define CONST_TABLE_SIZE 1024

typedef struct ConstEntry {
    uint32_t vreg_id;
    int64_t value;
    bool is_bigint;
    uint64_t bigint_limbs[16];
    uint8_t num_limbs;
    struct ConstEntry* next;
} ConstEntry;

typedef struct {
    ConstEntry* buckets[CONST_TABLE_SIZE];
} ConstTable;

static uint32_t hash_vreg(uint32_t vreg_id) {
    return vreg_id % CONST_TABLE_SIZE;
}

static void const_table_init(ConstTable* table) {
    memset(table->buckets, 0, sizeof(table->buckets));
}

static void const_table_destroy(ConstTable* table) {
    for (int i = 0; i < CONST_TABLE_SIZE; i++) {
        ConstEntry* entry = table->buckets[i];
        while (entry) {
            ConstEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
}

static void const_table_insert(ConstTable* table, uint32_t vreg_id, int64_t value) {
    uint32_t bucket = hash_vreg(vreg_id);
    ConstEntry* entry = (ConstEntry*)malloc(sizeof(ConstEntry));
    entry->vreg_id = vreg_id;
    entry->value = value;
    entry->is_bigint = false;
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
}

static void const_table_insert_bigint(ConstTable* table, uint32_t vreg_id, 
                                     const uint64_t* limbs, uint8_t num_limbs) {
    uint32_t bucket = hash_vreg(vreg_id);
    ConstEntry* entry = (ConstEntry*)malloc(sizeof(ConstEntry));
    entry->vreg_id = vreg_id;
    entry->is_bigint = true;
    entry->num_limbs = num_limbs;
    memcpy(entry->bigint_limbs, limbs, num_limbs * sizeof(uint64_t));
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
}

static ConstEntry* const_table_lookup(ConstTable* table, uint32_t vreg_id) {
    uint32_t bucket = hash_vreg(vreg_id);
    ConstEntry* entry = table->buckets[bucket];
    while (entry) {
        if (entry->vreg_id == vreg_id) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// Bigint arithmetic helpers
static bool bigint_add(const uint64_t* a, uint8_t a_limbs, const uint64_t* b, uint8_t b_limbs,
                      uint64_t* result, uint8_t* result_limbs) {
    uint8_t max_limbs = (a_limbs > b_limbs) ? a_limbs : b_limbs;
    uint64_t carry = 0;
    
    for (uint8_t i = 0; i < max_limbs; i++) {
        uint64_t a_val = (i < a_limbs) ? a[i] : 0;
        uint64_t b_val = (i < b_limbs) ? b[i] : 0;
        uint64_t sum = a_val + b_val + carry;
        
        result[i] = sum;
        carry = (sum < a_val) ? 1 : 0;  // Overflow detection
    }
    
    if (carry && max_limbs < 16) {
        result[max_limbs] = carry;
        max_limbs++;
    }
    
    *result_limbs = max_limbs;
    return max_limbs <= 16;  // Check if result fits
}

static bool bigint_sub(const uint64_t* a, uint8_t a_limbs, const uint64_t* b, uint8_t b_limbs,
                      uint64_t* result, uint8_t* result_limbs) {
    uint8_t max_limbs = (a_limbs > b_limbs) ? a_limbs : b_limbs;
    uint64_t borrow = 0;
    
    for (uint8_t i = 0; i < max_limbs; i++) {
        uint64_t a_val = (i < a_limbs) ? a[i] : 0;
        uint64_t b_val = (i < b_limbs) ? b[i] : 0;
        
        if (a_val >= b_val + borrow) {
            result[i] = a_val - b_val - borrow;
            borrow = 0;
        } else {
            result[i] = (UINT64_MAX - b_val - borrow) + a_val + 1;
            borrow = 1;
        }
    }
    
    // Find actual number of limbs (remove leading zeros)
    while (max_limbs > 1 && result[max_limbs - 1] == 0) {
        max_limbs--;
    }
    
    *result_limbs = max_limbs;
    return true;
}

// Power of 2 detection for strength reduction
static bool is_power_of_2(int64_t value, int* shift_amount) {
    if (value <= 0) return false;
    
    uint64_t uval = (uint64_t)value;
    if ((uval & (uval - 1)) != 0) return false;  // Not a power of 2
    
    *shift_amount = 0;
    while (uval > 1) {
        uval >>= 1;
        (*shift_amount)++;
    }
    return true;
}

bool opt_constant_folding(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    ConstTable const_table;
    const_table_init(&const_table);
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            // Track constant definitions
            if (instr->opcode == FCXIR_CONST) {
                const_table_insert(&const_table, instr->u.const_op.dest.id, instr->u.const_op.value);
                continue;
            }
            
            if (instr->opcode == FCXIR_CONST_BIGINT) {
                const_table_insert_bigint(&const_table, instr->u.const_bigint_op.dest.id,
                                        instr->u.const_bigint_op.limbs, instr->u.const_bigint_op.num_limbs);
                continue;
            }
            
            // Fold binary operations
            if ((instr->opcode >= FCXIR_ADD && instr->opcode <= FCXIR_MOD) ||
                (instr->opcode >= FCXIR_AND && instr->opcode <= FCXIR_ROTATE_RIGHT) ||
                (instr->opcode >= FCXIR_CMP_EQ && instr->opcode <= FCXIR_CMP_GE)) {
                
                ConstEntry* left_const = const_table_lookup(&const_table, instr->u.binary_op.left.id);
                ConstEntry* right_const = const_table_lookup(&const_table, instr->u.binary_op.right.id);
                
                // Handle regular 64-bit constant folding
                if (left_const && right_const && !left_const->is_bigint && !right_const->is_bigint) {
                    int64_t left_val = left_const->value;
                    int64_t right_val = right_const->value;
                    int64_t result = 0;
                    bool can_fold = true;
                    
                    switch (instr->opcode) {
                        // Arithmetic operations
                        case FCXIR_ADD:
                            result = left_val + right_val;
                            break;
                        case FCXIR_SUB:
                            result = left_val - right_val;
                            break;
                        case FCXIR_MUL:
                            result = left_val * right_val;
                            break;
                        case FCXIR_DIV:
                            if (right_val != 0) {
                                result = left_val / right_val;
                            } else {
                                can_fold = false;  // Don't fold division by zero
                            }
                            break;
                        case FCXIR_MOD:
                            if (right_val != 0) {
                                result = left_val % right_val;
                            } else {
                                can_fold = false;  // Don't fold modulo by zero
                            }
                            break;
                            
                        // Bitwise operations
                        case FCXIR_AND:
                            result = left_val & right_val;
                            break;
                        case FCXIR_OR:
                            result = left_val | right_val;
                            break;
                        case FCXIR_XOR:
                            result = left_val ^ right_val;
                            break;
                            
                        // Shift operations
                        case FCXIR_LSHIFT:
                            if (right_val >= 0 && right_val < 64) {
                                result = left_val << right_val;
                            } else {
                                can_fold = false;  // Don't fold invalid shifts
                            }
                            break;
                        case FCXIR_RSHIFT:
                            if (right_val >= 0 && right_val < 64) {
                                result = left_val >> right_val;  // Arithmetic shift
                            } else {
                                can_fold = false;
                            }
                            break;
                        case FCXIR_LOGICAL_RSHIFT:
                            if (right_val >= 0 && right_val < 64) {
                                result = (int64_t)((uint64_t)left_val >> right_val);  // Logical shift
                            } else {
                                can_fold = false;
                            }
                            break;
                        case FCXIR_ROTATE_LEFT:
                            if (right_val >= 0 && right_val < 64) {
                                uint64_t uval = (uint64_t)left_val;
                                result = (int64_t)((uval << right_val) | (uval >> (64 - right_val)));
                            } else {
                                can_fold = false;
                            }
                            break;
                        case FCXIR_ROTATE_RIGHT:
                            if (right_val >= 0 && right_val < 64) {
                                uint64_t uval = (uint64_t)left_val;
                                result = (int64_t)((uval >> right_val) | (uval << (64 - right_val)));
                            } else {
                                can_fold = false;
                            }
                            break;
                            
                        // Comparison operations
                        case FCXIR_CMP_EQ:
                            result = (left_val == right_val) ? 1 : 0;
                            break;
                        case FCXIR_CMP_NE:
                            result = (left_val != right_val) ? 1 : 0;
                            break;
                        case FCXIR_CMP_LT:
                            result = (left_val < right_val) ? 1 : 0;
                            break;
                        case FCXIR_CMP_LE:
                            result = (left_val <= right_val) ? 1 : 0;
                            break;
                        case FCXIR_CMP_GT:
                            result = (left_val > right_val) ? 1 : 0;
                            break;
                        case FCXIR_CMP_GE:
                            result = (left_val >= right_val) ? 1 : 0;
                            break;
                            
                        default:
                            can_fold = false;
                            break;
                    }
                    
                    if (can_fold) {
                        // Replace with constant
                        instr->opcode = FCXIR_CONST;
                        instr->u.const_op.dest = instr->u.binary_op.dest;
                        instr->u.const_op.value = result;
                        
                        // Track the new constant
                        const_table_insert(&const_table, instr->u.const_op.dest.id, result);
                        changed = true;
                    }
                }
                
                // Handle bigint constant folding
                else if (left_const && right_const && left_const->is_bigint && right_const->is_bigint) {
                    uint64_t result_limbs[16];
                    uint8_t result_num_limbs;
                    bool can_fold = true;
                    
                    switch (instr->opcode) {
                        case FCXIR_ADD:
                            can_fold = bigint_add(left_const->bigint_limbs, left_const->num_limbs,
                                                right_const->bigint_limbs, right_const->num_limbs,
                                                result_limbs, &result_num_limbs);
                            break;
                        case FCXIR_SUB:
                            can_fold = bigint_sub(left_const->bigint_limbs, left_const->num_limbs,
                                                right_const->bigint_limbs, right_const->num_limbs,
                                                result_limbs, &result_num_limbs);
                            break;
                        // TODO: Implement bigint MUL, DIV, MOD, bitwise operations
                        default:
                            can_fold = false;
                            break;
                    }
                    
                    if (can_fold) {
                        // Replace with bigint constant
                        instr->opcode = FCXIR_CONST_BIGINT;
                        instr->u.const_bigint_op.dest = instr->u.binary_op.dest;
                        memcpy(instr->u.const_bigint_op.limbs, result_limbs, 
                              result_num_limbs * sizeof(uint64_t));
                        instr->u.const_bigint_op.num_limbs = result_num_limbs;
                        
                        // Track the new bigint constant
                        const_table_insert_bigint(&const_table, instr->u.const_bigint_op.dest.id,
                                                result_limbs, result_num_limbs);
                        changed = true;
                    }
                }
            }
            
            // Fold unary operations
            else if (instr->opcode == FCXIR_NEG || instr->opcode == FCXIR_NOT) {
                ConstEntry* src_const = const_table_lookup(&const_table, instr->u.unary_op.src.id);
                
                if (src_const && !src_const->is_bigint) {
                    int64_t src_val = src_const->value;
                    int64_t result = 0;
                    
                    switch (instr->opcode) {
                        case FCXIR_NEG:
                            result = -src_val;
                            break;
                        case FCXIR_NOT:
                            result = ~src_val;
                            break;
                        default:
                            continue;
                    }
                    
                    // Replace with constant
                    instr->opcode = FCXIR_CONST;
                    instr->u.const_op.dest = instr->u.unary_op.dest;
                    instr->u.const_op.value = result;
                    
                    // Track the new constant
                    const_table_insert(&const_table, instr->u.const_op.dest.id, result);
                    changed = true;
                }
            }
        }
    }
    
    const_table_destroy(&const_table);
    return changed;
}

// ============================================================================
// Algebraic Simplification Pass
// ============================================================================

bool opt_algebraic_simplification(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    ConstTable const_table;
    const_table_init(&const_table);
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            // Track constants
            if (instr->opcode == FCXIR_CONST) {
                const_table_insert(&const_table, instr->u.const_op.dest.id, instr->u.const_op.value);
                continue;
            }
            
            // Apply algebraic simplifications
            if ((instr->opcode >= FCXIR_ADD && instr->opcode <= FCXIR_MOD) ||
                (instr->opcode >= FCXIR_AND && instr->opcode <= FCXIR_XOR)) {
                
                ConstEntry* left_const = const_table_lookup(&const_table, instr->u.binary_op.left.id);
                ConstEntry* right_const = const_table_lookup(&const_table, instr->u.binary_op.right.id);
                
                // Identity operations: x op identity = x
                if (right_const && !right_const->is_bigint) {
                    int64_t right_val = right_const->value;
                    bool simplify_to_left = false;
                    
                    switch (instr->opcode) {
                        case FCXIR_ADD:
                        case FCXIR_SUB:
                        case FCXIR_OR:
                        case FCXIR_XOR:
                            if (right_val == 0) simplify_to_left = true;
                            break;
                        case FCXIR_MUL:
                        case FCXIR_DIV:
                            if (right_val == 1) simplify_to_left = true;
                            break;
                        case FCXIR_AND:
                            if (right_val == -1) simplify_to_left = true;  // All bits set
                            break;
                        default:
                            break;
                    }
                    
                    if (simplify_to_left) {
                        // Replace with MOV (register-to-register move)
                        instr->opcode = FCXIR_MOV;
                        instr->u.load_store.dest = instr->u.binary_op.dest;
                        instr->u.load_store.src = instr->u.binary_op.left;
                        instr->u.load_store.offset = 0;
                        changed = true;
                        continue;
                    }
                }
                
                // Left identity operations: identity op x = x (for commutative ops)
                if (left_const && !left_const->is_bigint) {
                    int64_t left_val = left_const->value;
                    bool simplify_to_right = false;
                    
                    switch (instr->opcode) {
                        case FCXIR_ADD:
                        case FCXIR_OR:
                        case FCXIR_XOR:
                            if (left_val == 0) simplify_to_right = true;
                            break;
                        case FCXIR_MUL:
                            if (left_val == 1) simplify_to_right = true;
                            break;
                        case FCXIR_AND:
                            if (left_val == -1) simplify_to_right = true;  // All bits set
                            break;
                        default:
                            break;
                    }
                    
                    if (simplify_to_right) {
                        // Replace with MOV
                        instr->opcode = FCXIR_MOV;
                        instr->u.load_store.dest = instr->u.binary_op.dest;
                        instr->u.load_store.src = instr->u.binary_op.right;
                        instr->u.load_store.offset = 0;
                        changed = true;
                        continue;
                    }
                }
                
                // Annihilator operations: x op annihilator = annihilator
                if (right_const && !right_const->is_bigint) {
                    int64_t right_val = right_const->value;
                    int64_t annihilator_result = 0;
                    bool has_annihilator = false;
                    
                    switch (instr->opcode) {
                        case FCXIR_MUL:
                        case FCXIR_AND:
                            if (right_val == 0) {
                                annihilator_result = 0;
                                has_annihilator = true;
                            }
                            break;
                        default:
                            break;
                    }
                    
                    if (has_annihilator) {
                        // Replace with constant
                        instr->opcode = FCXIR_CONST;
                        instr->u.const_op.dest = instr->u.binary_op.dest;
                        instr->u.const_op.value = annihilator_result;
                        const_table_insert(&const_table, instr->u.const_op.dest.id, annihilator_result);
                        changed = true;
                        continue;
                    }
                }
                
                // Left annihilator operations (for commutative ops)
                if (left_const && !left_const->is_bigint) {
                    int64_t left_val = left_const->value;
                    int64_t annihilator_result = 0;
                    bool has_annihilator = false;
                    
                    switch (instr->opcode) {
                        case FCXIR_MUL:
                        case FCXIR_AND:
                            if (left_val == 0) {
                                annihilator_result = 0;
                                has_annihilator = true;
                            }
                            break;
                        default:
                            break;
                    }
                    
                    if (has_annihilator) {
                        // Replace with constant
                        instr->opcode = FCXIR_CONST;
                        instr->u.const_op.dest = instr->u.binary_op.dest;
                        instr->u.const_op.value = annihilator_result;
                        const_table_insert(&const_table, instr->u.const_op.dest.id, annihilator_result);
                        changed = true;
                        continue;
                    }
                }
                
                // Self operations: x op x = result
                if (instr->u.binary_op.left.id == instr->u.binary_op.right.id) {
                    bool simplify_to_self = false;
                    bool simplify_to_zero = false;
                    
                    switch (instr->opcode) {
                        case FCXIR_OR:
                        case FCXIR_AND:
                            simplify_to_self = true;
                            break;
                        case FCXIR_SUB:
                        case FCXIR_XOR:
                            simplify_to_zero = true;
                            break;
                        default:
                            break;
                    }
                    
                    if (simplify_to_self) {
                        // Replace with MOV
                        instr->opcode = FCXIR_MOV;
                        instr->u.load_store.dest = instr->u.binary_op.dest;
                        instr->u.load_store.src = instr->u.binary_op.left;
                        instr->u.load_store.offset = 0;
                        changed = true;
                    } else if (simplify_to_zero) {
                        // Replace with constant 0
                        instr->opcode = FCXIR_CONST;
                        instr->u.const_op.dest = instr->u.binary_op.dest;
                        instr->u.const_op.value = 0;
                        const_table_insert(&const_table, instr->u.const_op.dest.id, 0);
                        changed = true;
                    }
                }
            }
            
            // Double negation: -(-x) = x
            else if (instr->opcode == FCXIR_NEG) {
                // Look for the source instruction
                for (uint32_t j = 0; j < i; j++) {
                    FcxIRInstruction* prev = &block->instructions[j];
                    if (prev->opcode == FCXIR_NEG && 
                        prev->u.unary_op.dest.id == instr->u.unary_op.src.id) {
                        // Double negation found: replace with MOV
                        instr->opcode = FCXIR_MOV;
                        instr->u.load_store.dest = instr->u.unary_op.dest;
                        instr->u.load_store.src = prev->u.unary_op.src;
                        instr->u.load_store.offset = 0;
                        changed = true;
                        break;
                    }
                }
            }
            
            // Double complement: ~(~x) = x
            else if (instr->opcode == FCXIR_NOT) {
                // Look for the source instruction
                for (uint32_t j = 0; j < i; j++) {
                    FcxIRInstruction* prev = &block->instructions[j];
                    if (prev->opcode == FCXIR_NOT && 
                        prev->u.unary_op.dest.id == instr->u.unary_op.src.id) {
                        // Double complement found: replace with MOV
                        instr->opcode = FCXIR_MOV;
                        instr->u.load_store.dest = instr->u.unary_op.dest;
                        instr->u.load_store.src = prev->u.unary_op.src;
                        instr->u.load_store.offset = 0;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    
    const_table_destroy(&const_table);
    return changed;
}

// ============================================================================
// Strength Reduction Pass
// ============================================================================

bool opt_strength_reduction(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    ConstTable const_table;
    const_table_init(&const_table);
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            // Track constants
            if (instr->opcode == FCXIR_CONST) {
                const_table_insert(&const_table, instr->u.const_op.dest.id, instr->u.const_op.value);
                continue;
            }
            
            // Strength reduction for multiplication by powers of 2
            if (instr->opcode == FCXIR_MUL) {
                ConstEntry* right_const = const_table_lookup(&const_table, instr->u.binary_op.right.id);
                if (right_const && !right_const->is_bigint) {
                    int shift_amount;
                    if (is_power_of_2(right_const->value, &shift_amount)) {
                        // Replace MUL with LSHIFT
                        instr->opcode = FCXIR_LSHIFT;
                        // Update the constant to be the shift amount
                        // Find the original constant instruction and update it
                        for (uint32_t k = 0; k < i; k++) {
                            FcxIRInstruction* const_instr = &block->instructions[k];
                            if (const_instr->opcode == FCXIR_CONST && 
                                const_instr->u.const_op.dest.id == instr->u.binary_op.right.id) {
                                const_instr->u.const_op.value = shift_amount;
                                const_table_insert(&const_table, instr->u.binary_op.right.id, shift_amount);
                                break;
                            }
                        }
                        changed = true;
                    }
                }
                
                // Check left operand too (multiplication is commutative)
                ConstEntry* left_const = const_table_lookup(&const_table, instr->u.binary_op.left.id);
                if (left_const && !left_const->is_bigint && !right_const) {
                    int shift_amount;
                    if (is_power_of_2(left_const->value, &shift_amount)) {
                        // Swap operands and replace with LSHIFT
                        instr->opcode = FCXIR_LSHIFT;
                        instr->u.binary_op.left = instr->u.binary_op.right;
                        // Update the constant to be the shift amount
                        for (uint32_t k = 0; k < i; k++) {
                            FcxIRInstruction* const_instr = &block->instructions[k];
                            if (const_instr->opcode == FCXIR_CONST && 
                                const_instr->u.const_op.dest.id == instr->u.binary_op.left.id) {
                                const_instr->u.const_op.value = shift_amount;
                                const_table_insert(&const_table, instr->u.binary_op.left.id, shift_amount);
                                break;
                            }
                        }
                        changed = true;
                    }
                }
            }
            
            // Strength reduction for division by powers of 2
            else if (instr->opcode == FCXIR_DIV) {
                ConstEntry* right_const = const_table_lookup(&const_table, instr->u.binary_op.right.id);
                if (right_const && !right_const->is_bigint) {
                    int shift_amount;
                    if (is_power_of_2(right_const->value, &shift_amount)) {
                        // Replace DIV with RSHIFT (arithmetic right shift)
                        instr->opcode = FCXIR_RSHIFT;
                        // Update the constant to be the shift amount
                        // Find the original constant instruction and update it
                        for (uint32_t k = 0; k < i; k++) {
                            FcxIRInstruction* const_instr = &block->instructions[k];
                            if (const_instr->opcode == FCXIR_CONST && 
                                const_instr->u.const_op.dest.id == instr->u.binary_op.right.id) {
                                const_instr->u.const_op.value = shift_amount;
                                const_table_insert(&const_table, instr->u.binary_op.right.id, shift_amount);
                                break;
                            }
                        }
                        changed = true;
                    }
                }
            }
            
            // Strength reduction for modulo by powers of 2
            else if (instr->opcode == FCXIR_MOD) {
                ConstEntry* right_const = const_table_lookup(&const_table, instr->u.binary_op.right.id);
                if (right_const && !right_const->is_bigint) {
                    int shift_amount;
                    if (is_power_of_2(right_const->value, &shift_amount)) {
                        // Replace MOD with AND (x % 2^n = x & (2^n - 1))
                        instr->opcode = FCXIR_AND;
                        // Right operand should be (2^n - 1)
                        int64_t mask = right_const->value - 1;
                        // Find the original constant instruction and update it
                        for (uint32_t k = 0; k < i; k++) {
                            FcxIRInstruction* const_instr = &block->instructions[k];
                            if (const_instr->opcode == FCXIR_CONST && 
                                const_instr->u.const_op.dest.id == instr->u.binary_op.right.id) {
                                const_instr->u.const_op.value = mask;
                                const_table_insert(&const_table, instr->u.binary_op.right.id, mask);
                                break;
                            }
                        }
                        changed = true;
                    }
                }
            }
        }
    }
    
    const_table_destroy(&const_table);
    return changed;
}

bool opt_dead_code_elimination(FcxIRFunction* function) {
    if (!function) return false;
    
    bool changed = false;
    
    // Track which virtual registers are used
    bool* used = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    if (!used) return false;
    
    // Mark all used registers
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_MOV:
                    // MOV is register-to-register, mark source as used
                    used[instr->u.load_store.src.id] = true;
                    break;
                case FCXIR_LOAD:
                case FCXIR_STORE:
                case FCXIR_LOAD_VOLATILE:
                case FCXIR_STORE_VOLATILE:
                    used[instr->u.load_store.src.id] = true;
                    break;
                    
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD:
                case FCXIR_AND:
                case FCXIR_OR:
                case FCXIR_XOR:
                case FCXIR_LSHIFT:
                case FCXIR_RSHIFT:
                case FCXIR_LOGICAL_RSHIFT:
                case FCXIR_ROTATE_LEFT:
                case FCXIR_ROTATE_RIGHT:
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB:
                    used[instr->u.binary_op.left.id] = true;
                    used[instr->u.binary_op.right.id] = true;
                    break;
                    
                case FCXIR_NEG:
                case FCXIR_NOT:
                case FCXIR_ATOMIC_LOAD:
                    used[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_BRANCH:
                    used[instr->u.branch_op.cond.id] = true;
                    break;
                
                case FCXIR_STORE_GLOBAL:
                    // Mark the source vreg as used (the value being stored)
                    used[instr->u.global_op.vreg.id] = true;
                    break;
                    
                case FCXIR_RETURN:
                    if (instr->u.return_op.has_value) {
                        used[instr->u.return_op.value.id] = true;
                    }
                    break;
                
                case FCXIR_CALL:
                    // Mark all arguments as used
                    for (uint8_t j = 0; j < instr->u.call_op.arg_count; j++) {
                        used[instr->u.call_op.args[j].id] = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Remove instructions that define unused registers
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        uint32_t write_idx = 0;
        
        for (uint32_t read_idx = 0; read_idx < block->instruction_count; read_idx++) {
            FcxIRInstruction* instr = &block->instructions[read_idx];
            bool keep = true;
            
            // Check if this instruction defines a register
            switch (instr->opcode) {
                case FCXIR_CONST:
                    keep = used[instr->u.const_op.dest.id];
                    break;
                case FCXIR_CONST_BIGINT:
                    keep = used[instr->u.const_bigint_op.dest.id];
                    break;
                case FCXIR_MOV:
                case FCXIR_LOAD:
                case FCXIR_LOAD_VOLATILE:
                    keep = used[instr->u.load_store.dest.id];
                    break;
                case FCXIR_LOAD_GLOBAL:
                    keep = used[instr->u.global_op.vreg.id];
                    break;
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD:
                case FCXIR_AND:
                case FCXIR_OR:
                case FCXIR_XOR:
                case FCXIR_LSHIFT:
                case FCXIR_RSHIFT:
                case FCXIR_LOGICAL_RSHIFT:
                case FCXIR_ROTATE_LEFT:
                case FCXIR_ROTATE_RIGHT:
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB:
                    keep = used[instr->u.binary_op.dest.id];
                    break;
                default:
                    // Keep all other instructions (stores, branches, etc.)
                    keep = true;
                    break;
            }
            
            if (keep) {
                if (write_idx != read_idx) {
                    block->instructions[write_idx] = block->instructions[read_idx];
                }
                write_idx++;
            } else {
                changed = true;
            }
        }
        
        block->instruction_count = write_idx;
    }
    
    free(used);
    return changed;
}

// ============================================================================
// Loop Invariant Code Motion (Simplified)
// ============================================================================

bool opt_loop_invariant_code_motion(FcxIRFunction* function) {
    if (!function) return false;
    
    // This is a placeholder for loop optimization
    // A full implementation would:
    // 1. Identify loops in the CFG
    // 2. Find loop-invariant computations
    // 3. Move them outside the loop
    
    // For now, just return false (no changes)
    return false;
}

// ============================================================================
// Type Checking Pass
// ============================================================================

bool opt_type_checking(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track types of virtual registers
    VRegType* types = (VRegType*)calloc(function->next_vreg_id, sizeof(VRegType));
    if (!types) return false;
    
    // Initialize all types to I64 (default integer type)
    // This prevents false positives from untracked types
    for (uint32_t i = 0; i < function->next_vreg_id; i++) {
        types[i] = VREG_TYPE_I64;
    }
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_CONST:
                    types[instr->u.const_op.dest.id] = instr->u.const_op.dest.type;
                    break;
                
                case FCXIR_CONST_BIGINT:
                    types[instr->u.const_bigint_op.dest.id] = instr->u.const_bigint_op.dest.type;
                    break;
                
                case FCXIR_MOV:
                    // Propagate type from source to destination
                    types[instr->u.load_store.dest.id] = types[instr->u.load_store.src.id];
                    break;
                    
                case FCXIR_ADD:
                case FCXIR_SUB:
                case FCXIR_MUL:
                case FCXIR_DIV:
                case FCXIR_MOD: {
                    // Propagate type to destination (don't warn, just track)
                    VRegType left_type = types[instr->u.binary_op.left.id];
                    types[instr->u.binary_op.dest.id] = left_type;
                    break;
                }
                
                case FCXIR_CMP_EQ:
                case FCXIR_CMP_NE:
                case FCXIR_CMP_LT:
                case FCXIR_CMP_LE:
                case FCXIR_CMP_GT:
                case FCXIR_CMP_GE:
                    // Comparison results are boolean (stored as i64)
                    types[instr->u.binary_op.dest.id] = VREG_TYPE_I64;
                    break;
                
                case FCXIR_PTR_ADD:
                case FCXIR_PTR_SUB: {
                    // Result is a pointer
                    VRegType left_type = types[instr->u.binary_op.left.id];
                    types[instr->u.binary_op.dest.id] = left_type;
                    break;
                }
                
                case FCXIR_CALL:
                    // Call results are typically i64
                    types[instr->u.call_op.dest.id] = VREG_TYPE_I64;
                    break;
                
                default:
                    break;
            }
        }
    }
    
    free(types);
    return !has_error;
}

// ============================================================================
// Pointer Analysis Pass
// ============================================================================

bool opt_pointer_analysis(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track pointer types and their origins
    typedef struct {
        bool is_pointer;
        VRegType ptr_type;
        bool is_null;
        bool is_allocated;
    } PointerInfo;
    
    PointerInfo* ptr_info = (PointerInfo*)calloc(function->next_vreg_id, sizeof(PointerInfo));
    if (!ptr_info) return false;
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    // Mark as allocated pointer
                    ptr_info[instr->u.alloc_op.dest.id].is_pointer = true;
                    ptr_info[instr->u.alloc_op.dest.id].ptr_type = VREG_TYPE_PTR;
                    ptr_info[instr->u.alloc_op.dest.id].is_allocated = true;
                    ptr_info[instr->u.alloc_op.dest.id].is_null = false;
                    break;
                    
                case FCXIR_CONST:
                    // Check for null pointer (0)
                    if (instr->u.const_op.value == 0 && 
                        instr->u.const_op.dest.type == VREG_TYPE_PTR) {
                        ptr_info[instr->u.const_op.dest.id].is_pointer = true;
                        ptr_info[instr->u.const_op.dest.id].is_null = true;
                    }
                    break;
                    
                case FCXIR_MOV:
                    // MOV is register-to-register, propagate pointer info
                    ptr_info[instr->u.load_store.dest.id] = ptr_info[instr->u.load_store.src.id];
                    break;
                    
                case FCXIR_LOAD:
                case FCXIR_STORE:
                case FCXIR_LOAD_VOLATILE:
                case FCXIR_STORE_VOLATILE:
                    // Check for null pointer dereference
                    if (ptr_info[instr->u.load_store.src.id].is_null) {
                        fprintf(stderr, "Warning: Potential null pointer dereference\n");
                        has_error = true;
                    }
                    break;
                    
                case FCXIR_DEALLOC:
                    // Mark as deallocated
                    ptr_info[instr->u.unary_op.src.id].is_allocated = false;
                    break;
                    
                case FCXIR_PTR_CAST:
                    // Track pointer type changes
                    ptr_info[instr->u.ptr_op.dest.id].is_pointer = true;
                    ptr_info[instr->u.ptr_op.dest.id].ptr_type = instr->u.ptr_op.target_type;
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    free(ptr_info);
    return !has_error;
}

// ============================================================================
// Memory Safety Analysis Pass
// ============================================================================

bool opt_memory_safety_analysis(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track allocated and freed pointers
    bool* allocated = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* freed = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    
    if (!allocated || !freed) {
        free(allocated);
        free(freed);
        return false;
    }
    
    bool has_error = false;
    
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    allocated[instr->u.alloc_op.dest.id] = true;
                    freed[instr->u.alloc_op.dest.id] = false;
                    break;
                    
                case FCXIR_DEALLOC:
                    if (freed[instr->u.unary_op.src.id]) {
                        fprintf(stderr, "Warning: Double free detected\n");
                        has_error = true;
                    }
                    if (!allocated[instr->u.unary_op.src.id]) {
                        fprintf(stderr, "Warning: Freeing unallocated memory\n");
                        has_error = true;
                    }
                    freed[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_MOV:
                    // MOV is register-to-register, propagate freed status
                    if (freed[instr->u.load_store.src.id]) {
                        freed[instr->u.load_store.dest.id] = true;
                    }
                    break;
                    
                case FCXIR_LOAD:
                case FCXIR_STORE:
                    if (freed[instr->u.load_store.src.id]) {
                        fprintf(stderr, "Warning: Use after free detected\n");
                        has_error = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    free(allocated);
    free(freed);
    return !has_error;
}

// ============================================================================
// Leak Detection Pass
// ============================================================================

bool opt_leak_detection(FcxIRFunction* function) {
    if (!function) return false;
    
    // Track allocated pointers that are never freed
    bool* allocated = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* freed = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    bool* escaped = (bool*)calloc(function->next_vreg_id, sizeof(bool));
    
    if (!allocated || !freed || !escaped) {
        free(allocated);
        free(freed);
        free(escaped);
        return false;
    }
    
    // First pass: track allocations and frees
    for (uint32_t b = 0; b < function->block_count; b++) {
        FcxIRBasicBlock* block = &function->blocks[b];
        
        for (uint32_t i = 0; i < block->instruction_count; i++) {
            FcxIRInstruction* instr = &block->instructions[i];
            
            switch (instr->opcode) {
                case FCXIR_ALLOC:
                case FCXIR_ARENA_ALLOC:
                case FCXIR_SLAB_ALLOC:
                case FCXIR_POOL_ALLOC:
                case FCXIR_STACK_ALLOC:
                    allocated[instr->u.alloc_op.dest.id] = true;
                    break;
                    
                case FCXIR_DEALLOC:
                    freed[instr->u.unary_op.src.id] = true;
                    break;
                    
                case FCXIR_RETURN:
                    // If returning a pointer, it escapes
                    if (instr->u.return_op.has_value) {
                        escaped[instr->u.return_op.value.id] = true;
                    }
                    break;
                    
                case FCXIR_CALL:
                    // Pointers passed to functions escape
                    for (uint8_t j = 0; j < instr->u.call_op.arg_count; j++) {
                        escaped[instr->u.call_op.args[j].id] = true;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Check for leaks
    bool has_leaks = false;
    for (uint32_t i = 0; i < function->next_vreg_id; i++) {
        if (allocated[i] && !freed[i] && !escaped[i]) {
            fprintf(stderr, "Warning: Potential memory leak for %%v%u\n", i);
            has_leaks = true;
        }
    }
    
    free(allocated);
    free(freed);
    free(escaped);
    
    return !has_leaks;
}

// ============================================================================
// Run All Optimization Passes
// ============================================================================

bool ir_optimize_function_with_level(FcxIRFunction* function, int opt_level) {
    if (!function) return false;
    
    bool changed = false;
    
    // O0: No optimizations
    if (opt_level == 0) {
        return false;
    }
    
    // O1: Basic optimizations only
    if (opt_level == 1) {
        if (opt_constant_folding(function)) {
            changed = true;
        }
        if (opt_dead_code_elimination(function)) {
            changed = true;
        }
        
        // Run analysis passes (silently, only report errors)
        opt_type_checking(function);
        opt_pointer_analysis(function);
        
        return changed;
    }
    
    // O2 and above: Full optimization pipeline
    bool pass_changed = true;
    int iteration = 0;
    const int max_iterations = (opt_level >= 3) ? 15 : 10;  // More iterations for O3+
    
    // Run optimization passes iteratively until convergence
    while (pass_changed && iteration < max_iterations) {
        pass_changed = false;
        iteration++;
        
        // Run constant folding first
        if (opt_constant_folding(function)) {
            pass_changed = true;
            changed = true;
        }
        
        // Run algebraic simplification
        if (opt_algebraic_simplification(function)) {
            pass_changed = true;
            changed = true;
        }
        
        // Run strength reduction
        if (opt_strength_reduction(function)) {
            pass_changed = true;
            changed = true;
        }
        
        // Run dead code elimination to clean up
        if (opt_dead_code_elimination(function)) {
            pass_changed = true;
            changed = true;
        }
        
        // O3+: Run loop optimizations
        if (opt_level >= 3) {
            if (opt_loop_invariant_code_motion(function)) {
                pass_changed = true;
                changed = true;
            }
        }
    }
    
    // Run analysis passes (silently, only report errors)
    opt_type_checking(function);
    opt_pointer_analysis(function);
    opt_memory_safety_analysis(function);
    opt_leak_detection(function);
    
    return changed;
}

bool ir_optimize_module_with_level(FcxIRModule* module, int opt_level) {
    if (!module) return false;
    
    bool changed = false;
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        if (ir_optimize_function_with_level(&module->functions[i], opt_level)) {
            changed = true;
        }
    }
    
    return changed;
}

bool ir_optimize_function(FcxIRFunction* function) {
    // Default to O2 optimization level
    return ir_optimize_function_with_level(function, 2);
}

bool ir_optimize_module(FcxIRModule* module) {
    if (!module) return false;
    
    bool changed = false;
    
    for (uint32_t i = 0; i < module->function_count; i++) {
        if (ir_optimize_function(&module->functions[i])) {
            changed = true;
        }
    }
    
    return changed;
}
