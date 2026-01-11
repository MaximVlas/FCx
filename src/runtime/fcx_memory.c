#include "fcx_runtime.h"
#include <string.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

// Global memory manager instance
FcxMemoryManager g_fcx_memory_manager = {0};

// Automatically detect pointer width
#if defined(__LP64__) || defined(_WIN64) || __SIZEOF_POINTER__ == 8
    #define FCX_POINTER_BITS 64
    #define fcx_clz(x) __builtin_clzll(x)
#else
    #define FCX_POINTER_BITS 32
    #define fcx_clz(x) __builtin_clzl(x)
#endif

#define FCX_SMALL_SIZE_MAX 128
#define FCX_SIZE_CLASSES 32
#define FCX_MIN_BLOCK_SIZE 32          // Minimum block size including header
#define FCX_MIN_FRAGMENT_SIZE 16       // Minimum fragment to split off
#define FCX_MAX_ALIGNMENT 4096         // Maximum supported alignment
#define FCX_BLOCK_OVERHEAD sizeof(BlockHeader)

// Classes: 0=8, 1=16, 2=32, 3=64, 4=128
static const uint8_t small_size_class[17] = {
    0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4
};

static inline size_t get_size_class(size_t size) {
    // 1. Tiny allocations & Zero
    if (__builtin_expect(size <= 8, 0)) {
        return 0;
    }

    // 2. Small Path (9 - 128 bytes)
    if (__builtin_expect(size <= FCX_SMALL_SIZE_MAX, 1)) {
        // (size + 7) >> 3 for 128 is 16. Guaranteed in-bounds for small_size_class.
        return small_size_class[(size + 7) >> 3];
    }

    // 3. Large Path (> 128 bytes)
    if (__builtin_expect(size == SIZE_MAX, 0)) {
        return FCX_SIZE_CLASSES - 1;
    }

    // Mathematical continuity:
    // size 129-256: (64 - 56) - 3 = 5
    // size 257-512: (64 - 55) - 3 = 6
    // No offset (+5) needed; the bit math naturally continues from class 4.
    size_t sc = (size_t)(FCX_POINTER_BITS - fcx_clz(size - 1)) - 3;
    
    // 4. Final safety clamp
    if (__builtin_expect(sc >= FCX_SIZE_CLASSES, 0)) {
        return FCX_SIZE_CLASSES - 1;
    }

    return sc;
}

// Direct sys_brk syscall with validation
// Returns NULL on error instead of (void*)-1 for safer error handling
// Also validates alignment of returned address
static inline void *sys_brk(void *addr) {
    void *actual_brk;
    
    __asm__ __volatile__(
        "syscall"
        : "=a"(actual_brk)                /* output in rax (index 0) */
        : "0"(__NR_brk),                  /* input syscall num in rax */
          "D"(addr)                       /* input addr in rdi */
        : "rcx", "r11", "memory"          /* clobbers */
    );
    
    // If we requested a specific address but the kernel 
    // returned something lower, the allocation failed.
    if (addr != (void *)0 && (uintptr_t)actual_brk < (uintptr_t)addr) {
        // Only set errno if you are sure your environment supports it
        // (e.g., you aren't writing a bare-metal kernel).
        errno = ENOMEM; 
        return (void *)-1;
    }
    
    return actual_brk;
}
// Extend heap using sys_brk with proper validation
static int extend_heap(FcxMemoryManager *mgr, size_t min_size) {
    /* Validate manager and heap pointers */
    if (mgr == NULL || mgr->heap_start == NULL || mgr->heap_end == NULL) {
        return -1;
    }
    
    /* Validate heap pointers are properly ordered */
    if (mgr->heap_end < mgr->heap_start) {
        return -1;
    }
    
    uintptr_t heap_start_uint = (uintptr_t)mgr->heap_start;
    uintptr_t heap_end_uint = (uintptr_t)mgr->heap_end;
    size_t current_size = heap_end_uint - heap_start_uint;
    
    /* Determine proper alignment for the platform - Linux uses 16-byte alignment */
    size_t alignment = sizeof(void *) >= 8 ? 16 : 8;
    
    /* Check for integer overflow before calculations */
    size_t new_size;
    if (SIZE_MAX / 2 < current_size) {
        /* Cannot double without overflow */
        if (SIZE_MAX - current_size < min_size) {
            return -1; /* Addition would overflow */
        }
        new_size = current_size + min_size;
    } else {
        /* Safe to double */
        new_size = current_size * 2;
        
        /* Ensure we have at least min_size additional space */
        if (SIZE_MAX - current_size < min_size || new_size < current_size + min_size) {
            if (SIZE_MAX - current_size < min_size) {
                return -1; /* Addition would overflow */
            }
            new_size = current_size + min_size;
        }
    }
    
    /* Fix: Proper overflow check for alignment calculation */
    if (SIZE_MAX - (alignment - 1) < new_size) {
        return -1; /* Would overflow during alignment calculation */
    }
    
    /* Align the new size to proper boundary */
    new_size = (new_size + alignment - 1) & ~(alignment - 1);
    
    /* Fix: Use uintptr_t for overflow check to match pointer arithmetic */
    if (UINTPTR_MAX - heap_start_uint < new_size) {
        return -1; /* Would wrap around */
    }
    
    /* Calculate target address (safe after overflow checks) */
    void *target = (void *)(heap_start_uint + new_size);
    void *new_end = sys_brk(target);
    
    /* Check for brk failure - Linux brk() returns (void *)-1 on error */
    if (new_end == (void *)-1) {
        return -1;
    }
    
    /* Verify brk didn't shrink the heap (defensive check) */
    if ((uintptr_t)new_end < heap_end_uint) {
        return -1;
    }
    
    /* Fix: Calculate minimum required size with overflow protection */
    size_t min_required_size;
    if (SIZE_MAX - current_size < min_size) {
        return -1; /* This should have been caught earlier, but be defensive */
    }
    min_required_size = current_size + min_size;
    
    /* Fix: Check against aligned minimum requirement */
    uintptr_t min_required_addr = heap_start_uint + min_required_size;
    if ((uintptr_t)new_end < min_required_addr) {
        return -1;
    }
    
    /* Verify new_end is properly aligned (defensive - should always pass on Linux) */
    if ((uintptr_t)new_end & (alignment - 1)) {
        return -1;
    }
    
    mgr->heap_end = (uint8_t *)new_end;
    return 0;
}

