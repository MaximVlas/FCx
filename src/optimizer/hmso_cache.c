/**
 * FCx HMSO - Incremental Build Cache
 */

#define _POSIX_C_SOURCE 200809L
#include "hmso.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// ============================================================================
// Build Cache Management
// ============================================================================

BuildCache *hmso_cache_create(const char *cache_dir) {
    BuildCache *cache = (BuildCache *)calloc(1, sizeof(BuildCache));
    if (!cache) return NULL;
    
    cache->cache_dir = strdup(cache_dir ? cache_dir : ".fcx_cache");
    cache->entries = NULL;
    cache->count = 0;
    cache->capacity = 0;
    
    // Create cache directory if it doesn't exist
    struct stat st;
    if (stat(cache->cache_dir, &st) != 0) {
        if (mkdir(cache->cache_dir, 0755) != 0) {
            fprintf(stderr, "HMSO: Cannot create cache directory: %s\n", cache->cache_dir);
        }
    }
    
    // Load existing cache entries
    char cache_index_path[512];
    snprintf(cache_index_path, sizeof(cache_index_path), "%s/index.fcxc", cache->cache_dir);
    
    FILE *f = fopen(cache_index_path, "rb");
    if (f) {
        // Read cache index
        char magic[4];
        if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "FCXC", 4) == 0) {
            uint32_t num_entries;
            if (fread(&num_entries, sizeof(uint32_t), 1, f) == 1) {
                cache->entries = (CacheEntry *)calloc(num_entries, sizeof(CacheEntry));
                cache->capacity = num_entries;
                
                for (uint32_t i = 0; i < num_entries && cache->entries; i++) {
                    CacheEntry *entry = &cache->entries[cache->count];
                    
                    // Read source path
                    uint32_t path_len;
                    if (fread(&path_len, sizeof(uint32_t), 1, f) != 1) break;
                    
                    entry->source_path = (char *)malloc(path_len + 1);
                    if (!entry->source_path) break;
                    
                    if (fread(entry->source_path, 1, path_len, f) != path_len) {
                        free(entry->source_path);
                        break;
                    }
                    entry->source_path[path_len] = '\0';
                    
                    // Read hashes and timestamp
                    fread(&entry->source_hash, sizeof(uint64_t), 1, f);
                    fread(&entry->dependency_hash, sizeof(uint64_t), 1, f);
                    fread(&entry->timestamp, sizeof(uint64_t), 1, f);
                    
                    // Read cached object path
                    if (fread(&path_len, sizeof(uint32_t), 1, f) != 1) break;
                    
                    entry->cached_object_path = (char *)malloc(path_len + 1);
                    if (!entry->cached_object_path) {
                        free(entry->source_path);
                        break;
                    }
                    
                    if (fread(entry->cached_object_path, 1, path_len, f) != path_len) {
                        free(entry->source_path);
                        free(entry->cached_object_path);
                        break;
                    }
                    entry->cached_object_path[path_len] = '\0';
                    
                    cache->count++;
                }
            }
        }
        fclose(f);
        
        printf("HMSO: Loaded %u cache entries from %s\n", cache->count, cache_index_path);
    }
    
    return cache;
}

void hmso_cache_destroy(BuildCache *cache) {
    if (!cache) return;
    
    // Save cache index before destroying
    if (cache->cache_dir && cache->count > 0) {
        char cache_index_path[512];
        snprintf(cache_index_path, sizeof(cache_index_path), "%s/index.fcxc", cache->cache_dir);
        
        FILE *f = fopen(cache_index_path, "wb");
        if (f) {
            fwrite("FCXC", 1, 4, f);
            fwrite(&cache->count, sizeof(uint32_t), 1, f);
            
            for (uint32_t i = 0; i < cache->count; i++) {
                CacheEntry *entry = &cache->entries[i];
                
                // Write source path
                uint32_t path_len = strlen(entry->source_path);
                fwrite(&path_len, sizeof(uint32_t), 1, f);
                fwrite(entry->source_path, 1, path_len, f);
                
                // Write hashes and timestamp
                fwrite(&entry->source_hash, sizeof(uint64_t), 1, f);
                fwrite(&entry->dependency_hash, sizeof(uint64_t), 1, f);
                fwrite(&entry->timestamp, sizeof(uint64_t), 1, f);
                
                // Write cached object path
                path_len = strlen(entry->cached_object_path);
                fwrite(&path_len, sizeof(uint32_t), 1, f);
                fwrite(entry->cached_object_path, 1, path_len, f);
            }
            
            fclose(f);
            printf("HMSO: Saved %u cache entries to %s\n", cache->count, cache_index_path);
        }
    }
    
    // Free entries
    for (uint32_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].source_path);
        free(cache->entries[i].cached_object_path);
        // Note: cached_summary is owned by global index, don't free here
    }
    free(cache->entries);
    free(cache->cache_dir);
    free(cache);
}

