/**
 * FCx HMSO - Iterative Refinement (Stage 5) and Final Link (Stage 6)
 * Uses LLVM backend for code generation and linking
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include "../codegen/llvm_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

// ============================================================================
// Iterative Refinement (Stage 5)
// ============================================================================

// Estimate performance score for current optimization state
static double estimate_performance(HMSOContext *ctx) {
    if (!ctx || !ctx->chunks) return 0.0;
    
    double score = 0.0;
    
    for (uint32_t i = 0; i < ctx->num_chunks; i++) {
        OptimizationChunk *chunk = ctx->chunks[i];
        if (!chunk) continue;
        
        // Higher score for smaller hot code
        double chunk_score = 1.0;
        
        // Penalize large instruction counts
        if (chunk->total_instructions > 0) {
            chunk_score = 10000.0 / (double)chunk->total_instructions;
        }
        
        // Bonus for hot chunks being well-optimized
        chunk_score *= (1.0 + chunk->hotness_score);
        
        // Bonus for optimized chunks
        if (chunk->optimized) {
            chunk_score *= 1.5;
        }
        
        score += chunk_score;
    }
    
    return score;
}

// Re-partition based on optimization results
static OptimizationChunk **repartition_based_on_results(HMSOContext *ctx,
                                                         uint32_t *out_count) {
    if (!ctx || !ctx->global_index) {
        *out_count = 0;
        return NULL;
    }
    
    // Analyze current chunks for improvement opportunities
    // - Merge small chunks that are frequently called together
    // - Split chunks that have mixed hot/cold code
    
    // For now, just return existing partitioning
    // Real implementation would analyze call patterns and re-partition
    
    *out_count = ctx->num_chunks;
    return ctx->chunks;
}

void hmso_iterative_optimize(HMSOContext *ctx, uint32_t max_iterations) {
    if (!ctx || max_iterations == 0) return;
    
    printf("HMSO: Starting iterative refinement (max %u iterations)...\n", max_iterations);
    
    double prev_score = 0.0;
    
    for (uint32_t iter = 0; iter < max_iterations; iter++) {
        printf("\n=== Iteration %u ===\n", iter + 1);
        
        // Re-partition based on what we learned
        uint32_t new_count = 0;
        OptimizationChunk **new_chunks = repartition_based_on_results(ctx, &new_count);
        
        if (new_chunks && new_chunks != ctx->chunks) {
            // Free old chunks if different
            // (simplified - real implementation would handle this properly)
            ctx->chunks = new_chunks;
            ctx->num_chunks = new_count;
        }
        
        // Re-optimize with new partitioning
        hmso_optimize_all_chunks_parallel(ctx);
        hmso_optimize_cross_chunk(ctx);
        
        // Score the result
        double score = estimate_performance(ctx);
        printf("  Performance score: %.2f (previous: %.2f)\n", score, prev_score);
        
        // Check for convergence
        if (iter > 0 && fabs(score - prev_score) < ctx->config.convergence_threshold) {
            printf("  Converged after %u iterations\n", iter + 1);
            break;
        }
        
        prev_score = score;
    }
    
    printf("HMSO: Iterative refinement complete\n");
}

// ============================================================================
// Final Link and Layout (Stage 6)
// ============================================================================

// Section types for code layout
typedef enum {
    SECTION_HOT_TEXT,
    SECTION_COLD_TEXT,
    SECTION_STARTUP_TEXT,
    SECTION_HOT_DATA,
    SECTION_COLD_DATA,
    SECTION_RODATA,
    SECTION_COUNT
} SectionType;

// Function placement info
typedef struct {
    uint32_t func_idx;
    SectionType section;
    uint64_t offset;
    uint32_t size;
} FunctionPlacement;

// Assign functions to sections based on profile/hotness
static FunctionPlacement *assign_functions_to_sections(HMSOContext *ctx,
                                                        uint32_t *out_count) {
    if (!ctx || !ctx->global_index || !ctx->global_index->call_graph) {
        *out_count = 0;
        return NULL;
    }
    
    CallGraph *cg = ctx->global_index->call_graph;
    
    FunctionPlacement *placements = (FunctionPlacement *)calloc(
        cg->num_nodes, sizeof(FunctionPlacement));
    if (!placements) {
        *out_count = 0;
        return NULL;
    }
    
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        if (!cg->nodes[i].is_reachable) continue;
        
        FunctionPlacement *p = &placements[count++];
        p->func_idx = i;
        p->offset = 0;  // Will be assigned during layout
        p->size = 0;    // Will be calculated from IR
        
        // Determine section based on function characteristics
        const char *name = cg->nodes[i].name;
        
        // Check for startup functions
        if (name && (strcmp(name, "_start") == 0 ||
                     strcmp(name, "main") == 0 ||
                     strcmp(name, "_init") == 0 ||
                     strncmp(name, "__init_", 7) == 0)) {
            p->section = SECTION_STARTUP_TEXT;
            continue;
        }
        
        // Check hotness from chunk assignment
        bool is_hot = false;
        for (uint32_t c = 0; c < ctx->num_chunks; c++) {
            OptimizationChunk *chunk = ctx->chunks[c];
            if (!chunk) continue;
            
            for (uint32_t f = 0; f < chunk->num_functions; f++) {
                if (chunk->function_indices[f] == i) {
                    is_hot = chunk->hotness_score > 0.5;
                    break;
                }
            }
            if (is_hot) break;
        }
        
        p->section = is_hot ? SECTION_HOT_TEXT : SECTION_COLD_TEXT;
    }
    
    *out_count = count;
    return placements;
}

// Order functions within sections for cache locality
static void order_functions_by_call_graph(FunctionPlacement *placements,
                                          uint32_t count,
                                          CallGraph *cg) {
    if (!placements || count == 0 || !cg) return;
    
    // Group by section first
    for (uint32_t s = 0; s < SECTION_COUNT; s++) {
        // Find functions in this section
        uint32_t *section_funcs = (uint32_t *)malloc(count * sizeof(uint32_t));
        uint32_t section_count = 0;
        
        if (!section_funcs) continue;
        
        for (uint32_t i = 0; i < count; i++) {
            if (placements[i].section == s) {
                section_funcs[section_count++] = i;
            }
        }
        
        if (section_count <= 1) {
            free(section_funcs);
            continue;
        }
        
        // Simple greedy ordering: place frequently called together functions nearby
        // Start with most-called function
        uint32_t best_start = 0;
        uint32_t best_callers = 0;
        
        for (uint32_t i = 0; i < section_count; i++) {
            uint32_t func_idx = placements[section_funcs[i]].func_idx;
            if (cg->nodes[func_idx].num_callers > best_callers) {
                best_callers = cg->nodes[func_idx].num_callers;
                best_start = i;
            }
        }
        
        // Swap to front
        if (best_start != 0) {
            uint32_t tmp = section_funcs[0];
            section_funcs[0] = section_funcs[best_start];
            section_funcs[best_start] = tmp;
        }
        
        // Greedy: for each position, pick function most related to previous
        for (uint32_t pos = 1; pos < section_count; pos++) {
            uint32_t prev_func = placements[section_funcs[pos - 1]].func_idx;
            uint32_t best_next = pos;
            uint32_t best_score = 0;
            
            for (uint32_t i = pos; i < section_count; i++) {
                uint32_t func_idx = placements[section_funcs[i]].func_idx;
                
                // Score based on call relationship
                uint32_t score = 0;
                for (uint32_t e = 0; e < cg->num_edges; e++) {
                    if ((cg->edges[e].caller_idx == prev_func && 
                         cg->edges[e].callee_idx == func_idx) ||
                        (cg->edges[e].caller_idx == func_idx && 
                         cg->edges[e].callee_idx == prev_func)) {
                        score += cg->edges[e].call_count;
                    }
                }
                
                if (score > best_score) {
                    best_score = score;
                    best_next = i;
                }
            }
            
            // Swap to current position
            if (best_next != pos) {
                uint32_t tmp = section_funcs[pos];
                section_funcs[pos] = section_funcs[best_next];
                section_funcs[best_next] = tmp;
            }
        }
        
        free(section_funcs);
    }
}

// Write optimized binary using LLVM backend
bool hmso_final_link(HMSOContext *ctx, const char *output_path) {
    if (!ctx || !output_path) return false;
    
    printf("HMSO: Final link to %s using LLVM backend...\n", output_path);
    
    // Assign functions to sections for analysis
    uint32_t num_placements = 0;
    FunctionPlacement *placements = assign_functions_to_sections(ctx, &num_placements);
    
    if (!placements || num_placements == 0) {
        printf("HMSO: No functions to link\n");
        free(placements);
        return false;
    }
    
    printf("  Assigned %u functions to sections\n", num_placements);
    
    // Count per section for statistics
    uint32_t section_counts[SECTION_COUNT] = {0};
    for (uint32_t i = 0; i < num_placements; i++) {
        section_counts[placements[i].section]++;
    }
    
    printf("  Hot text: %u functions\n", section_counts[SECTION_HOT_TEXT]);
    printf("  Cold text: %u functions\n", section_counts[SECTION_COLD_TEXT]);
    printf("  Startup: %u functions\n", section_counts[SECTION_STARTUP_TEXT]);
    
    // Order functions within sections for cache locality
    if (ctx->global_index && ctx->global_index->call_graph) {
        order_functions_by_call_graph(placements, num_placements, 
                                      ctx->global_index->call_graph);
    }
    
    // Create LLVM backend with appropriate optimization config
    LLVMBackendConfig config = llvm_default_config();
    if (ctx->config.lto_iterations > 1) {
        config = llvm_release_config();
    }
    
    LLVMBackend *backend = llvm_backend_create(NULL, &config);
    if (!backend) {
        fprintf(stderr, "HMSO: Failed to create LLVM backend\n");
        free(placements);
        return false;
    }
    
    // Load and emit IR from compilation units
    bool success = true;
    if (ctx->global_index) {
        for (uint32_t i = 0; i < ctx->global_index->num_units && success; i++) {
            CompilationUnit *unit = &ctx->global_index->units[i];
            if (!unit || !unit->ir_loaded || !unit->ir_data) continue;
            
            // Cast ir_data to FcIRModule if available
            FcIRModule *fc_module = (FcIRModule *)unit->ir_data;
            if (fc_module) {
                success = llvm_emit_module(backend, fc_module);
                if (!success) {
                    fprintf(stderr, "HMSO: Failed to emit unit %u: %s\n", 
                            i, llvm_backend_get_error(backend));
                }
            }
        }
    }
    
    // Generate executable using LLVM
    if (success) {
        success = llvm_compile_and_link(backend, output_path);
        if (!success) {
            fprintf(stderr, "HMSO: Failed to link: %s\n", 
                    llvm_backend_get_error(backend));
        }
    }
    
    // Print LLVM statistics
    if (success) {
        llvm_print_statistics(backend);
    }
    
    // Cleanup
    llvm_backend_destroy(backend);
    free(placements);
    
    if (success) {
        printf("HMSO: Successfully wrote %s\n", output_path);
    } else {
        printf("HMSO: Failed to write %s\n", output_path);
    }
    
    return success;
}

// ============================================================================
// High-Level API
// ============================================================================

bool hmso_optimize_program(const char **source_files, uint32_t count,
                           const char *output_path, const HMSOConfig *config) {
    if (!source_files || count == 0 || !output_path) return false;
    
    clock_t start_time = clock();
    
    printf("HMSO: Optimizing %u source files...\n", count);
    
    // Create context
    HMSOContext *ctx = hmso_create(config);
    if (!ctx) {
        fprintf(stderr, "HMSO: Failed to create context\n");
        return false;
    }
    
    // Stage 0: Compile each file (would be done externally in real implementation)
    // For now, assume .fcx.o files already exist
    
    // Stage 1: Build global index
    ctx->global_index = hmso_build_global_index(source_files, count);
    if (!ctx->global_index) {
        fprintf(stderr, "HMSO: Failed to build global index\n");
        hmso_destroy(ctx);
        return false;
    }
    
    // Load profile if available
    ProfileData *profile = NULL;
    if (ctx->config.use_profile && ctx->config.profile_path) {
        profile = hmso_load_profile(ctx->config.profile_path);
    }
    
    // Stage 2: Partition program
    ctx->chunks = hmso_partition_program(ctx->global_index, profile, &ctx->num_chunks);
    if (!ctx->chunks || ctx->num_chunks == 0) {
        fprintf(stderr, "HMSO: Failed to partition program\n");
        hmso_free_profile(profile);
        hmso_destroy(ctx);
        return false;
    }
    
    // Stage 3: Parallel chunk optimization
    hmso_optimize_all_chunks_parallel(ctx);
    
    // Stage 4: Cross-chunk optimization
    if (ctx->config.enable_lto) {
        hmso_optimize_cross_chunk(ctx);
    }
    
    // Stage 5: Iterative refinement (for O3 and above)
    if (ctx->config.lto_iterations > 1) {
        hmso_iterative_optimize(ctx, ctx->config.lto_iterations);
    }
    
    // Stage 6: Final link
    bool success = hmso_final_link(ctx, output_path);
    
    // Calculate statistics
    clock_t end_time = clock();
    ctx->stats.total_time_ms = (double)(end_time - start_time) * 1000.0 / CLOCKS_PER_SEC;
    
    // Print statistics
    hmso_print_stats(ctx);
    
    // Cleanup
    hmso_free_profile(profile);
    hmso_destroy(ctx);
    
    return success;
}

void hmso_print_stats(const HMSOContext *ctx) {
    if (!ctx) return;
    
    printf("\n=== HMSO Statistics ===\n");
    printf("Functions optimized: %lu\n", ctx->stats.functions_optimized);
    printf("Instructions before: %lu\n", ctx->stats.instructions_before);
    printf("Instructions after:  %lu\n", ctx->stats.instructions_after);
    printf("Inlines performed:   %lu\n", ctx->stats.inlines_performed);
    printf("Dead code removed:   %lu\n", ctx->stats.dead_code_removed);
    printf("Total time:          %.2f ms\n", ctx->stats.total_time_ms);
    
    if (ctx->stats.instructions_before > 0) {
        double reduction = 100.0 * (1.0 - (double)ctx->stats.instructions_after / 
                                          (double)ctx->stats.instructions_before);
        printf("Code size reduction: %.1f%%\n", reduction);
    }
}

// ============================================================================
// Profile Support
// ============================================================================

ProfileData *hmso_load_profile(const char *profile_path) {
    if (!profile_path) return NULL;
    
    FILE *f = fopen(profile_path, "rb");
    if (!f) {
        fprintf(stderr, "HMSO: Cannot open profile: %s\n", profile_path);
        return NULL;
    }
    
    ProfileData *profile = (ProfileData *)calloc(1, sizeof(ProfileData));
    if (!profile) {
        fclose(f);
        return NULL;
    }
    
    // Read profile header
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "FCXP", 4) != 0) {
        fprintf(stderr, "HMSO: Invalid profile format\n");
        free(profile);
        fclose(f);
        return NULL;
    }
    
    // Read execution count
    fread(&profile->execution_count, sizeof(uint64_t), 1, f);
    
    // Read block counts
    fread(&profile->num_blocks, sizeof(uint32_t), 1, f);
    if (profile->num_blocks > 0) {
        profile->block_counts = (uint64_t *)malloc(profile->num_blocks * sizeof(uint64_t));
        if (profile->block_counts) {
            fread(profile->block_counts, sizeof(uint64_t), profile->num_blocks, f);
        }
    }
    
    // Read branch probabilities
    fread(&profile->num_branches, sizeof(uint32_t), 1, f);
    if (profile->num_branches > 0) {
        profile->branch_probs = (double *)malloc(profile->num_branches * sizeof(double));
        if (profile->branch_probs) {
            fread(profile->branch_probs, sizeof(double), profile->num_branches, f);
        }
    }
    
    fclose(f);
    
    printf("HMSO: Loaded profile with %lu executions, %u blocks, %u branches\n",
           profile->execution_count, profile->num_blocks, profile->num_branches);
    
    return profile;
}

void hmso_free_profile(ProfileData *profile) {
    if (!profile) return;
    
    free(profile->block_counts);
    free(profile->branch_probs);
    free(profile);
}

bool hmso_merge_profiles(const char **profile_paths, uint32_t count,
                         const char *output_path) {
    if (!profile_paths || count == 0 || !output_path) return false;
    
    printf("HMSO: Merging %u profiles...\n", count);
    
    // Load first profile as base
    ProfileData *merged = hmso_load_profile(profile_paths[0]);
    if (!merged) return false;
    
    // Merge remaining profiles
    for (uint32_t i = 1; i < count; i++) {
        ProfileData *p = hmso_load_profile(profile_paths[i]);
        if (!p) continue;
        
        merged->execution_count += p->execution_count;
        
        // Merge block counts (assuming same structure)
        if (p->num_blocks == merged->num_blocks && p->block_counts && merged->block_counts) {
            for (uint32_t b = 0; b < merged->num_blocks; b++) {
                merged->block_counts[b] += p->block_counts[b];
            }
        }
        
        // Merge branch probabilities (weighted average)
        if (p->num_branches == merged->num_branches && p->branch_probs && merged->branch_probs) {
            for (uint32_t b = 0; b < merged->num_branches; b++) {
                merged->branch_probs[b] = (merged->branch_probs[b] + p->branch_probs[b]) / 2.0;
            }
        }
        
        hmso_free_profile(p);
    }
    
    // Write merged profile
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        hmso_free_profile(merged);
        return false;
    }
    
    fwrite("FCXP", 1, 4, f);
    fwrite(&merged->execution_count, sizeof(uint64_t), 1, f);
    fwrite(&merged->num_blocks, sizeof(uint32_t), 1, f);
    if (merged->block_counts) {
        fwrite(merged->block_counts, sizeof(uint64_t), merged->num_blocks, f);
    }
    fwrite(&merged->num_branches, sizeof(uint32_t), 1, f);
    if (merged->branch_probs) {
        fwrite(merged->branch_probs, sizeof(double), merged->num_branches, f);
    }
    
    fclose(f);
    hmso_free_profile(merged);
    
    printf("HMSO: Merged profile written to %s\n", output_path);
    return true;
}