// ============================================================================
// O(1) FREE LIST OPERATIONS (Doubly-linked list)
// ============================================================================

// Insert block into free list - O(1), NO VALIDATION
static inline void insert_free_block_fast(FcxMemoryManager *mgr, BlockHeader *block, size_t size_class) {
    BlockHeader *head = mgr->size_classes[size_class];
    block->next = head;
    block->prev = NULL;
    if (head) {
        head->prev = block;
    }
    mgr->size_classes[size_class] = block;
}

// Remove block from free list - O(1) with doubly-linked list
static inline void remove_free_block_fast(FcxMemoryManager *mgr, BlockHeader *block, size_t size_class) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        mgr->size_classes[size_class] = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->next = NULL;
    block->prev = NULL;
}

// Get next physical block - O(1)
static inline BlockHeader *get_next_physical(BlockHeader *block, uint8_t *heap_end) {
    uint8_t *next_addr = (uint8_t *)block + sizeof(BlockHeader) + block->size;
    if (next_addr >= heap_end) return NULL;
    BlockHeader *next = (BlockHeader *)next_addr;
    return (next->magic == FCX_BLOCK_MAGIC) ? next : NULL;
}

// Merge two adjacent blocks
static inline __attribute__((unused)) void merge_blocks_fast(BlockHeader *first, BlockHeader *second) {
    first->size += sizeof(BlockHeader) + second->size;
    first->has_next = second->has_next;
}

// ============================================================================
// Memory Allocator Implementation
// ============================================================================

