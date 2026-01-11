/**
 * FCx HMSO - Global Index Construction (Stage 1)
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global Index Construction
// ============================================================================

// Load summary from object file
static CompilationSummary *load_summary_from_object(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "HMSO: Cannot open object file: %s\n", path);
        return NULL;
    }
    
    FCXObjectHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return NULL;
    }
    
    if (header.magic != FCXO_MAGIC) {
        fprintf(stderr, "HMSO: Invalid object file magic: %s\n", path);
        fclose(f);
        return NULL;
    }
    
    if (header.summary_size == 0) {
        fclose(f);
        return NULL;
    }
    
    // Seek to summary section
    fseek(f, header.summary_offset, SEEK_SET);
    
    // Read summary (simplified - real implementation would deserialize properly)
    CompilationSummary *summary = (CompilationSummary *)calloc(1, sizeof(CompilationSummary));
    if (!summary) {
        fclose(f);
        return NULL;
    }
    
    // Read number of functions
    if (fread(&summary->num_functions, sizeof(uint32_t), 1, f) != 1) {
        free(summary);
        fclose(f);
        return NULL;
    }
    
    if (summary->num_functions > 0) {
        summary->functions = (FunctionSummary *)calloc(summary->num_functions, 
                                                        sizeof(FunctionSummary));
        if (!summary->functions) {
            free(summary);
            fclose(f);
            return NULL;
        }
        
        // Read each function summary
        for (uint32_t i = 0; i < summary->num_functions; i++) {
            FunctionSummary *func = &summary->functions[i];
            
            // Read name length and name
            uint32_t name_len;
            if (fread(&name_len, sizeof(uint32_t), 1, f) != 1) break;
            
            func->name = (char *)malloc(name_len + 1);
            if (!func->name) break;
            
            if (fread(func->name, 1, name_len, f) != name_len) {
                free(func->name);
                func->name = NULL;
                break;
            }
            func->name[name_len] = '\0';
            
            // Read metrics
            fread(&func->hash, sizeof(uint64_t), 1, f);
            fread(&func->instruction_count, sizeof(uint32_t), 1, f);
            fread(&func->basic_block_count, sizeof(uint32_t), 1, f);
            fread(&func->cyclomatic_complexity, sizeof(uint32_t), 1, f);
            fread(&func->flags, sizeof(uint32_t), 1, f);
            fread(&func->memory_access, sizeof(uint32_t), 1, f);
            fread(&func->inline_cost, sizeof(uint32_t), 1, f);
            
            // Read call sites
            fread(&func->num_callsites, sizeof(uint32_t), 1, f);
            if (func->num_callsites > 0) {
                func->callsites = (CallSite *)calloc(func->num_callsites, sizeof(CallSite));
                if (func->callsites) {
                    for (uint32_t j = 0; j < func->num_callsites; j++) {
                        uint32_t callee_len;
                        fread(&callee_len, sizeof(uint32_t), 1, f);
                        func->callsites[j].callee_name = (char *)malloc(callee_len + 1);
                        if (func->callsites[j].callee_name) {
                            fread(func->callsites[j].callee_name, 1, callee_len, f);
                            func->callsites[j].callee_name[callee_len] = '\0';
                        }
                        fread(&func->callsites[j].call_count, sizeof(uint32_t), 1, f);
                    }
                }
            }
        }
    }
    
    fclose(f);
    return summary;
}

// Register symbols from a compilation unit
static void register_symbols(GlobalIndex *idx, CompilationSummary *summary, 
                            uint32_t unit_idx) {
    if (!idx || !summary) return;
    
    for (uint32_t i = 0; i < summary->num_functions; i++) {
        FunctionSummary *func = &summary->functions[i];
        if (!func->name) continue;
        
        // Grow symbol table if needed
        if (idx->symbol_table.count >= idx->symbol_table.capacity) {
            uint32_t new_cap = idx->symbol_table.capacity == 0 ? 64 : 
                              idx->symbol_table.capacity * 2;
            char **new_keys = (char **)realloc(idx->symbol_table.keys, 
                                               new_cap * sizeof(char *));
            uint32_t *new_units = (uint32_t *)realloc(idx->symbol_table.unit_indices,
                                                      new_cap * sizeof(uint32_t));
            if (!new_keys || !new_units) return;
            
            idx->symbol_table.keys = new_keys;
            idx->symbol_table.unit_indices = new_units;
            idx->symbol_table.capacity = new_cap;
        }
        
        idx->symbol_table.keys[idx->symbol_table.count] = strdup(func->name);
        idx->symbol_table.unit_indices[idx->symbol_table.count] = unit_idx;
        idx->symbol_table.count++;
    }
}

// Build call graph edges from summaries
static void build_call_edges(GlobalIndex *idx, CompilationSummary *summary,
                            uint32_t unit_idx) {
    if (!idx || !summary || !idx->call_graph) return;
    
    (void)unit_idx;  // Used for debugging, suppress warning
    
    CallGraph *cg = idx->call_graph;
    
    for (uint32_t i = 0; i < summary->num_functions; i++) {
        FunctionSummary *func = &summary->functions[i];
        
        // Find caller node
        uint32_t caller_node = UINT32_MAX;
        for (uint32_t n = 0; n < cg->num_nodes; n++) {
            if (cg->nodes[n].name && func->name &&
                strcmp(cg->nodes[n].name, func->name) == 0) {
                caller_node = n;
                break;
            }
        }
        
        if (caller_node == UINT32_MAX) continue;
        
        // Add edges for each call site
        for (uint32_t j = 0; j < func->num_callsites; j++) {
            CallSite *site = &func->callsites[j];
            if (!site->callee_name) continue;
            
            // Find callee node
            uint32_t callee_node = UINT32_MAX;
            for (uint32_t n = 0; n < cg->num_nodes; n++) {
                if (cg->nodes[n].name && 
                    strcmp(cg->nodes[n].name, site->callee_name) == 0) {
                    callee_node = n;
                    break;
                }
            }
            
            if (callee_node == UINT32_MAX) continue;
            
            // Add edge
            if (cg->num_edges >= 10000) continue;  // Safety limit
            
            CallEdge *edge = &cg->edges[cg->num_edges++];
            edge->caller_idx = caller_node;
            edge->callee_idx = callee_node;
            edge->call_count = site->call_count;
            edge->dynamic_count = 0;
            edge->is_hot = false;
            
            // Update node adjacency lists
            // (simplified - real implementation would use dynamic arrays)
        }
    }
}

// Resolve cross-module references
static void resolve_references(GlobalIndex *idx) {
    if (!idx) return;
    
    // Build reference map: for each symbol, track which units use it
    for (uint32_t u = 0; u < idx->num_units; u++) {
        CompilationSummary *summary = idx->units[u].summary;
        if (!summary) continue;
        
        for (uint32_t f = 0; f < summary->num_functions; f++) {
            FunctionSummary *func = &summary->functions[f];
            
            for (uint32_t c = 0; c < func->num_callsites; c++) {
                CallSite *site = &func->callsites[c];
                if (!site->callee_name) continue;
                
                // Find which unit defines this symbol
                for (uint32_t s = 0; s < idx->symbol_table.count; s++) {
                    if (idx->symbol_table.keys[s] &&
                        strcmp(idx->symbol_table.keys[s], site->callee_name) == 0) {
                        // Found definition - add to reference map
                        // (simplified implementation)
                        break;
                    }
                }
            }
        }
    }
}

// Tarjan's SCC algorithm for call graph
typedef struct {
    uint32_t *index;
    uint32_t *lowlink;
    bool *on_stack;
    uint32_t *stack;
    uint32_t stack_size;
    uint32_t current_index;
    uint32_t current_scc;
    CallGraph *cg;
} TarjanState;

static void tarjan_strongconnect(TarjanState *state, uint32_t v) {
    state->index[v] = state->current_index;
    state->lowlink[v] = state->current_index;
    state->current_index++;
    state->stack[state->stack_size++] = v;
    state->on_stack[v] = true;
    
    // For each edge from v
    for (uint32_t e = 0; e < state->cg->num_edges; e++) {
        if (state->cg->edges[e].caller_idx != v) continue;
        
        uint32_t w = state->cg->edges[e].callee_idx;
        
        if (state->index[w] == UINT32_MAX) {
            tarjan_strongconnect(state, w);
            if (state->lowlink[w] < state->lowlink[v]) {
                state->lowlink[v] = state->lowlink[w];
            }
        } else if (state->on_stack[w]) {
            if (state->index[w] < state->lowlink[v]) {
                state->lowlink[v] = state->index[w];
            }
        }
    }
    
    // If v is a root node, pop the SCC
    if (state->lowlink[v] == state->index[v]) {
        uint32_t w;
        do {
            w = state->stack[--state->stack_size];
            state->on_stack[w] = false;
            state->cg->nodes[w].scc_id = state->current_scc;
        } while (w != v);
        state->current_scc++;
    }
}

static void compute_sccs(CallGraph *cg) {
    if (!cg || cg->num_nodes == 0) return;
    
    TarjanState state;
    state.index = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    state.lowlink = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    state.on_stack = (bool *)calloc(cg->num_nodes, sizeof(bool));
    state.stack = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    state.stack_size = 0;
    state.current_index = 0;
    state.current_scc = 0;
    state.cg = cg;
    
    if (!state.index || !state.lowlink || !state.on_stack || !state.stack) {
        free(state.index);
        free(state.lowlink);
        free(state.on_stack);
        free(state.stack);
        return;
    }
    
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        state.index[i] = UINT32_MAX;
    }
    
    for (uint32_t v = 0; v < cg->num_nodes; v++) {
        if (state.index[v] == UINT32_MAX) {
            tarjan_strongconnect(&state, v);
        }
    }
    
    free(state.index);
    free(state.lowlink);
    free(state.on_stack);
    free(state.stack);
}

// Mark reachable code from entry points
void hmso_mark_live_code(GlobalIndex *idx) {
    if (!idx || !idx->call_graph) return;
    
    CallGraph *cg = idx->call_graph;
    
    // Reset reachability
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        cg->nodes[i].is_reachable = false;
    }
    
    // BFS from entry points
    uint32_t *queue = (uint32_t *)malloc(cg->num_nodes * sizeof(uint32_t));
    if (!queue) return;
    
    uint32_t head = 0, tail = 0;
    
    // Add entry points to queue
    for (uint32_t i = 0; i < idx->num_entry_points; i++) {
        uint32_t ep = idx->entry_points[i];
        if (ep < cg->num_nodes && !cg->nodes[ep].is_reachable) {
            cg->nodes[ep].is_reachable = true;
            queue[tail++] = ep;
        }
    }
    
    // Also mark "main" and "_start" as entry points
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        if (cg->nodes[i].name && 
            (strcmp(cg->nodes[i].name, "main") == 0 ||
             strcmp(cg->nodes[i].name, "_start") == 0)) {
            if (!cg->nodes[i].is_reachable) {
                cg->nodes[i].is_reachable = true;
                queue[tail++] = i;
            }
        }
    }
    
    // BFS
    while (head < tail) {
        uint32_t node = queue[head++];
        
        // Find all callees
        for (uint32_t e = 0; e < cg->num_edges; e++) {
            if (cg->edges[e].caller_idx == node) {
                uint32_t callee = cg->edges[e].callee_idx;
                if (!cg->nodes[callee].is_reachable) {
                    cg->nodes[callee].is_reachable = true;
                    queue[tail++] = callee;
                }
            }
        }
    }
    
    free(queue);
    
    // Count dead functions
    uint32_t dead_count = 0;
    for (uint32_t i = 0; i < cg->num_nodes; i++) {
        if (!cg->nodes[i].is_reachable) {
            dead_count++;
        }
    }
    
    if (dead_count > 0) {
        printf("HMSO: Found %u dead functions (unreachable from entry points)\n", 
               dead_count);
    }
}

// Build call graph from global index
CallGraph *hmso_build_call_graph(GlobalIndex *idx) {
    if (!idx) return NULL;
    
    CallGraph *cg = (CallGraph *)calloc(1, sizeof(CallGraph));
    if (!cg) return NULL;
    
    // Count total functions across all units
    uint32_t total_funcs = 0;
    for (uint32_t u = 0; u < idx->num_units; u++) {
        if (idx->units[u].summary) {
            total_funcs += idx->units[u].summary->num_functions;
        }
    }
    
    if (total_funcs == 0) {
        free(cg);
        return NULL;
    }
    
    // Allocate nodes
    cg->nodes = calloc(total_funcs, sizeof(*cg->nodes));
    cg->edges = calloc(total_funcs * 10, sizeof(CallEdge));  // Estimate
    
    if (!cg->nodes || !cg->edges) {
        free(cg->nodes);
        free(cg->edges);
        free(cg);
        return NULL;
    }
    
    // Populate nodes
    uint32_t node_idx = 0;
    for (uint32_t u = 0; u < idx->num_units; u++) {
        CompilationSummary *summary = idx->units[u].summary;
        if (!summary) continue;
        
        for (uint32_t f = 0; f < summary->num_functions; f++) {
            cg->nodes[node_idx].name = strdup(summary->functions[f].name);
            cg->nodes[node_idx].unit_idx = u;
            cg->nodes[node_idx].func_idx = f;
            cg->nodes[node_idx].scc_id = UINT32_MAX;
            cg->nodes[node_idx].is_reachable = false;
            node_idx++;
        }
    }
    cg->num_nodes = node_idx;
    
    // Build edges
    for (uint32_t u = 0; u < idx->num_units; u++) {
        if (idx->units[u].summary) {
            build_call_edges(idx, idx->units[u].summary, u);
        }
    }
    
    // Compute SCCs
    compute_sccs(cg);
    
    return cg;
}

// Main global index construction
GlobalIndex *hmso_build_global_index(const char **object_files, uint32_t count) {
    if (!object_files || count == 0) return NULL;
    
    printf("HMSO: Building global index from %u object files...\n", count);
    
    GlobalIndex *idx = (GlobalIndex *)calloc(1, sizeof(GlobalIndex));
    if (!idx) return NULL;
    
    // Allocate compilation units
    idx->units = (CompilationUnit *)calloc(count, sizeof(CompilationUnit));
    if (!idx->units) {
        free(idx);
        return NULL;
    }
    idx->num_units = count;
    
    // First pass: load all summaries
    printf("HMSO: Pass 1 - Loading summaries...\n");
    for (uint32_t i = 0; i < count; i++) {
        idx->units[i].path = strdup(object_files[i]);
        idx->units[i].summary = load_summary_from_object(object_files[i]);
        idx->units[i].ir_loaded = false;
        
        if (idx->units[i].summary) {
            printf("  Loaded %s: %u functions\n", object_files[i],
                   idx->units[i].summary->num_functions);
        }
    }
    
    // Second pass: register all symbols
    printf("HMSO: Pass 2 - Registering symbols...\n");
    for (uint32_t i = 0; i < count; i++) {
        if (idx->units[i].summary) {
            register_symbols(idx, idx->units[i].summary, i);
        }
    }
    printf("  Registered %u symbols\n", idx->symbol_table.count);
    
    // Third pass: build call graph
    printf("HMSO: Pass 3 - Building call graph...\n");
    idx->call_graph = hmso_build_call_graph(idx);
    if (idx->call_graph) {
        printf("  Call graph: %u nodes, %u edges\n", 
               idx->call_graph->num_nodes, idx->call_graph->num_edges);
    }
    
    // Fourth pass: resolve cross-references
    printf("HMSO: Pass 4 - Resolving cross-references...\n");
    resolve_references(idx);
    
    // Fifth pass: mark live code
    printf("HMSO: Pass 5 - Marking live code...\n");
    hmso_mark_live_code(idx);
    
    return idx;
}

void hmso_free_global_index(GlobalIndex *idx) {
    if (!idx) return;
    
    // Free compilation units
    for (uint32_t i = 0; i < idx->num_units; i++) {
        free(idx->units[i].path);
        
        if (idx->units[i].summary) {
            CompilationSummary *sum = idx->units[i].summary;
            
            for (uint32_t f = 0; f < sum->num_functions; f++) {
                free(sum->functions[f].name);
                for (uint32_t c = 0; c < sum->functions[f].num_callsites; c++) {
                    free(sum->functions[f].callsites[c].callee_name);
                }
                free(sum->functions[f].callsites);
            }
            free(sum->functions);
            free(sum->globals);
            free(sum->edges);
            free(sum);
        }
        
        free(idx->units[i].ir_data);
    }
    free(idx->units);
    
    // Free call graph
    if (idx->call_graph) {
        for (uint32_t i = 0; i < idx->call_graph->num_nodes; i++) {
            free(idx->call_graph->nodes[i].name);
            free(idx->call_graph->nodes[i].callers);
            free(idx->call_graph->nodes[i].callees);
        }
        free(idx->call_graph->nodes);
        free(idx->call_graph->edges);
        free(idx->call_graph);
    }
    
    // Free symbol table
    for (uint32_t i = 0; i < idx->symbol_table.count; i++) {
        free(idx->symbol_table.keys[i]);
    }
    free(idx->symbol_table.keys);
    free(idx->symbol_table.unit_indices);
    
    // Free reference map
    for (uint32_t i = 0; i < idx->reference_map.count; i++) {
        free(idx->reference_map.keys[i]);
        free(idx->reference_map.user_indices[i]);
    }
    free(idx->reference_map.keys);
    free(idx->reference_map.user_indices);
    free(idx->reference_map.user_counts);
    
    // Free hot paths
    if (idx->hot_paths) {
        for (uint32_t i = 0; i < idx->hot_paths->count; i++) {
            free(idx->hot_paths->paths[i].function_indices);
        }
        free(idx->hot_paths->paths);
        free(idx->hot_paths);
    }
    
    // Free opportunities
    if (idx->opportunities) {
        free(idx->opportunities->opportunities);
        free(idx->opportunities);
    }
    
    free(idx->entry_points);
    free(idx);
}
