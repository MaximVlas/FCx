/**
 * FCx Hierarchical Multi-Stage Optimizer (HMSO)
 * 
 * Design Philosophy:
 * Iterative refinement through progressive context expansion.
 * Start with minimal context (fast), gradually incorporate more context (accurate),
 * and iterate until convergence or resource limits.
 */

#ifndef FCX_HMSO_H
#define FCX_HMSO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include "../ir/fcx_ir.h"

// ============================================================================
// FCX Object File Format (.fcx.o)
// ============================================================================

#define FCXO_MAGIC 0x4F584346  // "FCXO" in little-endian
#define FCXO_VERSION 1

typedef struct {
    uint32_t magic;                    // "FCXO"
    uint32_t version;
    
    // Machine code section
    uint64_t code_offset;
    uint64_t code_size;
    
    // IR section (for LTO)
    uint64_t ir_offset;
    uint64_t ir_size;
    
    // Summary metadata
    uint64_t summary_offset;
    uint64_t summary_size;
    
    // Profile data section (optional)
    uint64_t profile_offset;
    uint64_t profile_size;
} FCXObjectHeader;

// ============================================================================
// Function Summary - Lightweight metadata for global analysis
// ============================================================================

// Function behavior flags
typedef enum {
    FUNC_FLAG_NONE          = 0,
    FUNC_FLAG_PURE          = (1 << 0),   // No side effects, same inputs = same outputs
    FUNC_FLAG_CONST         = (1 << 1),   // Pure + no memory reads
    FUNC_FLAG_NOTHROW       = (1 << 2),   // Never throws/panics
    FUNC_FLAG_NORECURSE     = (1 << 3),   // Never calls itself (directly or indirectly)
    FUNC_FLAG_NORETURN      = (1 << 4),   // Never returns (exit, abort, infinite loop)
    FUNC_FLAG_LEAF          = (1 << 5),   // Makes no function calls
    FUNC_FLAG_INLINE_HINT   = (1 << 6),   // Suggested for inlining
    FUNC_FLAG_NOINLINE      = (1 << 7),   // Should not be inlined
    FUNC_FLAG_HOT           = (1 << 8),   // Frequently executed
    FUNC_FLAG_COLD          = (1 << 9),   // Rarely executed
    FUNC_FLAG_STARTUP       = (1 << 10),  // Initialization code
    FUNC_FLAG_VECTORIZABLE  = (1 << 11),  // Contains vectorizable loops
    FUNC_FLAG_HAS_ATOMICS   = (1 << 12),  // Uses atomic operations
    FUNC_FLAG_HAS_SYSCALLS  = (1 << 13),  // Makes system calls
} FunctionFlags;

// Memory access pattern
typedef enum {
    MEM_ACCESS_NONE     = 0,
    MEM_ACCESS_READ     = (1 << 0),
    MEM_ACCESS_WRITE    = (1 << 1),
    MEM_ACCESS_ARGMEM   = (1 << 2),   // Accesses memory through arguments
    MEM_ACCESS_GLOBAL   = (1 << 3),   // Accesses global memory
    MEM_ACCESS_ALLOC    = (1 << 4),   // Allocates memory
    MEM_ACCESS_FREE     = (1 << 5),   // Frees memory
} MemoryAccessFlags;

// Pointer aliasing information
typedef struct {
    uint32_t num_pointers;
    struct {
        uint32_t vreg_id;
        uint32_t alias_set;          // Pointers in same set may alias
        bool may_escape;             // Pointer may escape function
        bool is_restrict;            // Pointer is restrict-qualified
    } *pointers;
} PointerAliasing;

// Call site information
typedef struct {
    char *callee_name;               // Name of called function
    uint64_t callee_hash;            // Hash for fast lookup
    uint32_t call_count;             // Static count in function
    uint32_t arg_count;
    bool is_indirect;                // Indirect call (function pointer)
    bool is_tail_call;               // Tail call optimization candidate
} CallSite;

// Profile data for a function
typedef struct {
    uint64_t execution_count;        // Total executions
    uint64_t *block_counts;          // Per-basic-block counts
    uint32_t num_blocks;
    double *branch_probs;            // Branch probabilities
    uint32_t num_branches;
    uint64_t total_cycles;           // Estimated cycle count
} ProfileData;

// Function summary - lightweight metadata
typedef struct {
    char *name;
    uint64_t hash;                   // Content hash for incremental builds
    
    // Cost metrics
    uint32_t instruction_count;
    uint32_t basic_block_count;
    uint32_t cyclomatic_complexity;
    uint32_t loop_depth_max;
    
    // Behavior flags
    uint32_t flags;                  // FunctionFlags
    
    // Memory behavior
    uint32_t memory_access;          // MemoryAccessFlags
    PointerAliasing aliasing_info;
    
    // Call information
    uint32_t num_callsites;
    CallSite *callsites;
    
    // Profile data (optional)
    ProfileData *profile;
    
    // Optimization decisions
    bool is_hot;
    bool is_inline_candidate;
    uint32_t inline_cost;
    uint32_t inline_benefit;
} FunctionSummary;