int fcx_memory_init(void) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    
    /* 1. CRITICAL: Validate global state before any operations */
    if (__builtin_expect(mgr == NULL, 0)) {
        errno = EINVAL;
        return -1;
    }
    
    /* 2. Atomic state check with memory barrier - prevent TOCTOU races */
    void *volatile heap_start = __atomic_load_n(&mgr->heap_start, __ATOMIC_ACQUIRE);
    if (heap_start != NULL) {
        return 0;  /* Already initialized */
    }
    
    /* 3. Comprehensive sys_brk validation with retry logic */
    void *current_brk;
    int retry_count = 0;
    const int max_retries = 3;
    
    do {
        current_brk = sys_brk(NULL);
        if (__builtin_expect(current_brk != (void *)-1, 1)) {
            break;
        }
        
        /* Handle transient errors - EAGAIN, EINTR */
        if (errno == EAGAIN || errno == EINTR) {
            retry_count++;
            if (retry_count >= max_retries) {
                break;
            }
            continue;
        }
        
        /* Non-recoverable error */
        return -1;
    } while (retry_count < max_retries);
    
    if (__builtin_expect(current_brk == (void *)-1, 0)) {
        return -1;
    }
    
    /* 4. Validate current_brk against system limits */
    uintptr_t brk_uint = (uintptr_t)current_brk;
    if (__builtin_expect(brk_uint == 0 || brk_uint >= UINTPTR_MAX - 4096, 0)) {
        errno = ENOMEM;
        return -1;
    }
    
    /* 5. Robust alignment calculation with overflow protection */
    size_t alignment = 16;  /* Standard Linux alignment */
    uintptr_t aligned_start;
    
    /* Check for overflow before alignment calculation */
    if (__builtin_expect(UINTPTR_MAX - (alignment - 1) < brk_uint, 0)) {
        errno = ENOMEM;
        return -1;
    }
    
    aligned_start = (brk_uint + alignment - 1) & ~(alignment - 1);
    
    /* 6. Validate alignment result */
    if (__builtin_expect((aligned_start & (alignment - 1)) != 0, 0)) {
        errno = EINVAL;  /* Alignment failed - should never happen */
        return -1;
    }
    
    /* 7. Initial heap size with overflow protection */
    const size_t initial_heap_size = 1024 * 1024;  /* 1MB */
    uintptr_t requested_end;
    
    if (__builtin_expect(UINTPTR_MAX - initial_heap_size < aligned_start, 0)) {
        errno = ENOMEM;
        return -1;
    }
    
    requested_end = aligned_start + initial_heap_size;
    
    /* 8. Request heap extension with comprehensive validation */
    void *heap_end = sys_brk((void *)requested_end);
    if (__builtin_expect(heap_end == (void *)-1, 0)) {
        /* Try fallback to smaller size if initial request fails */
        size_t fallback_size = 512 * 1024;  /* 512KB */
        if (UINTPTR_MAX - fallback_size < aligned_start) {
            return -1;
        }
        
        heap_end = sys_brk((void *)(aligned_start + fallback_size));
        if (__builtin_expect(heap_end == (void *)-1, 0)) {
            return -1;
        }
        requested_end = aligned_start + fallback_size;
    }
    
    /* 9. Validate heap extension result comprehensively */
    uintptr_t heap_end_uint = (uintptr_t)heap_end;
    
    /* Check for kernel error return */
    if (__builtin_expect(heap_end == (void *)-1, 0)) {
        return -1;
    }
    
    /* Validate we got at least the requested memory */
    if (__builtin_expect(heap_end_uint < requested_end, 0)) {
        /* Partial allocation - check if we have minimum required space */
        size_t min_required = sizeof(BlockHeader) + FCX_MIN_BLOCK_SIZE;
        if (heap_end_uint < aligned_start + min_required) {
            /* Release partial allocation */
            sys_brk((void *)brk_uint);
            errno = ENOMEM;
            return -1;
        }
        /* Use what we got, but log warning (in debug mode) */
        if (mgr->debug_mode) {
            /* Debug logging would go here */
        }
    }
    
    /* 10. Validate heap size against minimum requirements */
    size_t actual_heap_size = heap_end_uint - aligned_start;
    if (__builtin_expect(actual_heap_size < sizeof(BlockHeader) + FCX_MIN_BLOCK_SIZE, 0)) {
        sys_brk((void *)brk_uint);  /* Release allocation */
        errno = ENOMEM;
        return -1;
    }
    
    /* 11. Validate alignment of heap end */
    if (__builtin_expect((heap_end_uint & (alignment - 1)) != 0, 0)) {
        /* Linux brk should always return page-aligned addresses */
        sys_brk((void *)brk_uint);
        errno = EINVAL;
        return -1;
    }
    
    /* 12. CRITICAL: Zero-initialize the entire heap before use */
    /* This prevents information leaks and catches use-before-init bugs */
    size_t zero_size = actual_heap_size;
    if (zero_size > 64 * 1024) {
        zero_size = 64 * 1024;  /* Zero first 64KB for performance */
    }
    __builtin_memset((void *)aligned_start, 0, zero_size);
    
    /* 13. Atomic state update with memory barrier */
    BlockHeader *initial_block = (BlockHeader *)aligned_start;
    
    /* Initialize manager state atomically */
    mgr->heap_start = (uint8_t *)aligned_start;
    mgr->heap_end = (uint8_t *)heap_end;
    
    /* 14. Initialize size classes with poison values for debugging */
    for (size_t i = 0; i < FCX_SIZE_CLASSES; i++) {
        mgr->size_classes[i] = NULL;
    }
    
    /* 15. Initialize arena table */
    for (size_t i = 0; i < FCX_MAX_ARENA_SCOPES; i++) {
        mgr->arena_table[i] = NULL;
    }
    
    /* 16. Initialize remaining manager fields */
    mgr->active_arenas = NULL;
    mgr->slab_caches = NULL;
    mgr->fixed_pools = NULL;
    mgr->total_allocated = 0;
    mgr->total_freed = 0;
    mgr->fragmentation_pct = 0;
    mgr->debug_mode = 0;  /* Performance-critical paths should disable debug mode */
    mgr->alignment = alignment;
    mgr->endianness = FCX_ENDIAN_LITTLE;
    mgr->last_phys_block = NULL;  /* Ensure this is initialized */
    
    /* 17. Create initial free block with comprehensive validation */
    size_t block_overhead = sizeof(BlockHeader);
    size_t available_size = actual_heap_size - block_overhead;
    
    if (__builtin_expect(available_size < FCX_MIN_BLOCK_SIZE, 0)) {
        /* This should have been caught earlier, but be defensive */
        sys_brk((void *)brk_uint);
        mgr->heap_start = NULL;
        mgr->heap_end = NULL;
        errno = ENOMEM;
        return -1;
    }
    
    /* Initialize block header with poison values to catch use-before-init */
    initial_block->size = available_size;
    initial_block->is_free = 1;
    initial_block->has_next = 0;
    initial_block->prev_free = 0;
    initial_block->magic = FCX_BLOCK_MAGIC;
    initial_block->next = NULL;
    initial_block->prev = NULL;
    initial_block->phys_prev = NULL;
    
    /* 18. Validate block initialization */
    if (__builtin_expect(initial_block->magic != FCX_BLOCK_MAGIC, 0)) {
        /* Memory corruption detected during initialization */
        sys_brk((void *)brk_uint);
        mgr->heap_start = NULL;
        mgr->heap_end = NULL;
        errno = EFAULT;
        return -1;
    }
    
    /* 19. Calculate size class with bounds checking */
    size_t size_class = get_size_class(available_size);
    if (__builtin_expect(size_class >= FCX_SIZE_CLASSES, 0)) {
        size_class = FCX_SIZE_CLASSES - 1;  /* Clamp to maximum class */
    }
    
    /* 20. Insert into free list with validation */
    insert_free_block_fast(mgr, initial_block, size_class);
    
    /* 21. Final validation: ensure block is in free list */
    if (__builtin_expect(mgr->size_classes[size_class] != initial_block, 0)) {
        /* Critical failure - inconsistent state */
        sys_brk((void *)brk_uint);
        mgr->heap_start = NULL;
        mgr->heap_end = NULL;
        errno = EFAULT;
        return -1;
    }
    
    /* 22. Memory barrier to ensure all writes are visible */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    
    return 0;
}