// Find cache entry for source file
static CacheEntry *find_cache_entry(BuildCache *cache, const char *source_path) {
    if (!cache || !source_path) return NULL;
    
    for (uint32_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].source_path &&
            strcmp(cache->entries[i].source_path, source_path) == 0) {
            return &cache->entries[i];
        }
    }
    
    return NULL;
}

// Calculate hash of all dependencies for a source file
static uint64_t hash_dependencies(const char *source_path) {
    // In a real implementation, this would:
    // 1. Parse the source file for #include directives
    // 2. Hash all included files
    // 3. Recursively hash their dependencies
    
    // For now, just return a placeholder
    return hmso_hash_file(source_path);
}

bool hmso_needs_recompilation(BuildCache *cache, const char *source_path) {
    if (!cache || !source_path) return true;
    
    CacheEntry *entry = find_cache_entry(cache, source_path);
    if (!entry) {
        // Not in cache - needs compilation
        return true;
    }
    
    // Check if source changed
    uint64_t current_hash = hmso_hash_file(source_path);
    if (current_hash != entry->source_hash) {
        printf("HMSO: Source changed: %s\n", source_path);
        return true;
    }
    
    // Check if dependencies changed
    uint64_t dep_hash = hash_dependencies(source_path);
    if (dep_hash != entry->dependency_hash) {
        printf("HMSO: Dependencies changed: %s\n", source_path);
        return true;
    }
    
    // Check if cached object exists
    struct stat st;
    if (stat(entry->cached_object_path, &st) != 0) {
        printf("HMSO: Cached object missing: %s\n", entry->cached_object_path);
        return true;
    }
    
    return false;
}

// Add or update cache entry
static void update_cache_entry(BuildCache *cache, const char *source_path,
                               const char *object_path, uint64_t source_hash,
                               uint64_t dep_hash) {
    if (!cache || !source_path || !object_path) return;
    
    CacheEntry *entry = find_cache_entry(cache, source_path);
    
    if (!entry) {
        // Add new entry
        if (cache->count >= cache->capacity) {
            uint32_t new_cap = cache->capacity == 0 ? 16 : cache->capacity * 2;
            CacheEntry *new_entries = (CacheEntry *)realloc(
                cache->entries, new_cap * sizeof(CacheEntry));
            if (!new_entries) return;
            
            cache->entries = new_entries;
            cache->capacity = new_cap;
        }
        
        entry = &cache->entries[cache->count++];
        entry->source_path = strdup(source_path);
        entry->cached_object_path = strdup(object_path);
        entry->cached_summary = NULL;
    } else {
        // Update existing entry
        free(entry->cached_object_path);
        entry->cached_object_path = strdup(object_path);
    }
    
    entry->source_hash = source_hash;
    entry->dependency_hash = dep_hash;
    entry->timestamp = (uint64_t)time(NULL);
}

// ============================================================================
// Incremental Build
// ============================================================================

// Find files that need recompilation
static char **find_changed_files(BuildCache *cache, const char **source_files,
                                 uint32_t count, uint32_t *out_count) {
    if (!source_files || count == 0) {
        *out_count = 0;
        return NULL;
    }
    
    char **changed = (char **)malloc(count * sizeof(char *));
    if (!changed) {
        *out_count = 0;
        return NULL;
    }
    
    uint32_t num_changed = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        if (hmso_needs_recompilation(cache, source_files[i])) {
            changed[num_changed++] = strdup(source_files[i]);
        }
    }
    
    *out_count = num_changed;
    return changed;
}

