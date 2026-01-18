#include "fcx_runtime.h"

// Atomic load (! operator)
// Maps to simple MOV for aligned access, or LOCK MOV for unaligned
uint64_t fcx_atomic_load(volatile uint64_t* ptr) {
    uint64_t value;
    
    // Check alignment
    if (((uintptr_t)ptr & 7) == 0) {
        // Aligned access - simple MOV with compiler barrier
        __asm__ volatile(
            "movq (%1), %0"
            : "=r"(value)
            : "r"(ptr)
            : "memory"
        );
    } else {
        // Unaligned access - use atomic instruction
        __asm__ volatile(
            "lock; movq (%1), %0"
            : "=r"(value)
            : "r"(ptr)
            : "memory"
        );
    }
    
    return value;
}

// Atomic store (!! operator)
// Maps to LOCK XCHG for guaranteed atomicity
void fcx_atomic_store(volatile uint64_t* ptr, uint64_t val) {
    __asm__ volatile(
        "lock; xchgq %0, (%1)"
        : "+r"(val)
        : "r"(ptr)
        : "memory"
    );
}

// Atomic swap (<==> operator)
// Maps directly to LOCK XCHG
uint64_t fcx_atomic_swap(volatile uint64_t* ptr, uint64_t val) {
    uint64_t old_val;
    
    __asm__ volatile(
        "lock; xchgq %0, (%2)"
        : "=r"(old_val), "+m"(*ptr)
        : "r"(ptr), "0"(val)
        : "memory"
    );
    
    return old_val;
}

// Compare-and-swap (<=> operator)
// Maps to LOCK CMPXCHG
bool fcx_atomic_cas(volatile uint64_t* ptr, uint64_t expected, uint64_t new_val) {
    uint64_t prev = expected;
    
    __asm__ volatile(
        "lock; cmpxchgq %2, %1"
        : "=a"(prev), "+m"(*ptr)
        : "r"(new_val), "0"(prev)
        : "memory", "cc"
    );
    
    return prev == expected;
}

// ============================================================================
// Memory Barriers
// ============================================================================

// Full memory barrier (!=> operator)
// Maps to MFENCE
void fcx_barrier_full(void) {
    __asm__ volatile("mfence" ::: "memory");
}

// Load fence (!> operator)
// Maps to LFENCE
void fcx_barrier_load(void) {
    __asm__ volatile("lfence" ::: "memory");
}

// Store fence (!< operator)
// Maps to SFENCE
void fcx_barrier_store(void) {
    __asm__ volatile("sfence" ::: "memory");
}

// ============================================================================
// Atomic Arithmetic Operations
// ============================================================================

// Atomic add with fence (?!! operator)
// Maps to LOCK XADD
uint64_t fcx_atomic_add(volatile uint64_t* ptr, uint64_t val) {
    uint64_t result = val;
    
    __asm__ volatile(
        "lock; xaddq %0, (%2)"
        : "+r"(result), "+m"(*ptr)
        : "r"(ptr)
        : "memory", "cc"
    );
    
    return result; // Returns old value
}

// Atomic XOR (~! operator)
// Implemented using CAS loop
uint64_t fcx_atomic_xor(volatile uint64_t* ptr, uint64_t val) {
    uint64_t old_val, new_val;
    
    do {
        old_val = fcx_atomic_load(ptr);
        new_val = old_val ^ val;
    } while (!fcx_atomic_cas(ptr, old_val, new_val));
    
    return old_val;
}

// Atomic subtract
uint64_t fcx_atomic_sub(volatile uint64_t* ptr, uint64_t val) {
    return fcx_atomic_add(ptr, (uint64_t)(-(int64_t)val));
}

// Atomic AND
uint64_t fcx_atomic_and(volatile uint64_t* ptr, uint64_t val) {
    uint64_t old_val, new_val;
    
    do {
        old_val = fcx_atomic_load(ptr);
        new_val = old_val & val;
    } while (!fcx_atomic_cas(ptr, old_val, new_val));
    
    return old_val;
}

// Atomic OR
uint64_t fcx_atomic_or(volatile uint64_t* ptr, uint64_t val) {
    uint64_t old_val, new_val;
    
    do {
        old_val = fcx_atomic_load(ptr);
        new_val = old_val | val;
    } while (!fcx_atomic_cas(ptr, old_val, new_val));
    
    return old_val;
}