static inline bool is_power_of_two(size_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

void *fcx_alloc(size_t size, size_t alignment) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;

    // 1. CRITICAL: Input validation
    if (__builtin_expect(size == 0, 0)) {
        errno = EINVAL;
        return NULL;
    }

    if (__builtin_expect(alignment == 0, 0)) {
        alignment = 8;
    }

    if (__builtin_expect(!is_power_of_two(alignment) || alignment > FCX_MAX_ALIGNMENT, 0)) {
        errno = EINVAL;
        return NULL;
    }

    // 2. Overflow protection
    if (__builtin_expect(size > (SIZE_MAX - alignment) || 
                        size > (SIZE_MAX - FCX_BLOCK_OVERHEAD), 0)) {
        errno = ENOMEM;
        return NULL;
    }

    // 3. Initialization
    if (__builtin_expect(mgr->heap_start == NULL, 0)) {
        if (fcx_memory_init() != 0) {
            return NULL;
        }
    }

    // 4. Alignment and size normalization
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    if (aligned_size < FCX_MIN_BLOCK_SIZE - FCX_BLOCK_OVERHEAD) {
        aligned_size = FCX_MIN_BLOCK_SIZE - FCX_BLOCK_OVERHEAD;
    }

    size_t size_class = get_size_class(aligned_size);

    // 5. Freelist search with physical block safety
    for (size_t sc = size_class; sc < FCX_SIZE_CLASSES; sc++) {
        BlockHeader *current = mgr->size_classes[sc];
        
        while (current) {
            // Validate block integrity before use
            if (__builtin_expect(current->magic != FCX_BLOCK_MAGIC, 0)) {
                // Corrupted block - this should never happen in production
                errno = EFAULT;
                return NULL;
            }

            if (current->is_free && current->size >= aligned_size) {
                remove_free_block_fast(mgr, current, sc);

                // Splitting logic with proper fragment sizing
                size_t total_needed = aligned_size + FCX_BLOCK_OVERHEAD;
                size_t remaining = current->size - aligned_size;
                
                if (remaining >= FCX_MIN_FRAGMENT_SIZE + FCX_BLOCK_OVERHEAD) {
                    BlockHeader *new_block = (BlockHeader *)((uint8_t *)current + total_needed);
                    
                    // Initialize new fragment block
                    new_block->size = remaining - FCX_BLOCK_OVERHEAD;
                    new_block->is_free = 1;
                    new_block->magic = FCX_BLOCK_MAGIC;
                    new_block->phys_prev = current;
                    new_block->has_next = current->has_next;
                    new_block->prev_free = 0;
                    new_block->next = NULL;
                    new_block->prev = NULL;
                    
                    // Update current block
                    current->size = aligned_size;
                    current->has_next = 1;
                    
                    // Update next physical block's phys_prev pointer
                    BlockHeader *next_phys = (BlockHeader *)((uint8_t *)new_block + FCX_BLOCK_OVERHEAD + new_block->size);
                    if ((uint8_t *)next_phys < (uint8_t *)mgr->heap_end && 
                        (uint8_t *)next_phys > (uint8_t *)mgr->heap_start) {
                        next_phys->phys_prev = new_block;
                    }

                    // Reinsert fragment into appropriate size class
                    size_t frag_class = get_size_class(new_block->size);
                    insert_free_block_fast(mgr, new_block, frag_class);
                }

                // Finalize allocation
                current->is_free = 0;
                current->prev_free = 0;  // Not free, so no free prev pointer needed
                
                mgr->total_allocated += aligned_size;
                
                void *user_ptr = (uint8_t *)current + FCX_BLOCK_OVERHEAD;
                return user_ptr;
            }
            current = current->next;
        }
    }

    // 6. Heap extension with comprehensive safety checks
    size_t total_block_size = aligned_size + FCX_BLOCK_OVERHEAD;
    
    // Align heap extension to page boundaries for efficiency
    size_t page_aligned_size = (total_block_size + 4095) & ~4095;
    
    uint8_t *old_heap_end = (uint8_t *)mgr->heap_end;
    
    if (__builtin_expect(extend_heap(mgr, page_aligned_size) != 0, 0)) {
        return NULL;
    }

    // Validate new block position
    BlockHeader *new_block = (BlockHeader *)old_heap_end;
    if (__builtin_expect((uint8_t *)new_block + total_block_size > (uint8_t *)mgr->heap_end, 0)) {
        // Extension failed to provide enough space
        errno = ENOMEM;
        return NULL;
    }

    // Initialize new block completely
    new_block->size = aligned_size;
    new_block->is_free = 0;
    new_block->magic = FCX_BLOCK_MAGIC;
    new_block->has_next = 0;           // No next block yet
    new_block->prev_free = 0;          // Not free
    new_block->next = NULL;            // Not in free list
    new_block->prev = NULL;            // Not in free list
    new_block->phys_prev = NULL;       // First block in this heap region
    
    // Update total allocation statistics
    mgr->total_allocated += aligned_size;
    
    return (uint8_t *)new_block + FCX_BLOCK_OVERHEAD;
}

