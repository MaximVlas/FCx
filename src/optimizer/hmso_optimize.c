/**
 * FCx HMSO - Chunk Optimization (Stage 3) and Cross-Chunk Optimization (Stage 4)
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include "../ir/fcx_ir.h"
#include "../ir/ir_optimize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <math.h>

// ============================================================================
// Interprocedural Optimizations
// ============================================================================

// Inline cost/benefit analysis
typedef struct {
    uint32_t caller_idx;
    uint32_t callee_idx;
    uint32_t call_count;
    int32_t benefit;           // Positive = good to inline
    bool should_inline;
} InlineCandidate;

static int32_t calculate_inline_benefit(FunctionSummary *caller, 
                                        FunctionSummary *callee,
                                        uint32_t call_count) {
    if (!caller || !callee) return INT32_MIN;
    
    // Don't inline if callee is too large
    if (callee->instruction_count > 200) return INT32_MIN;
    
    // Don't inline recursive functions
    if (callee->flags & FUNC_FLAG_NORECURSE) {
        // OK to inline
    } else {
        // Check for direct recursion
        for (uint32_t i = 0; i < callee->num_callsites; i++) {
            if (callee->callsites[i].callee_name &&
                strcmp(callee->callsites[i].callee_name, callee->name) == 0) {
                return INT32_MIN;  // Don't inline recursive
            }
        }
    }
    
    // Calculate benefit
    int32_t benefit = 0;
    
    // Benefit from eliminating call overhead
    benefit += 10 * call_count;
    
    // Benefit from enabling further optimizations
    if (callee->flags & FUNC_FLAG_PURE) {
        benefit += 20;  // Pure functions enable more CSE
    }
    if (callee->flags & FUNC_FLAG_CONST) {
        benefit += 30;  // Const functions enable constant folding
    }
    if (callee->flags & FUNC_FLAG_LEAF) {
        benefit += 15;  // Leaf functions are cheap to inline
    }
    
    // Cost from code size increase
    int32_t size_cost = callee->instruction_count * call_count;
    benefit -= size_cost / 10;
    
    // Bonus for small functions
    if (callee->instruction_count < 20) {
        benefit += 50;
    } else if (callee->instruction_count < 50) {
        benefit += 20;
    }
    
    // Bonus for hot callers
    if (caller->is_hot) {
        benefit *= 2;
    }
    
    return benefit;
}

// Find inline candidates within a chunk
static InlineCandidate *find_inline_candidates(OptimizationChunk *chunk,
                                               GlobalIndex *idx,
                                               uint32_t threshold,
                                               uint32_t *out_count) {
    if (!chunk || !idx) {
        *out_count = 0;
        return NULL;
    }
    
    uint32_t capacity = 64;
    InlineCandidate *candidates = (InlineCandidate *)malloc(capacity * sizeof(InlineCandidate));
    if (!candidates) {
        *out_count = 0;
        return NULL;
    }
    
    uint32_t count = 0;
    
    // Check each function in chunk
    for (uint32_t i = 0; i < chunk->num_functions; i++) {
        uint32_t caller_idx = chunk->function_indices[i];
        
        // Get caller summary
        uint32_t unit_idx = idx->call_graph->nodes[caller_idx].unit_idx;
        uint32_t local_idx = idx->call_graph->nodes[caller_idx].func_idx;
        
        if (unit_idx >= idx->num_units || !idx->units[unit_idx].summary) continue;
        
        FunctionSummary *caller = &idx->units[unit_idx].summary->functions[local_idx];
        
        // Check each call site
        for (uint32_t c = 0; c < caller->num_callsites; c++) {
            CallSite *site = &caller->callsites[c];
            if (!site->callee_name || site->is_indirect) continue;
            
            // Find callee in chunk
            for (uint32_t j = 0; j < chunk->num_functions; j++) {
                uint32_t callee_idx = chunk->function_indices[j];
                
                if (!idx->call_graph->nodes[callee_idx].name) continue;
                if (strcmp(idx->call_graph->nodes[callee_idx].name, 
                          site->callee_name) != 0) continue;
                
                // Found callee - calculate benefit
                uint32_t callee_unit = idx->call_graph->nodes[callee_idx].unit_idx;
                uint32_t callee_local = idx->call_graph->nodes[callee_idx].func_idx;
                
                if (callee_unit >= idx->num_units || 
                    !idx->units[callee_unit].summary) continue;
                
                FunctionSummary *callee = &idx->units[callee_unit].summary->functions[callee_local];
                
                int32_t benefit = calculate_inline_benefit(caller, callee, site->call_count);
                
                if (benefit > (int32_t)threshold) {
                    // Grow array if needed
                    if (count >= capacity) {
                        capacity *= 2;
                        InlineCandidate *new_cands = (InlineCandidate *)realloc(
                            candidates, capacity * sizeof(InlineCandidate));
                        if (!new_cands) break;
                        candidates = new_cands;
                    }
                    
                    candidates[count].caller_idx = caller_idx;
                    candidates[count].callee_idx = callee_idx;
                    candidates[count].call_count = site->call_count;
                    candidates[count].benefit = benefit;
                    candidates[count].should_inline = true;
                    count++;
                }
                
                break;  // Found callee, no need to continue
            }
        }
    }
    
    *out_count = count;
    return candidates;
}

// ============================================================================
// Chunk Optimization
// ============================================================================

// Local context for chunk optimization
typedef struct {
    OptimizationChunk *chunk;
    GlobalIndex *idx;
    FcxIRModule *ir;              // Loaded IR for this chunk
    InlineCandidate *inlines;
    uint32_t num_inlines;
    
    // Statistics
    uint32_t instructions_before;
    uint32_t instructions_after;
    uint32_t inlines_performed;
} LocalContext;

static LocalContext *create_local_context(OptimizationChunk *chunk, GlobalIndex *idx) {
    LocalContext *ctx = (LocalContext *)calloc(1, sizeof(LocalContext));
    if (!ctx) return NULL;
    
    ctx->chunk = chunk;
    ctx->idx = idx;
    ctx->ir = NULL;
    ctx->inlines = NULL;
    ctx->num_inlines = 0;
    
    return ctx;
}

static void destroy_local_context(LocalContext *ctx) {
    if (!ctx) return;
    
    free(ctx->inlines);
    // Note: IR is owned by chunk, don't free here
    free(ctx);
}

// Load IR for chunk functions
static bool load_chunk_ir(LocalContext *ctx) {
    if (!ctx || !ctx->chunk || !ctx->idx) return false;
    
    // In a real implementation, this would load IR from object files
    // For now, we assume IR is already available in memory
    
    // Create a module to hold chunk functions
    ctx->ir = fcx_ir_module_create("chunk_module");
    if (!ctx->ir) return false;
    
    // Count total instructions
    ctx->instructions_before = 0;
    for (uint32_t i = 0; i < ctx->chunk->num_functions; i++) {
        uint32_t func_idx = ctx->chunk->function_indices[i];
        uint32_t unit_idx = ctx->idx->call_graph->nodes[func_idx].unit_idx;
        uint32_t local_idx = ctx->idx->call_graph->nodes[func_idx].func_idx;
        
        if (unit_idx < ctx->idx->num_units && ctx->idx->units[unit_idx].summary) {
            FunctionSummary *sum = &ctx->idx->units[unit_idx].summary->functions[local_idx];
            ctx->instructions_before += sum->instruction_count;
        }
    }
    
    return true;
}

// Perform inlining within chunk
static void perform_inlining(LocalContext *ctx, uint32_t threshold) {
    if (!ctx || !ctx->ir) return;
    
    // Find inline candidates
    ctx->inlines = find_inline_candidates(ctx->chunk, ctx->idx, threshold, &ctx->num_inlines);
    
    if (ctx->num_inlines == 0) return;
    
    printf("    Found %u inline candidates\n", ctx->num_inlines);
    
    // Sort by benefit (highest first)
    for (uint32_t i = 0; i < ctx->num_inlines - 1; i++) {
        for (uint32_t j = i + 1; j < ctx->num_inlines; j++) {
            if (ctx->inlines[j].benefit > ctx->inlines[i].benefit) {
                InlineCandidate tmp = ctx->inlines[i];
                ctx->inlines[i] = ctx->inlines[j];
                ctx->inlines[j] = tmp;
            }
        }
    }
    
    // Perform inlining (simplified - real implementation would modify IR)
    for (uint32_t i = 0; i < ctx->num_inlines; i++) {
        if (!ctx->inlines[i].should_inline) continue;
        
        // In real implementation: inline callee into caller
        ctx->inlines_performed++;
    }
    
    printf("    Performed %u inlines\n", ctx->inlines_performed);
}

// Run standard optimizations on a function
static bool optimize_function_standard(FcxIRFunction *func, const HMSOConfig *config) {
    if (!func) return false;
    
    bool changed = false;
    
    // Constant folding
    if (opt_constant_folding(func)) {
        changed = true;
    }
    
    // Dead code elimination
    if (opt_dead_code_elimination(func)) {
        changed = true;
    }
    
    // Loop invariant code motion (if enabled)
    if (config->level >= OPT_LEVEL_O2) {
        if (opt_loop_invariant_code_motion(func)) {
            changed = true;
        }
    }
    
    return changed;
}

// Main chunk optimization function
void hmso_optimize_chunk(OptimizationChunk *chunk, GlobalIndex *idx,
                         const HMSOConfig *config) {
    if (!chunk || !idx || !config) return;
    
    printf("  Optimizing chunk %u (%u functions, hotness=%.2f)\n",
           chunk->id, chunk->num_functions, chunk->hotness_score);
    
    // Create local context
    LocalContext *ctx = create_local_context(chunk, idx);
    if (!ctx) return;
    
    // Load IR for this chunk
    if (!load_chunk_ir(ctx)) {
        destroy_local_context(ctx);
        return;
    }
    
    // === Interprocedural optimizations within chunk ===
    
    // Aggressive inlining (we have full context)
    if (config->inline_threshold > 0) {
        perform_inlining(ctx, config->inline_threshold);
    }
    
    // Interprocedural constant propagation
    // (simplified - would propagate constants across function boundaries)
    
    // Dead code elimination with knowledge of external uses
    // (simplified - would use reference_map from global index)
    
    // === Intraprocedural optimizations ===
    
    if (ctx->ir) {
        for (uint32_t f = 0; f < ctx->ir->function_count; f++) {
            FcxIRFunction *func = &ctx->ir->functions[f];
            
            // Standard optimizations
            optimize_function_standard(func, config);
            
            // Expensive optimizations for hot functions
            if (config->enable_expensive_opts && chunk->hotness_score > 0.5) {
                // Polyhedral loop optimization
                // (placeholder - would use polyhedral model)
                
                // Superoptimization for tiny hot functions
                if (func->block_count == 1 && 
                    func->blocks[0].instruction_count < 20) {
                    // (placeholder - would use exhaustive search)
                }
            }
        }
    }
    
    // === Memory and vectorization ===
    
    // Alias analysis across chunk
    // (placeholder - would build alias sets)
    
    if (config->vectorize) {
        // Vectorize loops in chunk
        // (placeholder - would use SLP vectorizer)
    }
    
    // Update statistics
    ctx->instructions_after = ctx->instructions_before;  // Placeholder
    
    // Store optimized IR in chunk
    chunk->optimized_ir = ctx->ir;
    chunk->optimized = true;
    
    // Don't destroy IR - it's now owned by chunk
    ctx->ir = NULL;
    destroy_local_context(ctx);
}

// ============================================================================
// Parallel Chunk Optimization
// ============================================================================

typedef struct {
    OptimizationChunk *chunk;
    GlobalIndex *idx;
    HMSOConfig config;
    uint32_t thread_id;
} ChunkOptJob;

static void *chunk_opt_thread(void *arg) {
    ChunkOptJob *job = (ChunkOptJob *)arg;
    
    hmso_optimize_chunk(job->chunk, job->idx, &job->config);
    
    return NULL;
}

void hmso_optimize_all_chunks_parallel(HMSOContext *ctx) {
    if (!ctx || !ctx->chunks || ctx->num_chunks == 0) return;
    
    printf("HMSO: Optimizing %u chunks with %u threads...\n", 
           ctx->num_chunks, ctx->num_threads);
    
    // Sort chunks by priority (hot first)
    for (uint32_t i = 0; i < ctx->num_chunks - 1; i++) {
        for (uint32_t j = i + 1; j < ctx->num_chunks; j++) {
            if (ctx->chunks[j]->hotness_score > ctx->chunks[i]->hotness_score) {
                OptimizationChunk *tmp = ctx->chunks[i];
                ctx->chunks[i] = ctx->chunks[j];
                ctx->chunks[j] = tmp;
            }
        }
    }
    
    if (ctx->num_threads <= 1) {
        // Single-threaded optimization
        for (uint32_t i = 0; i < ctx->num_chunks; i++) {
            hmso_optimize_chunk(ctx->chunks[i], ctx->global_index, &ctx->config);
            ctx->stats.functions_optimized += ctx->chunks[i]->num_functions;
        }
    } else {
        // Multi-threaded optimization
        pthread_t *threads = (pthread_t *)malloc(ctx->num_threads * sizeof(pthread_t));
        ChunkOptJob *jobs = (ChunkOptJob *)malloc(ctx->num_chunks * sizeof(ChunkOptJob));
        
        if (!threads || !jobs) {
            free(threads);
            free(jobs);
            return;
        }
        
        uint32_t chunks_submitted = 0;
        uint32_t chunks_completed = 0;
        
        while (chunks_completed < ctx->num_chunks) {
            // Submit jobs up to thread limit
            uint32_t active_threads = 0;
            
            while (active_threads < ctx->num_threads && 
                   chunks_submitted < ctx->num_chunks) {
                jobs[chunks_submitted].chunk = ctx->chunks[chunks_submitted];
                jobs[chunks_submitted].idx = ctx->global_index;
                jobs[chunks_submitted].config = ctx->config;
                jobs[chunks_submitted].thread_id = active_threads;
                
                pthread_create(&threads[active_threads], NULL, 
                              chunk_opt_thread, &jobs[chunks_submitted]);
                
                chunks_submitted++;
                active_threads++;
            }
            
            // Wait for all active threads
            for (uint32_t t = 0; t < active_threads; t++) {
                pthread_join(threads[t], NULL);
                chunks_completed++;
                ctx->stats.functions_optimized += ctx->chunks[chunks_completed - 1]->num_functions;
            }
        }
        
        free(threads);
        free(jobs);
    }
    
    printf("HMSO: Chunk optimization complete\n");
}

// ============================================================================
// Cross-Chunk Optimization (Stage 4)
// ============================================================================

typedef struct {
    uint32_t caller_chunk;
    uint32_t callee_chunk;
    uint32_t caller_func;
    uint32_t callee_func;
    int32_t benefit;
} CrossChunkOpportunity;

static CrossChunkOpportunity *find_cross_chunk_opportunities(HMSOContext *ctx,
                                                              uint32_t *out_count) {
    if (!ctx || !ctx->global_index || !ctx->global_index->call_graph) {
        *out_count = 0;
        return NULL;
    }
    
    CallGraph *cg = ctx->global_index->call_graph;
    
    // Build chunk membership map
    uint32_t *func_to_chunk = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    if (!func_to_chunk) {
        *out_count = 0;
        return NULL;
    }
    
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        func_to_chunk[i] = UINT32_MAX;
    }
    
    for (uint32_t c = 0; c < ctx->num_chunks; c++) {
        for (uint32_t f = 0; f < ctx->chunks[c]->num_functions; f++) {
            uint32_t func_idx = ctx->chunks[c]->function_indices[f];
            if (func_idx < cg->num_nodes) {
                func_to_chunk[func_idx] = c;
            }
        }
    }
    
    // Find cross-chunk call edges
    uint32_t capacity = 64;
    CrossChunkOpportunity *opps = (CrossChunkOpportunity *)malloc(
        capacity * sizeof(CrossChunkOpportunity));
    uint32_t count = 0;
    
    if (!opps) {
        free(func_to_chunk);
        *out_count = 0;
        return NULL;
    }
    
    for (uint32_t e = 0; e < cg->num_edges; e++) {
        uint32_t caller = cg->edges[e].caller_idx;
        uint32_t callee = cg->edges[e].callee_idx;
        
        uint32_t caller_chunk = func_to_chunk[caller];
        uint32_t callee_chunk = func_to_chunk[callee];
        
        // Skip if same chunk or unassigned
        if (caller_chunk == callee_chunk) continue;
        if (caller_chunk == UINT32_MAX || callee_chunk == UINT32_MAX) continue;
        
        // Check if this is a hot edge worth optimizing
        if (!cg->edges[e].is_hot && cg->edges[e].call_count < 10) continue;
        
        // Grow array if needed
        if (count >= capacity) {
            capacity *= 2;
            CrossChunkOpportunity *new_opps = (CrossChunkOpportunity *)realloc(
                opps, capacity * sizeof(CrossChunkOpportunity));
            if (!new_opps) break;
            opps = new_opps;
        }
        
        opps[count].caller_chunk = caller_chunk;
        opps[count].callee_chunk = callee_chunk;
        opps[count].caller_func = caller;
        opps[count].callee_func = callee;
        opps[count].benefit = cg->edges[e].call_count * 10;  // Simple heuristic
        count++;
    }
    
    free(func_to_chunk);
    *out_count = count;
    return opps;
}

void hmso_optimize_cross_chunk(HMSOContext *ctx) {
    if (!ctx) return;
    
    printf("HMSO: Cross-chunk optimization...\n");
    
    // Find cross-chunk optimization opportunities
    uint32_t num_opps = 0;
    CrossChunkOpportunity *opps = find_cross_chunk_opportunities(ctx, &num_opps);
    
    if (!opps || num_opps == 0) {
        printf("  No cross-chunk opportunities found\n");
        free(opps);
        return;
    }
    
    printf("  Found %u cross-chunk call edges\n", num_opps);
    
    // Sort by benefit
    for (uint32_t i = 0; i < num_opps - 1; i++) {
        for (uint32_t j = i + 1; j < num_opps; j++) {
            if (opps[j].benefit > opps[i].benefit) {
                CrossChunkOpportunity tmp = opps[i];
                opps[i] = opps[j];
                opps[j] = tmp;
            }
        }
    }
    
    // Process top opportunities
    uint32_t processed = 0;
    for (uint32_t i = 0; i < num_opps && processed < 10; i++) {
        CrossChunkOpportunity *opp = &opps[i];
        
        // Check if inlining across chunks is beneficial
        if (opp->benefit > 50) {
            printf("  Cross-chunk inline opportunity: chunk %u -> chunk %u (benefit=%d)\n",
                   opp->caller_chunk, opp->callee_chunk, opp->benefit);
            
            // In real implementation: merge chunks or inline across boundary
            processed++;
        }
    }
    
    // Global code layout optimization
    // (placeholder - would reorder functions for cache locality)
    
    free(opps);
    printf("HMSO: Cross-chunk optimization complete\n");
}
