#ifndef IR_OPTIMIZE_H
#define IR_OPTIMIZE_H

#include "fcx_ir.h"
#include <stdbool.h>

// Optimization pass interface
typedef struct {
    const char* name;
    bool (*run)(FcxIRFunction* function);
} OptimizationPass;

// Constant folding pass
bool opt_constant_folding(FcxIRFunction* function);

// Algebraic simplification pass
bool opt_algebraic_simplification(FcxIRFunction* function);

// Strength reduction pass
bool opt_strength_reduction(FcxIRFunction* function);

// Dead code elimination pass
bool opt_dead_code_elimination(FcxIRFunction* function);

// Basic loop optimizations
bool opt_loop_invariant_code_motion(FcxIRFunction* function);

// Type checking and pointer analysis
bool opt_type_checking(FcxIRFunction* function);
bool opt_pointer_analysis(FcxIRFunction* function);

// Memory safety analysis
bool opt_memory_safety_analysis(FcxIRFunction* function);
bool opt_leak_detection(FcxIRFunction* function);

// Run all optimization passes on a function
bool ir_optimize_function(FcxIRFunction* function);
bool ir_optimize_function_with_level(FcxIRFunction* function, int opt_level);

// Run all optimization passes on a module
bool ir_optimize_module(FcxIRModule* module);
bool ir_optimize_module_with_level(FcxIRModule* module, int opt_level);

#endif // IR_OPTIMIZE_H