// Deallocation 
void fcx_free(void *ptr) {
    // 1. Basic Sanity Check
    if (__builtin_expect(!ptr, 0)) return;

    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    
    // 2. Locate and Validate Header
    BlockHeader *block = (BlockHeader *)((uint8_t *)ptr - FCX_BLOCK_OVERHEAD);

    // CRITICAL: Integrity Check
    // If magic is wrong, the heap is corrupted or the pointer is invalid.
    if (__builtin_expect(block->magic != FCX_BLOCK_MAGIC, 0)) {
        // In a kernel, this would be a panic. In userspace, we abort or set errno.
        return; 
    }

    // Double-free protection
    if (__builtin_expect(block->is_free, 0)) {
        return; 
    }

    // Prefetching for performance
    __builtin_prefetch(block->phys_prev, 1, 3);
    
    block->is_free = 1;
    mgr->total_allocated -= block->size; // Track net allocation

    // 3. Coalesce Backwards (Physical Previous)
    BlockHeader *prev_phys = block->phys_prev;
    // Ensure prev_phys is within heap boundaries
    if (prev_phys && 
        (uintptr_t)prev_phys >= (uintptr_t)mgr->heap_start &&
        prev_phys->magic == FCX_BLOCK_MAGIC && 
        prev_phys->is_free) {
        
        size_t prev_sc = get_size_class(prev_phys->size);
        remove_free_block_fast(mgr, prev_phys, prev_sc);
        
        // Merge block into prev_phys
        prev_phys->size += FCX_BLOCK_OVERHEAD + block->size;
        prev_phys->has_next = block->has_next;
        
        // Update the 'current' block pointer to the now-larger prev block
        block = prev_phys;
        // Invalidate magic of the old header to prevent accidental reuse
        ((BlockHeader *)((uint8_t *)ptr - FCX_BLOCK_OVERHEAD))->magic = 0;
    }

    // 4. Coalesce Forwards (Physical Next)
    if (block->has_next) {
        BlockHeader *next_phys = (BlockHeader *)((uint8_t *)block + FCX_BLOCK_OVERHEAD + block->size);
        
        // Safety check: Ensure next_phys doesn't overshoot heap_end
        if ((uintptr_t)next_phys < (uintptr_t)mgr->heap_end && 
            next_phys->magic == FCX_BLOCK_MAGIC && 
            next_phys->is_free) {
            
            size_t next_sc = get_size_class(next_phys->size);
            remove_free_block_fast(mgr, next_phys, next_sc);
            
            // Merge next_phys into block
            block->size += FCX_BLOCK_OVERHEAD + next_phys->size;
            block->has_next = next_phys->has_next;
            
            // Invalidate next_phys magic
            next_phys->magic = 0;
        }
    }

    // 5. Update Physical Continuity for the "next-next" block
    if (block->has_next) {
        BlockHeader *future_phys = (BlockHeader *)((uint8_t *)block + FCX_BLOCK_OVERHEAD + block->size);
        if ((uintptr_t)future_phys < (uintptr_t)mgr->heap_end) {
            future_phys->phys_prev = block;
        }
    } else {
        // If this block has no next, it is the new tail of the heap
        mgr->last_phys_block = block;
    }

    // 6. Final Re-insertion
    size_t final_sc = get_size_class(block->size);
    insert_free_block_fast(mgr, block, final_sc);
}

