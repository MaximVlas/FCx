#ifndef FCX_RUNTIME_H
#define FCX_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

// FCx Comprehensive Runtime System
// Self-contained runtime with advanced memory management, syscalls, atomics, and hardware control

// ============================================================================
// Memory Management System (Task 7.1)
// ============================================================================

// Magic number for block validation
#define FCX_BLOCK_MAGIC 0xDEADBEEF

// Size classes for segregated free lists (32 classes: 8 bytes to 4GB)
#define FCX_SIZE_CLASSES 32

// Block header with doubly-linked list for O(1) coalescing
// OPTIMIZED: Added prev pointer, removed packed for better alignment
typedef struct BlockHeader {
    size_t size;                // Block size in bytes
    uint8_t is_free;            // 1 = free, 0 = in use
    uint8_t has_next;           // 1 = has next block
    uint8_t prev_free;          // 1 = previous block is free (for fast coalesce)
    uint8_t reserved;           // Reserved for future use
    uint32_t magic;             // Debug magic number (0xDEADBEEF)
    struct BlockHeader* next;   // Next free block in size class
    struct BlockHeader* prev;   // Previous free block in size class (O(1) removal)
    struct BlockHeader* phys_prev; // Previous block in physical memory (O(1) coalesce)
} BlockHeader;

// Arena allocator for bump-pointer allocation
// OPTIMIZED: Added direct index table for O(1) lookup
#define FCX_MAX_ARENA_SCOPES 64

typedef struct ArenaAllocator {
    uint8_t* base;              // Arena base pointer
    uint8_t* current;           // Current allocation pointer
    size_t size;                // Total arena size
    size_t remaining;           // Remaining bytes
    uint32_t scope_id;          // Scope identifier for auto-reset
    struct ArenaAllocator* next; // Next arena in stack
} ArenaAllocator;

// Slab allocator for type-specific caches
typedef struct SlabAllocator {
    void** free_objects;        // Free object stack
    uint8_t* slab_memory;       // Slab memory region
    size_t object_size;         // Size of each object
    size_t objects_per_slab;    // Objects per slab
    uint32_t free_count;        // Number of free objects
    uint32_t type_hash;         // Hash of allocated type
    struct SlabAllocator* next; // Next slab cache
} SlabAllocator;

// Pool allocator for fixed-capacity pools
typedef struct PoolAllocator {
    void** pool_objects;        // Fixed pool of objects
    size_t capacity;            // Maximum objects
    size_t available;           // Available objects
    bool overflow_to_heap;      // Allow heap overflow
    struct PoolAllocator* next; // Next pool
} PoolAllocator;

// Endianness types
typedef enum {
    FCX_ENDIAN_LITTLE = 0,
    FCX_ENDIAN_BIG = 1,
    FCX_ENDIAN_NATIVE = 2
} FcxEndianness;

// Main memory manager structure
typedef struct {
    uint8_t* heap_start;        // Start of heap from sys_brk
    uint8_t* heap_end;          // Current heap end
    
    // Segregated free-lists for different allocation strategies
    BlockHeader* size_classes[FCX_SIZE_CLASSES]; // Power-of-2 size classes
    BlockHeader* last_phys_block;   // Last physical block in heap (for coalescing)
    ArenaAllocator* active_arenas;  // Stack of active arena allocators
    ArenaAllocator* arena_table[FCX_MAX_ARENA_SCOPES]; // Direct index table for O(1) arena lookup
    SlabAllocator* slab_caches;     // Type-specific slab caches
    PoolAllocator* fixed_pools;     // Fixed-capacity pools
    
    // Performance tracking and optimization
    uint32_t total_allocated;   // Total bytes allocated
    uint32_t total_freed;       // Total bytes freed
    uint32_t fragmentation_pct; // Current fragmentation percentage
    uint8_t debug_mode;         // Enable safety checks and leak detection
    uint8_t alignment;          // Default alignment (power of 2)
    uint8_t endianness;         // Memory layout endianness
} __attribute__((aligned(64))) FcxMemoryManager; // Cache-line aligned

// Memory allocator functions
void* fcx_alloc(size_t size, size_t alignment);
void fcx_free(void* ptr);
void* fcx_realloc(void* ptr, size_t new_size);

// Operator-centric allocators
void* fcx_arena_alloc(size_t size, size_t alignment, uint32_t scope_id);
void fcx_arena_reset(uint32_t scope_id);
void* fcx_slab_alloc(size_t object_size, uint32_t type_hash);
void fcx_slab_free(void* ptr, uint32_t type_hash);
void* fcx_pool_alloc(size_t object_size, size_t capacity, bool overflow);
void fcx_pool_free(void* ptr);