// Global variable summary
typedef struct {
    char *name;
    uint64_t hash;
    uint32_t size;
    uint32_t alignment;
    bool is_constant;
    bool is_thread_local;
    uint32_t num_readers;            // Functions that read this global
    uint32_t num_writers;            // Functions that write this global
} GlobalSummary;

// Call graph edge
typedef struct {
    uint32_t caller_idx;
    uint32_t callee_idx;
    uint32_t call_count;             // Static count
    uint64_t dynamic_count;          // From profile (if available)
    bool is_hot;
} CallEdge;

// ============================================================================
// Compilation Summary - Per-file metadata
// ============================================================================

typedef struct {
    // Function summaries
    uint32_t num_functions;
    FunctionSummary *functions;
    
    // Global variable summaries
    uint32_t num_globals;
    GlobalSummary *globals;
    
    // Call graph edges (within this unit)
    uint32_t num_edges;
    CallEdge *edges;
    
    // Source file info
    char *source_path;
    uint64_t source_hash;
    uint64_t timestamp;
} CompilationSummary;

// ============================================================================
// Global Index - Program-wide knowledge base
// ============================================================================

// Compilation unit reference
typedef struct {
    char *path;
    FCXObjectHeader header;
    CompilationSummary *summary;
    void *ir_data;                   // Loaded on demand
    bool ir_loaded;
} CompilationUnit;

// Unified call graph
typedef struct {
    uint32_t num_nodes;
    uint32_t num_edges;
    
    struct {
        char *name;
        uint32_t unit_idx;           // Which compilation unit
        uint32_t func_idx;           // Index within unit
        uint32_t *callers;           // Indices of calling functions
        uint32_t num_callers;
        uint32_t *callees;           // Indices of called functions
        uint32_t num_callees;
        uint32_t scc_id;             // Strongly connected component ID
        bool is_reachable;           // Reachable from entry points
    } *nodes;
    
    CallEdge *edges;
} CallGraph;

// Hot path - sequence of frequently executed functions
typedef struct {
    uint32_t *function_indices;
    uint32_t length;
    uint64_t execution_count;
    double hotness_score;
} HotPath;

typedef struct {
    HotPath *paths;
    uint32_t count;
    uint32_t capacity;
} HotPathDB;

// Optimization opportunity
typedef struct {
    enum {
        OPP_INLINE,
        OPP_DEVIRTUALIZE,
        OPP_CONSTANT_PROP,
        OPP_DEAD_CODE,
        OPP_LOOP_UNROLL,
        OPP_VECTORIZE,
        OPP_MERGE_FUNCTIONS,
    } type;
    
    uint32_t func_idx;
    uint32_t target_idx;             // For inline: callee; for merge: other function
    double expected_benefit;
    uint32_t estimated_cost;
} OptimizationOpportunity;

typedef struct {
    OptimizationOpportunity *opportunities;
    uint32_t count;
    uint32_t capacity;
} OpportunityQueue;

// Global index structure
typedef struct {
    // All compilation units
    uint32_t num_units;
    CompilationUnit *units;
    
    // Unified call graph
    CallGraph *call_graph;
    
    // Cross-module reference tracking
    struct {
        char **keys;
        uint32_t *unit_indices;
        uint32_t count;
        uint32_t capacity;
    } symbol_table;
    
    struct {
        char **keys;
        uint32_t **user_indices;
        uint32_t *user_counts;
        uint32_t count;
        uint32_t capacity;
    } reference_map;
    
    // Hot path identification
    HotPathDB *hot_paths;
    
    // Optimization opportunities
    OpportunityQueue *opportunities;
    
    // Entry points
    uint32_t *entry_points;
    uint32_t num_entry_points;
} GlobalIndex;

// ============================================================================
// Optimization Chunks - Partitioned optimization units
// ============================================================================

typedef struct {
    uint32_t id;
    uint32_t *function_indices;
    uint32_t num_functions;
    
    uint32_t total_instructions;
    double hotness_score;
    
    // Optimization budget
    uint32_t opt_level;              // How aggressively to optimize
    bool enable_expensive_opts;
    
    // State
    bool optimized;
    void *optimized_ir;
} OptimizationChunk;

// ============================================================================
// Optimization Configuration
// ============================================================================

typedef enum {
    OPT_LEVEL_O0 = 0,    // Debug - no LTO
    OPT_LEVEL_O1,        // Quick - basic local opts
    OPT_LEVEL_O2,        // Standard - thin LTO
    OPT_LEVEL_O3,        // Aggressive - full LTO
    OPT_LEVEL_OMAX,      // Maximum - iterative refinement
} OptLevel;