void *fcx_realloc(void *ptr, size_t new_size) {
    // 1. Extreme Input Validation & Fast Paths
    if (__builtin_expect(!ptr, 0)) {
        return fcx_alloc(new_size, 8);
    }
    
    if (__builtin_expect(new_size == 0, 0)) {
        fcx_free(ptr);
        return NULL;
    }

    // 2. Size & Alignment Safety
    // Ensure new_size + alignment doesn't wrap around SIZE_MAX
    if (__builtin_expect(new_size > (SIZE_MAX - FCX_BLOCK_OVERHEAD - 7), 0)) {
        errno = ENOMEM;
        return NULL;
    }

    size_t aligned_size = (new_size + 7) & ~((size_t)7);
    if (aligned_size < FCX_MIN_BLOCK_SIZE - FCX_BLOCK_OVERHEAD) {
        aligned_size = FCX_MIN_BLOCK_SIZE - FCX_BLOCK_OVERHEAD;
    }

    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    
    // 3. Pointer Range & Alignment Sanity Check
    // ptr must be within heap AND must be 8-byte aligned
    if (__builtin_expect((uintptr_t)ptr < (uintptr_t)mgr->heap_start || 
                        (uintptr_t)ptr >= (uintptr_t)mgr->heap_end ||
                        ((uintptr_t)ptr & 7) != 0, 0)) {
        errno = EFAULT;
        return NULL;
    }

    BlockHeader *block = (BlockHeader *)((uint8_t *)ptr - FCX_BLOCK_OVERHEAD);

    // 4. Header Integrity & Anti-Corruption Check
    // Verify magic, state, and size consistency
    if (__builtin_expect(block->magic != FCX_BLOCK_MAGIC || block->is_free, 0)) {
        errno = EFAULT; 
        return NULL; 
    }

    // 5. Case A: In-place Optimization (Shrinking or Same Size)
    if (block->size >= aligned_size) {
        size_t diff = block->size - aligned_size;
        
        // Only split if the remainder is large enough to be a valid block
        if (diff >= FCX_MIN_BLOCK_SIZE) {
            BlockHeader *spare = (BlockHeader *)((uint8_t *)ptr + aligned_size);
            
            spare->magic = FCX_BLOCK_MAGIC;
            spare->size = diff - FCX_BLOCK_OVERHEAD;
            spare->is_free = 1;
            spare->phys_prev = block;
            spare->has_next = block->has_next;
            
            block->size = aligned_size;
            block->has_next = 1;

            if (spare->has_next) {
                BlockHeader *next_next = (BlockHeader *)((uint8_t *)spare + FCX_BLOCK_OVERHEAD + spare->size);
                // Validate next_next address before writing
                if (__builtin_expect((uintptr_t)next_next + FCX_BLOCK_OVERHEAD <= (uintptr_t)mgr->heap_end, 1)) {
                    next_next->phys_prev = spare;
                }
            } else {
                mgr->last_phys_block = spare;
            }

            insert_free_block_fast(mgr, spare, get_size_class(spare->size));
        }
        return ptr;
    }

    // 6. Case B: Forward Coalescing (Expansion)
    if (block->has_next) {
        BlockHeader *next_phys = (BlockHeader *)((uint8_t *)block + FCX_BLOCK_OVERHEAD + block->size);
        
        // Validate next_phys is within heap bounds and valid
        if ((uintptr_t)next_phys + FCX_BLOCK_OVERHEAD < (uintptr_t)mgr->heap_end &&
            next_phys->magic == FCX_BLOCK_MAGIC && 
            next_phys->is_free &&
            next_phys->phys_prev == block) {
            
            size_t total_avail = block->size + FCX_BLOCK_OVERHEAD + next_phys->size;
            
            if (total_avail >= aligned_size) {
                remove_free_block_fast(mgr, next_phys, get_size_class(next_phys->size));
                
                block->size = total_avail;
                block->has_next = next_phys->has_next;
                
                // Recursively call realloc with same ptr to handle splitting logic in Case A
                // Since block->size is now >= aligned_size, this will enter Case A and return.
                return fcx_realloc(ptr, new_size);
            }
        }
    }

    // 7. Case C: Slow Path (Relocation)
    size_t old_data_size = block->size;
    void *new_ptr = fcx_alloc(new_size, 8);
    
    if (__builtin_expect(new_ptr != NULL, 1)) {
        // Copy only the actual data, constrained by the smaller of the two sizes
        // to prevent reading into the next block's header
        size_t copy_size = (old_data_size < new_size) ? old_data_size : new_size;
        __builtin_memcpy(new_ptr, ptr, copy_size);

        // Verify integrity before freeing old pointer
        if (__builtin_expect(block->magic == FCX_BLOCK_MAGIC, 1)) {
            fcx_free(ptr);
        }
        return new_ptr;
    }

    return NULL;
}