// Identify chunks affected by changed files
static OptimizationChunk **identify_affected_chunks(HMSOContext *ctx,
                                                     char **changed_files,
                                                     uint32_t num_changed,
                                                     uint32_t *out_count) {
    if (!ctx || !changed_files || num_changed == 0) {
        *out_count = 0;
        return NULL;
    }
    
    // Build set of affected function indices
    bool *affected_funcs = (bool *)calloc(
        ctx->global_index->call_graph->num_nodes, sizeof(bool));
    if (!affected_funcs) {
        *out_count = 0;
        return NULL;
    }
    
    // Mark functions from changed files
    for (uint32_t i = 0; i < num_changed; i++) {
        // Find compilation unit for this file
        for (uint32_t u = 0; u < ctx->global_index->num_units; u++) {
            if (ctx->global_index->units[u].path &&
                strcmp(ctx->global_index->units[u].path, changed_files[i]) == 0) {
                
                // Mark all functions from this unit
                for (uint32_t n = 0; n < ctx->global_index->call_graph->num_nodes; n++) {
                    if (ctx->global_index->call_graph->nodes[n].unit_idx == u) {
                        affected_funcs[n] = true;
                    }
                }
                break;
            }
        }
    }
    
    // Also mark callers of affected functions (signature might have changed)
    CallGraph *cg = ctx->global_index->call_graph;
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t e = 0; e < cg->num_edges; e++) {
            if (affected_funcs[cg->edges[e].callee_idx] &&
                !affected_funcs[cg->edges[e].caller_idx]) {
                affected_funcs[cg->edges[e].caller_idx] = true;
                changed = true;
            }
        }
    }
    
    // Find chunks containing affected functions
    bool *affected_chunks = (bool *)calloc(ctx->num_chunks, sizeof(bool));
    if (!affected_chunks) {
        free(affected_funcs);
        *out_count = 0;
        return NULL;
    }
    
    uint32_t num_affected = 0;
    for (uint32_t c = 0; c < ctx->num_chunks; c++) {
        for (uint32_t f = 0; f < ctx->chunks[c]->num_functions; f++) {
            if (affected_funcs[ctx->chunks[c]->function_indices[f]]) {
                if (!affected_chunks[c]) {
                    affected_chunks[c] = true;
                    num_affected++;
                }
                break;
            }
        }
    }
    
    // Build result array
    OptimizationChunk **result = (OptimizationChunk **)malloc(
        num_affected * sizeof(OptimizationChunk *));
    if (!result) {
        free(affected_funcs);
        free(affected_chunks);
        *out_count = 0;
        return NULL;
    }
    
    uint32_t idx = 0;
    for (uint32_t c = 0; c < ctx->num_chunks; c++) {
        if (affected_chunks[c]) {
            result[idx++] = ctx->chunks[c];
            ctx->chunks[c]->optimized = false;  // Mark for re-optimization
        }
    }
    
    free(affected_funcs);
    free(affected_chunks);
    
    *out_count = num_affected;
    return result;
}

void hmso_incremental_build(HMSOContext *ctx, BuildCache *cache,
                            const char **source_files, uint32_t count) {
    if (!ctx || !cache || !source_files || count == 0) return;
    
    printf("HMSO: Incremental build with %u source files...\n", count);
    
    // Phase 1: Identify what needs recompilation
    uint32_t num_changed = 0;
    char **changed_files = find_changed_files(cache, source_files, count, &num_changed);
    
    if (num_changed == 0) {
        printf("HMSO: No files changed, nothing to do\n");
        return;
    }
    
    printf("HMSO: %u files need recompilation\n", num_changed);
    
    // Phase 2: Recompile only changed files
    for (uint32_t i = 0; i < num_changed; i++) {
        printf("  Recompiling: %s\n", changed_files[i]);
        
        // Generate object file path
        char object_path[512];
        snprintf(object_path, sizeof(object_path), "%s/%s.fcx.o",
                 cache->cache_dir, changed_files[i]);
        
        // Compile file (placeholder - real implementation would call compiler)
        // hmso_compile_file(ctx, changed_files[i], object_path);
        
        // Update cache
        uint64_t source_hash = hmso_hash_file(changed_files[i]);
        uint64_t dep_hash = hash_dependencies(changed_files[i]);
        update_cache_entry(cache, changed_files[i], object_path, source_hash, dep_hash);
    }
    
    // Phase 3: Identify affected chunks
    uint32_t num_affected = 0;
    OptimizationChunk **affected = identify_affected_chunks(ctx, changed_files,
                                                            num_changed, &num_affected);
    
    printf("HMSO: %u chunks affected by changes\n", num_affected);
    
    // Phase 4: Re-optimize only affected chunks
    if (affected && num_affected > 0) {
        for (uint32_t i = 0; i < num_affected; i++) {
            hmso_optimize_chunk(affected[i], ctx->global_index, &ctx->config);
        }
    }
    
    // Phase 5: Incremental link
    // (In real implementation, would only re-link affected sections)
    
    // Cleanup
    for (uint32_t i = 0; i < num_changed; i++) {
        free(changed_files[i]);
    }
    free(changed_files);
    free(affected);
    
    printf("HMSO: Incremental build complete\n");
}

