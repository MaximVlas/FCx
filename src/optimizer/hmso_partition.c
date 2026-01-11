/**
 * FCx HMSO - Program Partitioning (Stage 2)
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// Partitioning Helpers
// ============================================================================

// Create a new optimization chunk
static OptimizationChunk *create_chunk(uint32_t id) {
    OptimizationChunk *chunk = (OptimizationChunk *)calloc(1, sizeof(OptimizationChunk));
    if (!chunk) return NULL;
    
    chunk->id = id;
    chunk->function_indices = NULL;
    chunk->num_functions = 0;
    chunk->total_instructions = 0;
    chunk->hotness_score = 0.0;
    chunk->opt_level = OPT_LEVEL_O2;
    chunk->enable_expensive_opts = false;
    chunk->optimized = false;
    chunk->optimized_ir = NULL;
    
    return chunk;
}

// Add function to chunk
static bool add_function_to_chunk(OptimizationChunk *chunk, uint32_t func_idx,
                                  uint32_t instruction_count, double hotness) {
    if (!chunk) return false;
    
    uint32_t *new_indices = (uint32_t *)realloc(chunk->function_indices,
                                                 (chunk->num_functions + 1) * sizeof(uint32_t));
    if (!new_indices) return false;
    
    chunk->function_indices = new_indices;
    chunk->function_indices[chunk->num_functions++] = func_idx;
    chunk->total_instructions += instruction_count;
    chunk->hotness_score = fmax(chunk->hotness_score, hotness);
    
    return true;
}

// Get function summary from global index
static FunctionSummary *get_function_summary(GlobalIndex *idx, uint32_t func_idx) {
    if (!idx || !idx->call_graph || func_idx >= idx->call_graph->num_nodes) {
        return NULL;
    }
    
    uint32_t unit_idx = idx->call_graph->nodes[func_idx].unit_idx;
    uint32_t local_idx = idx->call_graph->nodes[func_idx].func_idx;
    
    if (unit_idx >= idx->num_units || !idx->units[unit_idx].summary) {
        return NULL;
    }
    
    CompilationSummary *sum = idx->units[unit_idx].summary;
    if (local_idx >= sum->num_functions) {
        return NULL;
    }
    
    return &sum->functions[local_idx];
}

// ============================================================================
// Call-Graph Based Partitioning (No Profile)
// ============================================================================

// Merge small SCCs into larger chunks
static OptimizationChunk **merge_small_sccs(GlobalIndex *idx, 
                                            uint32_t min_chunk_size,
                                            uint32_t *out_count) {
    if (!idx || !idx->call_graph) {
        *out_count = 0;
        return NULL;
    }
    
    CallGraph *cg = idx->call_graph;
    
    // Find max SCC ID
    uint32_t max_scc = 0;
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        if (cg->nodes[i].scc_id != UINT32_MAX && cg->nodes[i].scc_id > max_scc) {
            max_scc = cg->nodes[i].scc_id;
        }
    }
    
    if (max_scc == 0) {
        // No SCCs found, create one chunk per function
        *out_count = 0;
        return NULL;
    }
    
    // Count functions per SCC
    uint32_t *scc_sizes = (uint32_t *)calloc(max_scc + 1, sizeof(uint32_t));
    uint32_t *scc_instructions = (uint32_t *)calloc(max_scc + 1, sizeof(uint32_t));
    
    if (!scc_sizes || !scc_instructions) {
        free(scc_sizes);
        free(scc_instructions);
        *out_count = 0;
        return NULL;
    }
    
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        if (cg->nodes[i].scc_id != UINT32_MAX) {
            scc_sizes[cg->nodes[i].scc_id]++;
            
            FunctionSummary *sum = get_function_summary(idx, i);
            if (sum) {
                scc_instructions[cg->nodes[i].scc_id] += sum->instruction_count;
            }
        }
    }
    
    // Create chunks from SCCs, merging small ones
    uint32_t chunk_capacity = 64;
    OptimizationChunk **chunks = (OptimizationChunk **)calloc(chunk_capacity, 
                                                               sizeof(OptimizationChunk *));
    if (!chunks) {
        free(scc_sizes);
        free(scc_instructions);
        *out_count = 0;
        return NULL;
    }
    
    uint32_t num_chunks = 0;
    OptimizationChunk *current_chunk = NULL;
    
    // Process SCCs in order
    for (uint32_t scc = 0; scc <= max_scc; scc++) {
        if (scc_sizes[scc] == 0) continue;
        
        // Start new chunk if needed
        if (!current_chunk || current_chunk->num_functions >= min_chunk_size) {
            if (current_chunk) {
                chunks[num_chunks++] = current_chunk;
                
                // Grow array if needed
                if (num_chunks >= chunk_capacity) {
                    chunk_capacity *= 2;
                    OptimizationChunk **new_chunks = (OptimizationChunk **)realloc(
                        chunks, chunk_capacity * sizeof(OptimizationChunk *));
                    if (!new_chunks) break;
                    chunks = new_chunks;
                }
            }
            current_chunk = create_chunk(num_chunks);
            if (!current_chunk) break;
        }
        
        // Add all functions from this SCC to current chunk
        for (uint32_t i = 0; i < cg->num_nodes; i++) {
            if (cg->nodes[i].scc_id == scc) {
                FunctionSummary *sum = get_function_summary(idx, i);
                uint32_t instr_count = sum ? sum->instruction_count : 0;
                add_function_to_chunk(current_chunk, i, instr_count, 0.0);
            }
        }
    }
    
    // Add final chunk
    if (current_chunk && current_chunk->num_functions > 0) {
        chunks[num_chunks++] = current_chunk;
    }
    
    free(scc_sizes);
    free(scc_instructions);
    
    *out_count = num_chunks;
    return chunks;
}

// Split large chunks for parallelism
static void split_large_chunks(OptimizationChunk ***chunks_ptr, uint32_t *count,
                               uint32_t max_chunk_size) {
    if (!chunks_ptr || !*chunks_ptr || *count == 0) return;
    
    OptimizationChunk **chunks = *chunks_ptr;
    uint32_t num_chunks = *count;
    
    // Count how many chunks need splitting
    uint32_t splits_needed = 0;
    for (uint32_t i = 0; i < num_chunks; i++) {
        if (chunks[i] && chunks[i]->num_functions > max_chunk_size) {
            splits_needed += (chunks[i]->num_functions / max_chunk_size);
        }
    }
    
    if (splits_needed == 0) return;
    
    // Allocate new array
    uint32_t new_capacity = num_chunks + splits_needed;
    OptimizationChunk **new_chunks = (OptimizationChunk **)calloc(new_capacity,
                                                                   sizeof(OptimizationChunk *));
    if (!new_chunks) return;
    
    uint32_t new_count = 0;
    
    for (uint32_t i = 0; i < num_chunks; i++) {
        OptimizationChunk *chunk = chunks[i];
        if (!chunk) continue;
        
        if (chunk->num_functions <= max_chunk_size) {
            // Keep as-is
            new_chunks[new_count++] = chunk;
        } else {
            // Split into smaller chunks
            uint32_t funcs_remaining = chunk->num_functions;
            uint32_t func_offset = 0;
            
            while (funcs_remaining > 0) {
                uint32_t funcs_in_split = (funcs_remaining > max_chunk_size) ? 
                                          max_chunk_size : funcs_remaining;
                
                OptimizationChunk *split = create_chunk(new_count);
                if (!split) break;
                
                split->function_indices = (uint32_t *)malloc(funcs_in_split * sizeof(uint32_t));
                if (!split->function_indices) {
                    free(split);
                    break;
                }
                
                memcpy(split->function_indices, 
                       chunk->function_indices + func_offset,
                       funcs_in_split * sizeof(uint32_t));
                split->num_functions = funcs_in_split;
                split->hotness_score = chunk->hotness_score;
                
                new_chunks[new_count++] = split;
                
                func_offset += funcs_in_split;
                funcs_remaining -= funcs_in_split;
            }
            
            // Free original chunk
            free(chunk->function_indices);
            free(chunk);
        }
    }
    
    free(chunks);
    *chunks_ptr = new_chunks;
    *count = new_count;
}

OptimizationChunk **hmso_call_graph_partition(GlobalIndex *idx, uint32_t *out_count) {
    if (!idx) {
        *out_count = 0;
        return NULL;
    }
    
    printf("HMSO: Partitioning using call graph analysis...\n");
    
    // Use SCCs as base chunks
    uint32_t num_chunks = 0;
    OptimizationChunk **chunks = merge_small_sccs(idx, 10, &num_chunks);
    
    if (!chunks || num_chunks == 0) {
        // Fallback: one chunk per function
        printf("HMSO: Fallback to per-function chunks\n");
        
        if (!idx->call_graph) {
            *out_count = 0;
            return NULL;
        }
        
        num_chunks = 0;
        chunks = (OptimizationChunk **)calloc(idx->call_graph->num_nodes,
                                               sizeof(OptimizationChunk *));
        if (!chunks) {
            *out_count = 0;
            return NULL;
        }
        
        for (uint32_t i = 0; i < idx->call_graph->num_nodes; i++) {
            if (!idx->call_graph->nodes[i].is_reachable) continue;
            
            OptimizationChunk *chunk = create_chunk(num_chunks);
            if (!chunk) continue;
            
            FunctionSummary *sum = get_function_summary(idx, i);
            uint32_t instr_count = sum ? sum->instruction_count : 0;
            add_function_to_chunk(chunk, i, instr_count, 0.0);
            
            chunks[num_chunks++] = chunk;
        }
    }
    
    // Split large chunks
    split_large_chunks(&chunks, &num_chunks, 100);
    
    printf("HMSO: Created %u optimization chunks\n", num_chunks);
    
    *out_count = num_chunks;
    return chunks;
}

// ============================================================================
// Profile-Guided Partitioning
// ============================================================================

// Identify hot paths from profile data
static HotPath *identify_hot_paths(ProfileData *profile, CallGraph *cg,
                                   uint32_t *out_count) {
    if (!profile || !cg) {
        *out_count = 0;
        return NULL;
    }
    
    // Find functions with high execution counts
    // uint32_t hot_threshold = 1000;  // TODO: Make configurable
    
    uint32_t *hot_funcs = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    uint32_t num_hot = 0;
    
    if (!hot_funcs) {
        *out_count = 0;
        return NULL;
    }
    
    // This is simplified - real implementation would use actual profile data
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        // Check if function is hot based on profile
        // For now, just mark functions with many callers as hot
        if (cg->nodes[i].num_callers > 3) {
            hot_funcs[num_hot++] = i;
        }
    }
    
    if (num_hot == 0) {
        free(hot_funcs);
        *out_count = 0;
        return NULL;
    }
    
    // Create hot paths by following call chains
    HotPath *paths = (HotPath *)calloc(num_hot, sizeof(HotPath));
    if (!paths) {
        free(hot_funcs);
        *out_count = 0;
        return NULL;
    }
    
    for (uint32_t i = 0; i < num_hot; i++) {
        paths[i].function_indices = (uint32_t *)malloc(10 * sizeof(uint32_t));
        if (!paths[i].function_indices) continue;
        
        paths[i].function_indices[0] = hot_funcs[i];
        paths[i].length = 1;
        paths[i].execution_count = 1000;  // Placeholder
        paths[i].hotness_score = 1.0;
        
        // Follow call edges to build path
        uint32_t current = hot_funcs[i];
        for (uint32_t depth = 0; depth < 9; depth++) {
            // Find most frequent callee
            uint32_t best_callee = UINT32_MAX;
            uint64_t best_count = 0;
            
            for (uint32_t e = 0; e < cg->num_edges; e++) {
                if (cg->edges[e].caller_idx == current) {
                    if (cg->edges[e].dynamic_count > best_count) {
                        best_count = cg->edges[e].dynamic_count;
                        best_callee = cg->edges[e].callee_idx;
                    }
                }
            }
            
            if (best_callee == UINT32_MAX) break;
            
            paths[i].function_indices[paths[i].length++] = best_callee;
            current = best_callee;
        }
    }
    
    free(hot_funcs);
    *out_count = num_hot;
    return paths;
}

OptimizationChunk **hmso_profile_guided_partition(GlobalIndex *idx,
                                                   ProfileData *profile,
                                                   uint32_t *out_count) {
    if (!idx || !profile) {
        return hmso_call_graph_partition(idx, out_count);
    }
    
    printf("HMSO: Profile-guided partitioning...\n");
    
    // Identify hot paths
    uint32_t num_hot_paths = 0;
    HotPath *hot_paths = identify_hot_paths(profile, idx->call_graph, &num_hot_paths);
    
    if (!hot_paths || num_hot_paths == 0) {
        printf("HMSO: No hot paths found, falling back to call graph partitioning\n");
        return hmso_call_graph_partition(idx, out_count);
    }
    
    printf("HMSO: Found %u hot paths\n", num_hot_paths);
    
    // Create chunks from hot paths
    uint32_t chunk_capacity = num_hot_paths + 10;
    OptimizationChunk **chunks = (OptimizationChunk **)calloc(chunk_capacity,
                                                               sizeof(OptimizationChunk *));
    if (!chunks) {
        for (uint32_t i = 0; i < num_hot_paths; i++) {
            free(hot_paths[i].function_indices);
        }
        free(hot_paths);
        *out_count = 0;
        return NULL;
    }
    
    uint32_t num_chunks = 0;
    bool *assigned = (bool *)calloc(idx->call_graph->num_nodes, sizeof(bool));
    
    // Create hot chunks from hot paths
    for (uint32_t p = 0; p < num_hot_paths; p++) {
        OptimizationChunk *chunk = create_chunk(num_chunks);
        if (!chunk) continue;
        
        chunk->opt_level = OPT_LEVEL_O3;
        chunk->enable_expensive_opts = true;
        chunk->hotness_score = hot_paths[p].hotness_score;
        
        for (uint32_t f = 0; f < hot_paths[p].length; f++) {
            uint32_t func_idx = hot_paths[p].function_indices[f];
            if (assigned[func_idx]) continue;
            
            FunctionSummary *sum = get_function_summary(idx, func_idx);
            uint32_t instr_count = sum ? sum->instruction_count : 0;
            add_function_to_chunk(chunk, func_idx, instr_count, hot_paths[p].hotness_score);
            assigned[func_idx] = true;
        }
        
        if (chunk->num_functions > 0) {
            chunks[num_chunks++] = chunk;
        } else {
            free(chunk->function_indices);
            free(chunk);
        }
    }
    
    // Create cold chunk for remaining functions
    OptimizationChunk *cold_chunk = create_chunk(num_chunks);
    if (cold_chunk) {
        cold_chunk->opt_level = OPT_LEVEL_O1;
        cold_chunk->enable_expensive_opts = false;
        cold_chunk->hotness_score = 0.0;
        
        for (uint32_t i = 0; i < idx->call_graph->num_nodes; i++) {
            if (assigned[i]) continue;
            if (!idx->call_graph->nodes[i].is_reachable) continue;
            
            FunctionSummary *sum = get_function_summary(idx, i);
            uint32_t instr_count = sum ? sum->instruction_count : 0;
            add_function_to_chunk(cold_chunk, i, instr_count, 0.0);
        }
        
        if (cold_chunk->num_functions > 0) {
            chunks[num_chunks++] = cold_chunk;
        } else {
            free(cold_chunk->function_indices);
            free(cold_chunk);
        }
    }
    
    // Cleanup
    free(assigned);
    for (uint32_t i = 0; i < num_hot_paths; i++) {
        free(hot_paths[i].function_indices);
    }
    free(hot_paths);
    
    printf("HMSO: Created %u chunks (%u hot, 1 cold)\n", num_chunks, num_chunks - 1);
    
    *out_count = num_chunks;
    return chunks;
}

// Main partitioning entry point
OptimizationChunk **hmso_partition_program(GlobalIndex *idx, ProfileData *profile,
                                           uint32_t *out_count) {
    if (profile) {
        return hmso_profile_guided_partition(idx, profile, out_count);
    } else {
        return hmso_call_graph_partition(idx, out_count);
    }
}