// ============================================================================
// Arena Allocator - O(1) DIRECT INDEX LOOKUP
// ============================================================================

void *fcx_arena_alloc(size_t size, size_t alignment, uint32_t scope_id) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    
    // O(1) direct index lookup
    size_t idx = scope_id & (FCX_MAX_ARENA_SCOPES - 1);
    ArenaAllocator *arena = mgr->arena_table[idx];

    // Verify scope_id matches (handle collisions)
    if (arena && arena->scope_id != scope_id) {
        // Collision - fall back to list search
        arena = mgr->active_arenas;
        while (arena && arena->scope_id != scope_id) {
            arena = arena->next;
        }
    }

    if (!arena) {
        size_t arena_size = (size * 2 < 4096) ? 4096 : size * 2;
        arena = (ArenaAllocator *)fcx_alloc(sizeof(ArenaAllocator), 8);
        if (__builtin_expect(!arena, 0)) return NULL;

        arena->base = (uint8_t *)fcx_alloc(arena_size, alignment);
        if (__builtin_expect(!arena->base, 0)) {
            fcx_free(arena);
            return NULL;
        }

        arena->current = arena->base;
        arena->size = arena_size;
        arena->remaining = arena_size;
        arena->scope_id = scope_id;
        arena->next = mgr->active_arenas;
        mgr->active_arenas = arena;
        mgr->arena_table[idx] = arena;  // Cache in direct index table
    }

    // Bump pointer allocation
    uintptr_t current = (uintptr_t)arena->current;
    uintptr_t aligned = (current + alignment - 1) & ~(alignment - 1);
    uintptr_t end = aligned + size;

    if (__builtin_expect(end > (uintptr_t)arena->base + arena->size, 0)) {
        return fcx_alloc(size, alignment);
    }

    arena->current = (uint8_t *)end;
    arena->remaining -= (end - current);
    return (void *)aligned;
}

void fcx_arena_reset(uint32_t scope_id) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    size_t idx = scope_id & (FCX_MAX_ARENA_SCOPES - 1);
    
    // Clear from direct index table
    if (mgr->arena_table[idx] && mgr->arena_table[idx]->scope_id == scope_id) {
        mgr->arena_table[idx] = NULL;
    }

    ArenaAllocator *arena = mgr->active_arenas;
    ArenaAllocator *prev = NULL;

    while (arena) {
        if (arena->scope_id == scope_id) {
            if (prev) prev->next = arena->next;
            else mgr->active_arenas = arena->next;

            fcx_free(arena->base);
            fcx_free(arena);
            return;
        }
        prev = arena;
        arena = arena->next;
    }
}

// ============================================================================
// Slab Allocator - OPTIMIZED with hash table
// ============================================================================

#define FCX_SLAB_HASH_SIZE 32
static SlabAllocator *g_slab_hash[FCX_SLAB_HASH_SIZE] = {0};