// ============================================================================
// Object File I/O
// ============================================================================

bool hmso_write_object_file(const char *path, const void *code, size_t code_size,
                            const void *ir, size_t ir_size,
                            const CompilationSummary *summary) {
    if (!path) return false;
    
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "HMSO: Cannot create object file: %s\n", path);
        return false;
    }
    
    // Calculate offsets
    uint64_t header_size = sizeof(FCXObjectHeader);
    uint64_t code_offset = header_size;
    uint64_t ir_offset = code_offset + (code_size ? code_size : 0);
    uint64_t summary_offset = ir_offset + (ir_size ? ir_size : 0);
    
    // Calculate summary size
    uint64_t summary_size = 0;
    if (summary) {
        summary_size = sizeof(uint32_t);  // num_functions
        for (uint32_t i = 0; i < summary->num_functions; i++) {
            summary_size += sizeof(uint32_t);  // name length
            summary_size += strlen(summary->functions[i].name);
            summary_size += sizeof(uint64_t);  // hash
            summary_size += sizeof(uint32_t) * 4;  // metrics
            summary_size += sizeof(uint32_t);  // num_callsites
            for (uint32_t j = 0; j < summary->functions[i].num_callsites; j++) {
                summary_size += sizeof(uint32_t);  // callee name length
                summary_size += strlen(summary->functions[i].callsites[j].callee_name);
                summary_size += sizeof(uint32_t);  // call_count
            }
        }
    }
    
    // Write header
    FCXObjectHeader header = {
        .magic = FCXO_MAGIC,
        .version = FCXO_VERSION,
        .code_offset = code_offset,
        .code_size = code_size,
        .ir_offset = ir_offset,
        .ir_size = ir_size,
        .summary_offset = summary_offset,
        .summary_size = summary_size,
        .profile_offset = 0,
        .profile_size = 0,
    };
    
    fwrite(&header, sizeof(header), 1, f);
    
    // Write code section
    if (code && code_size > 0) {
        fwrite(code, 1, code_size, f);
    }
    
    // Write IR section
    if (ir && ir_size > 0) {
        fwrite(ir, 1, ir_size, f);
    }
    
    // Write summary section
    if (summary) {
        fwrite(&summary->num_functions, sizeof(uint32_t), 1, f);
        
        for (uint32_t i = 0; i < summary->num_functions; i++) {
            FunctionSummary *func = &summary->functions[i];
            
            // Write name
            uint32_t name_len = strlen(func->name);
            fwrite(&name_len, sizeof(uint32_t), 1, f);
            fwrite(func->name, 1, name_len, f);
            
            // Write metrics
            fwrite(&func->hash, sizeof(uint64_t), 1, f);
            fwrite(&func->instruction_count, sizeof(uint32_t), 1, f);
            fwrite(&func->basic_block_count, sizeof(uint32_t), 1, f);
            fwrite(&func->cyclomatic_complexity, sizeof(uint32_t), 1, f);
            fwrite(&func->flags, sizeof(uint32_t), 1, f);
            fwrite(&func->memory_access, sizeof(uint32_t), 1, f);
            fwrite(&func->inline_cost, sizeof(uint32_t), 1, f);
            
            // Write call sites
            fwrite(&func->num_callsites, sizeof(uint32_t), 1, f);
            for (uint32_t j = 0; j < func->num_callsites; j++) {
                uint32_t callee_len = strlen(func->callsites[j].callee_name);
                fwrite(&callee_len, sizeof(uint32_t), 1, f);
                fwrite(func->callsites[j].callee_name, 1, callee_len, f);
                fwrite(&func->callsites[j].call_count, sizeof(uint32_t), 1, f);
            }
        }
    }
    
    fclose(f);
    
    printf("HMSO: Wrote object file: %s (%lu bytes)\n", path,
           (unsigned long)(summary_offset + summary_size));
    
    return true;
}