// Endianness-aware allocation
void* fcx_alloc_endian(size_t size, size_t alignment, FcxEndianness endianness);

// Memory management utilities
void fcx_coalesce_heap(void);
void fcx_compact_heap(void);
size_t fcx_get_fragmentation(void);
bool fcx_check_leak(void* ptr);

// Initialize memory manager
int fcx_memory_init(void);
void fcx_memory_shutdown(void);

// ============================================================================
// Syscall Interface (Task 7.2)
// ============================================================================

// Syscall numbers (Linux x86_64)
#define FCX_SYS_READ    0
#define FCX_SYS_WRITE   1
#define FCX_SYS_OPEN    2
#define FCX_SYS_CLOSE   3
#define FCX_SYS_BRK     12
#define FCX_SYS_EXIT    60
#define FCX_SYS_MMAP    9
#define FCX_SYS_MUNMAP  11

// Raw syscall interface (sys% operator)
long fcx_syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);

// Compact syscall operators
long fcx_write_op(int fd, const void* buf, size_t count);  // $/ operator
long fcx_read_op(int fd, void* buf, size_t count);         // /$ operator

// Higher-level syscall wrappers
int fcx_sys_open(const char* path, int flags, int mode);
int fcx_sys_close(int fd);
void* fcx_sys_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int fcx_sys_munmap(void* addr, size_t length);
void fcx_sys_exit(int status);

// Error handling with ?! operator
typedef struct {
    long value;
    int error;
} FcxSyscallResult;

FcxSyscallResult fcx_syscall_checked(long num, long arg1, long arg2, long arg3);

// ============================================================================
// Atomic Operations and Concurrency (Task 7.3)
// ============================================================================

// Atomic operations (! family operators)
uint64_t fcx_atomic_load(volatile uint64_t* ptr);           // ! operator
void fcx_atomic_store(volatile uint64_t* ptr, uint64_t val); // !! operator
uint64_t fcx_atomic_swap(volatile uint64_t* ptr, uint64_t val); // <==> operator
bool fcx_atomic_cas(volatile uint64_t* ptr, uint64_t expected, uint64_t new_val); // <=> operator

// Memory barriers
void fcx_barrier_full(void);      // !=> operator (MFENCE)
void fcx_barrier_load(void);      // !> operator (LFENCE)
void fcx_barrier_store(void);     // !< operator (SFENCE)

// Atomic arithmetic
uint64_t fcx_atomic_add(volatile uint64_t* ptr, uint64_t val);  // ?!! operator
uint64_t fcx_atomic_xor(volatile uint64_t* ptr, uint64_t val);  // ~! operator
uint64_t fcx_atomic_inc(volatile uint64_t* ptr);
uint64_t fcx_atomic_dec(volatile uint64_t* ptr);
bool fcx_atomic_test_and_set(volatile uint64_t* ptr);
void fcx_atomic_clear(volatile uint64_t* ptr);

// Volatile semantics for MMIO
uint64_t fcx_volatile_load(volatile uint64_t* ptr);   // >< operator (load)
void fcx_volatile_store(volatile uint64_t* ptr, uint64_t val); // >< operator (store)

// ============================================================================
// MMIO and Hardware Control (Task 7.4)
// ============================================================================

// MMIO operations
void* fcx_mmio_map(uint64_t physical_address, size_t size);  // @> operator
void fcx_mmio_unmap(void* address, size_t size);             // <@ operator

// Stack manipulation
void* fcx_stack_alloc_dynamic(size_t size);  // stack> operator
void fcx_stack_free_dynamic(void* ptr, size_t size);

// CPU feature detection
typedef struct {
    uint64_t features;          // Bit-packed feature flags
    uint16_t vector_width;      // Preferred vector width (128, 256, 512)
    uint8_t cache_line_size;    // L1 cache line size
    uint8_t red_zone_size;      // Available red zone (0-128 bytes)
    uint8_t alignment_pref;     // Preferred stack alignment
} CpuFeatures;

// CPU feature flags
#define CPU_FEATURE_SSE2        (1ULL << 0)
#define CPU_FEATURE_AVX2        (1ULL << 15)
#define CPU_FEATURE_AVX512F     (1ULL << 30)
#define CPU_FEATURE_BMI2        (1ULL << 25)

CpuFeatures fcx_detect_cpu_features(void);
bool fcx_has_feature(uint64_t feature);
void fcx_get_cpu_vendor(char vendor[13]);
void fcx_get_cpu_model(char model[49]);

// Stack pointer access
void* fcx_get_stack_pointer(void);
void* fcx_get_frame_pointer(void);