void *fcx_slab_alloc(size_t object_size, uint32_t type_hash) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    
    // O(1) hash lookup
    size_t hash_idx = type_hash & (FCX_SLAB_HASH_SIZE - 1);
    SlabAllocator *slab = g_slab_hash[hash_idx];

    // Verify type_hash matches
    if (slab && slab->type_hash != type_hash) {
        slab = mgr->slab_caches;
        while (slab && slab->type_hash != type_hash) {
            slab = slab->next;
        }
    }

    if (!slab) {
        slab = (SlabAllocator *)fcx_alloc(sizeof(SlabAllocator), 8);
        if (__builtin_expect(!slab, 0)) return NULL;

        slab->object_size = object_size;
        slab->objects_per_slab = 64;
        slab->type_hash = type_hash;

        size_t slab_size = object_size * 64;
        slab->slab_memory = (uint8_t *)fcx_alloc(slab_size, 8);
        if (__builtin_expect(!slab->slab_memory, 0)) {
            fcx_free(slab);
            return NULL;
        }

        slab->free_objects = (void **)fcx_alloc(sizeof(void *) * 64, 8);
        if (__builtin_expect(!slab->free_objects, 0)) {
            fcx_free(slab->slab_memory);
            fcx_free(slab);
            return NULL;
        }

        // Initialize free list
        uint8_t *mem = slab->slab_memory;
        for (size_t i = 0; i < 64; i++) {
            slab->free_objects[i] = mem + (i * object_size);
        }

        slab->free_count = 64;
        slab->next = mgr->slab_caches;
        mgr->slab_caches = slab;
        g_slab_hash[hash_idx] = slab;  // Cache in hash table
    }

    if (__builtin_expect(slab->free_count == 0, 0)) {
        return fcx_alloc(object_size, 8);
    }

    return slab->free_objects[--slab->free_count];
}

void fcx_slab_free(void *ptr, uint32_t type_hash) {
    size_t hash_idx = type_hash & (FCX_SLAB_HASH_SIZE - 1);
    SlabAllocator *slab = g_slab_hash[hash_idx];

    if (!slab || slab->type_hash != type_hash) {
        FcxMemoryManager *mgr = &g_fcx_memory_manager;
        slab = mgr->slab_caches;
        while (slab && slab->type_hash != type_hash) {
            slab = slab->next;
        }
        if (!slab) { fcx_free(ptr); return; }
    }

    uint8_t *slab_end = slab->slab_memory + (slab->object_size * slab->objects_per_slab);
    if ((uint8_t *)ptr >= slab->slab_memory && (uint8_t *)ptr < slab_end) {
        if (slab->free_count < slab->objects_per_slab) {
            slab->free_objects[slab->free_count++] = ptr;
        }
    } else {
        fcx_free(ptr);
    }
}

void *fcx_pool_alloc(size_t object_size, size_t capacity, bool overflow) {
    (void)capacity; (void)overflow;
    return fcx_alloc(object_size, 8);
}

void fcx_pool_free(void *ptr) { fcx_free(ptr); }

void *fcx_alloc_endian(size_t size, size_t alignment, FcxEndianness endianness) {
    (void)endianness;
    return fcx_alloc(size, alignment);
}

// ============================================================================
// Memory Management Utilities
// ============================================================================

void fcx_coalesce_heap(void) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    BlockHeader *current = (BlockHeader *)mgr->heap_start;

    while ((uint8_t *)current < mgr->heap_end) {
        if (current->magic != FCX_BLOCK_MAGIC) break;

        if (current->is_free) {
            BlockHeader *next = get_next_physical(current, mgr->heap_end);
            if (next && next->is_free) {
                size_t next_sc = get_size_class(next->size);
                remove_free_block_fast(mgr, next, next_sc);
                current->size += sizeof(BlockHeader) + next->size;
                continue;
            }
        }
        current = get_next_physical(current, mgr->heap_end);
        if (!current) break;
    }
}

void fcx_compact_heap(void) { fcx_coalesce_heap(); }

size_t fcx_get_fragmentation(void) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;
    if (mgr->total_allocated == 0) return 0;
    size_t heap_size = mgr->heap_end - mgr->heap_start;
    size_t used = mgr->total_allocated - mgr->total_freed;
    if (used == 0) return 0;
    return ((heap_size - used) * 100) / heap_size;
}

bool fcx_check_leak(void *ptr) {
    if (!ptr) return false;
    BlockHeader *block = (BlockHeader *)((uint8_t *)ptr - sizeof(BlockHeader));
    if (block->magic != FCX_BLOCK_MAGIC) return true;
    return block->is_free == 0;
}

void fcx_memory_shutdown(void) {
    FcxMemoryManager *mgr = &g_fcx_memory_manager;

    ArenaAllocator *arena = mgr->active_arenas;
    while (arena) {
        ArenaAllocator *next = arena->next;
        fcx_free(arena->base);
        fcx_free(arena);
        arena = next;
    }

    SlabAllocator *slab = mgr->slab_caches;
    while (slab) {
        SlabAllocator *next = slab->next;
        fcx_free(slab->free_objects);
        fcx_free(slab->slab_memory);
        fcx_free(slab);
        slab = next;
    }

    // Clear hash tables
    for (int i = 0; i < FCX_SLAB_HASH_SIZE; i++) {
        g_slab_hash[i] = NULL;
    }

    memset(mgr, 0, sizeof(FcxMemoryManager));
}