// ============================================================================
// Volatile Semantics for MMIO (>< operator)
// ============================================================================

// Volatile load (>< operator for load)
// Prevents compiler reordering and optimization
uint64_t fcx_volatile_load(volatile uint64_t* ptr) {
    uint64_t value;
    
    __asm__ volatile(
        "movq (%1), %0"
        : "=r"(value)
        : "r"(ptr)
        : "memory"
    );
    
    return value;
}

// Volatile store (>< operator for store)
// Prevents compiler reordering and optimization
void fcx_volatile_store(volatile uint64_t* ptr, uint64_t val) {
    __asm__ volatile(
        "movq %1, (%0)"
        :
        : "r"(ptr), "r"(val)
        : "memory"
    );
}

// ============================================================================
// Additional Atomic Operations
// ============================================================================

// Atomic increment
uint64_t fcx_atomic_inc(volatile uint64_t* ptr) {
    return fcx_atomic_add(ptr, 1);
}

// Atomic decrement
uint64_t fcx_atomic_dec(volatile uint64_t* ptr) {
    return fcx_atomic_sub(ptr, 1);
}

// Atomic test-and-set (for spinlocks)
bool fcx_atomic_test_and_set(volatile uint64_t* ptr) {
    return fcx_atomic_swap(ptr, 1) == 0;
}

// Atomic clear (for spinlocks)
void fcx_atomic_clear(volatile uint64_t* ptr) {
    fcx_atomic_store(ptr, 0);
}

// ============================================================================
// Spinlock Implementation
// ============================================================================

typedef struct {
    volatile uint64_t lock;
} FcxSpinlock;

void fcx_spinlock_init(FcxSpinlock* lock) {
    lock->lock = 0;
}

void fcx_spinlock_acquire(FcxSpinlock* lock) {
    while (!fcx_atomic_test_and_set(&lock->lock)) {
        // Spin with pause instruction to reduce contention
        __asm__ volatile("pause" ::: "memory");
    }
}

void fcx_spinlock_release(FcxSpinlock* lock) {
    fcx_atomic_clear(&lock->lock);
}

bool fcx_spinlock_try_acquire(FcxSpinlock* lock) {
    return fcx_atomic_test_and_set(&lock->lock);
}

// ============================================================================
// Thread Synchronization Primitives (Stubs)
// ============================================================================

// These would be fully implemented with futex syscalls

typedef struct {
    volatile uint64_t value;
} FcxSemaphore;

void fcx_semaphore_init(FcxSemaphore* sem, uint64_t initial) {
    sem->value = initial;
}

void fcx_semaphore_wait(FcxSemaphore* sem) {
    uint64_t old_val;
    
    do {
        old_val = fcx_atomic_load(&sem->value);
        if (old_val == 0) {
            // Would use futex_wait here
            __asm__ volatile("pause" ::: "memory");
            continue;
        }
    } while (!fcx_atomic_cas(&sem->value, old_val, old_val - 1));
}

void fcx_semaphore_post(FcxSemaphore* sem) {
    fcx_atomic_inc(&sem->value);
    // Would use futex_wake here
}

// ============================================================================
// Memory Ordering Variants
// ============================================================================

// Relaxed atomic operations (no ordering guarantees)
uint64_t fcx_atomic_load_relaxed(volatile uint64_t* ptr) {
    return *ptr; // Simple load, no barriers
}

void fcx_atomic_store_relaxed(volatile uint64_t* ptr, uint64_t val) {
    *ptr = val; // Simple store, no barriers
}

// Acquire semantics (loads after this cannot be reordered before)
uint64_t fcx_atomic_load_acquire(volatile uint64_t* ptr) {
    uint64_t value = fcx_atomic_load(ptr);
    fcx_barrier_load();
    return value;
}

// Release semantics (stores before this cannot be reordered after)
void fcx_atomic_store_release(volatile uint64_t* ptr, uint64_t val) {
    fcx_barrier_store();
    fcx_atomic_store(ptr, val);
}

// Sequential consistency (full ordering)
uint64_t fcx_atomic_load_seq_cst(volatile uint64_t* ptr) {
    fcx_barrier_full();
    uint64_t value = fcx_atomic_load(ptr);
    fcx_barrier_full();
    return value;
}

void fcx_atomic_store_seq_cst(volatile uint64_t* ptr, uint64_t val) {
    fcx_barrier_full();
    fcx_atomic_store(ptr, val);
    fcx_barrier_full();
}