// Performance monitoring
uint64_t fcx_rdtsc(void);
uint64_t fcx_rdtscp(void);
void fcx_pause(void);
void fcx_prefetch(const void* addr);
void fcx_prefetch_write(const void* addr);

// ============================================================================
// Runtime Initialization and Utilities
// ============================================================================

// Initialize entire runtime system
int fcx_runtime_init(void);
void fcx_runtime_shutdown(void);

// Error handling
void fcx_panic(const char* message);
void fcx_assert(bool condition, const char* message);

// Global memory manager instance
extern FcxMemoryManager g_fcx_memory_manager;

// ============================================================================
// Performance and Utility Functions
// ============================================================================

// Read timestamp counter
uint64_t fcx_rdtsc(void);
uint64_t fcx_rdtscp(void);

// Pause instruction for spin loops
void fcx_pause(void);

// Prefetch operations
void fcx_prefetch(const void* addr);
void fcx_prefetch_write(const void* addr);

// Cache control
void fcx_clflush(const void* addr);
void fcx_clflushopt(const void* addr);
void fcx_clwb(const void* addr);

// Adaptive memory operations
void fcx_memcpy_adaptive(void* dest, const void* src, size_t n);
void fcx_memset_adaptive(void* dest, int value, size_t n);

// Utility functions
size_t fcx_strlen(const char* str);
int fcx_strcmp(const char* s1, const char* s2);
char* fcx_strcpy(char* dest, const char* src);
void* fcx_memcpy(void* dest, const void* src, size_t n);
void* fcx_memset(void* dest, int value, size_t n);
int fcx_memcmp(const void* s1, const void* s2, size_t n);

// Debug and diagnostics
void fcx_print_int(int64_t value);
void fcx_print_hex(uint64_t value);
void fcx_print_str(const char* str);
void fcx_print_newline(void);
void fcx_print_memory_stats(void);
void fcx_print_cpu_features(void);

// Print functions for stdout (used by print> operator)
void _fcx_print_func(const char* str);
void _fcx_print_int(int64_t value);
void _fcx_println(const char* str);
void _fcx_println_int(int64_t value);

// Benchmark utilities
typedef void (*BenchmarkFunc)(void);
void fcx_benchmark(const char* name, BenchmarkFunc func, int iterations);

// ============================================================================
// High-Precision Timing (fcx_timing.c)
// ============================================================================

// Get current time in various units (monotonic clock)
int64_t fcx_time_ns(void);   // Nanoseconds
int64_t fcx_time_us(void);   // Microseconds
int64_t fcx_time_ms(void);   // Milliseconds
uint64_t fcx_cycles(void);   // CPU cycles (rdtscp)

// Timer management (up to 16 concurrent timers)
int64_t fcx_timer_start(void);                    // Start timer, returns ID
int64_t fcx_timer_stop_ns(int64_t timer_id);      // Stop and get nanoseconds
int64_t fcx_timer_stop_us(int64_t timer_id);      // Stop and get microseconds
int64_t fcx_timer_stop_ms(int64_t timer_id);      // Stop and get milliseconds
int64_t fcx_timer_stop_cycles(int64_t timer_id);  // Stop and get CPU cycles
int64_t fcx_timer_elapsed_ns(int64_t timer_id);   // Peek elapsed (no stop)
void fcx_timer_reset(int64_t timer_id);           // Reset timer

// Simple tick/tock timing (single global timer per thread)
void fcx_tick(void);          // Start timing
int64_t fcx_tock_ns(void);    // Get elapsed nanoseconds
int64_t fcx_tock_us(void);    // Get elapsed microseconds
int64_t fcx_tock_ms(void);    // Get elapsed milliseconds
int64_t fcx_tock_cycles(void); // Get elapsed CPU cycles

// Print timing with auto-formatting
void fcx_print_timing(const char* label, int64_t ns);

// FCx runtime exports
int64_t _fcx_time_ns(void);
int64_t _fcx_time_us(void);
int64_t _fcx_time_ms(void);
int64_t _fcx_cycles(void);
int64_t _fcx_timer_start(void);
int64_t _fcx_timer_stop_ns(int64_t id);
int64_t _fcx_timer_stop_us(int64_t id);
int64_t _fcx_timer_stop_ms(int64_t id);
int64_t _fcx_timer_stop_cycles(int64_t id);
int64_t _fcx_timer_elapsed_ns(int64_t id);
void _fcx_timer_reset(int64_t id);
void _fcx_tick(void);
int64_t _fcx_tock_ns(void);
int64_t _fcx_tock_us(void);
int64_t _fcx_tock_ms(void);
int64_t _fcx_tock_cycles(void);
void _fcx_print_timing(const char* label, int64_t ns);

#endif // FCX_RUNTIME_H