typedef struct {
    OptLevel level;
    bool enable_expensive_opts;      // Polyhedral, superoptimization
    uint32_t inline_threshold;
    uint32_t unroll_count;
    bool vectorize;
    bool enable_lto;
    uint32_t lto_iterations;
    uint32_t chunk_size_min;
    uint32_t chunk_size_max;
    uint32_t num_threads;
    double convergence_threshold;
    bool use_profile;
    char *profile_path;
} HMSOConfig;

// Default configurations
extern const HMSOConfig HMSO_CONFIG_O0;
extern const HMSOConfig HMSO_CONFIG_O1;
extern const HMSOConfig HMSO_CONFIG_O2;
extern const HMSOConfig HMSO_CONFIG_O3;
extern const HMSOConfig HMSO_CONFIG_OMAX;

// ============================================================================
// HMSO Context - Main optimizer state
// ============================================================================

typedef struct {
    HMSOConfig config;
    GlobalIndex *global_index;
    OptimizationChunk **chunks;
    uint32_t num_chunks;
    
    // Thread pool for parallel optimization
    pthread_t *threads;
    uint32_t num_threads;
    
    // Statistics
    struct {
        uint64_t functions_optimized;
        uint64_t instructions_before;
        uint64_t instructions_after;
        uint64_t inlines_performed;
        uint64_t dead_code_removed;
        double total_time_ms;
    } stats;
} HMSOContext;

// ============================================================================
// API Functions
// ============================================================================

// Context management
HMSOContext *hmso_create(const HMSOConfig *config);
void hmso_destroy(HMSOContext *ctx);

// Stage 0: Initial compilation (per-file)
bool hmso_compile_file(HMSOContext *ctx, const char *source_path, 
                       const char *output_path);
CompilationSummary *hmso_generate_summary(FcxIRModule *module);
bool hmso_write_object_file(const char *path, const void *code, size_t code_size,
                            const void *ir, size_t ir_size,
                            const CompilationSummary *summary);

// Stage 1: Global index construction
GlobalIndex *hmso_build_global_index(const char **object_files, uint32_t count);
void hmso_free_global_index(GlobalIndex *idx);
CallGraph *hmso_build_call_graph(GlobalIndex *idx);
void hmso_mark_live_code(GlobalIndex *idx);

// Stage 2: Partitioning
OptimizationChunk **hmso_partition_program(GlobalIndex *idx, ProfileData *profile,
                                           uint32_t *out_count);
OptimizationChunk **hmso_call_graph_partition(GlobalIndex *idx, uint32_t *out_count);
OptimizationChunk **hmso_profile_guided_partition(GlobalIndex *idx, 
                                                   ProfileData *profile,
                                                   uint32_t *out_count);

// Stage 3: Parallel chunk optimization
void hmso_optimize_chunk(OptimizationChunk *chunk, GlobalIndex *idx,
                         const HMSOConfig *config);
void hmso_optimize_all_chunks_parallel(HMSOContext *ctx);

// Stage 4: Cross-chunk optimization
void hmso_optimize_cross_chunk(HMSOContext *ctx);

// Stage 5: Iterative refinement
void hmso_iterative_optimize(HMSOContext *ctx, uint32_t max_iterations);

// Stage 6: Final link and layout
bool hmso_final_link(HMSOContext *ctx, const char *output_path);

// High-level API
bool hmso_optimize_program(const char **source_files, uint32_t count,
                           const char *output_path, const HMSOConfig *config);

// Incremental builds
typedef struct {
    char *source_path;
    uint64_t source_hash;
    uint64_t dependency_hash;
    uint64_t timestamp;
    char *cached_object_path;
    CompilationSummary *cached_summary;
} CacheEntry;

typedef struct {
    CacheEntry *entries;
    uint32_t count;
    uint32_t capacity;
    char *cache_dir;
} BuildCache;

BuildCache *hmso_cache_create(const char *cache_dir);
void hmso_cache_destroy(BuildCache *cache);
bool hmso_needs_recompilation(BuildCache *cache, const char *source_path);
void hmso_incremental_build(HMSOContext *ctx, BuildCache *cache,
                            const char **source_files, uint32_t count);

// Profile-guided optimization
ProfileData *hmso_load_profile(const char *profile_path);
void hmso_free_profile(ProfileData *profile);
bool hmso_merge_profiles(const char **profile_paths, uint32_t count,
                         const char *output_path);

// Utility functions
uint64_t hmso_hash_file(const char *path);
uint64_t hmso_hash_function(const FcxIRFunction *func);
void hmso_print_stats(const HMSOContext *ctx);

#endif // FCX_HMSO_H
