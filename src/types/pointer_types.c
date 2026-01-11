#include "pointer_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// FCx Three-Pointer Type System Implementation
// Resolves the fundamental tension between safety and performance
// 1. Handles (4 bytes) - Opaque resource identifiers
// 2. Typed Pointers (8 bytes) - Native 64-bit virtual addresses with type information  
// 3. Raw Pointers (8 bytes) - Untyped 64-bit virtual addresses

// Global type registry for type system integration
static TypeInfo *type_registry = NULL;
static size_t type_registry_size = 0;
static size_t type_registry_capacity = 0;

// Pointer debugging state
static bool pointer_debugging_enabled = false;

// Memory tracking for safety features (unused for now)
// static void **tracked_addresses = NULL;
// static size_t tracked_count = 0;
// static size_t tracked_capacity = 0;

// Handle operations
TypedHandle create_handle(Handle value, HandleType type) {
    TypedHandle handle = {value, type, true};
    return handle;
}

bool is_valid_handle(const TypedHandle *handle) {
    return handle && handle->is_valid && handle->type != HANDLE_INVALID;
}

bool handles_equal(const TypedHandle *a, const TypedHandle *b) {
    return a && b && a->value == b->value && a->type == b->type;
}

void invalidate_handle(TypedHandle *handle) {
    if (handle) {
        handle->is_valid = false;
        handle->type = HANDLE_INVALID;
    }
}

// Typed pointer operations
TypedPointer create_typed_pointer(void *address, uint32_t type_id, uint32_t flags) {
    TypedPointer ptr = {address, type_id, flags};
    return ptr;
}

bool is_valid_typed_pointer(const TypedPointer *ptr) {
    return ptr && ptr->address != NULL;
}

void *get_typed_pointer_address(const TypedPointer *ptr) {
    return ptr ? ptr->address : NULL;
}

uint32_t get_typed_pointer_type(const TypedPointer *ptr) {
    return ptr ? ptr->type_id : 0;
}

bool typed_pointers_compatible(const TypedPointer *a, const TypedPointer *b) {
    return a && b && a->type_id == b->type_id;
}

// Raw pointer operations
RawPointer create_raw_pointer(void *address, uint32_t size, uint32_t flags) {
    RawPointer ptr = {address, size, flags};
    return ptr;
}

bool is_valid_raw_pointer(const RawPointer *ptr) {
    return ptr && ptr->address != NULL;
}

void *get_raw_pointer_address(const RawPointer *ptr) {
    return ptr ? ptr->address : NULL;
}

uint32_t get_raw_pointer_size(const RawPointer *ptr) {
    return ptr ? ptr->size : 0;
}

// Stub implementations for other functions
PointerConversionResult handle_to_typed_pointer(const TypedHandle *handle, TypedPointer *result) {
    (void)handle; (void)result;
    return PTR_CONV_SUCCESS;
}

PointerConversionResult typed_pointer_to_raw_pointer(const TypedPointer *typed, RawPointer *result) {
    if (!typed || !result) return PTR_CONV_NULL_POINTER;
    *result = create_raw_pointer(typed->address, 0, RAW_FLAG_READABLE | RAW_FLAG_WRITABLE);
    return PTR_CONV_SUCCESS;
}

PointerConversionResult raw_pointer_to_typed_pointer(const RawPointer *raw, uint32_t type_id, TypedPointer *result) {
    if (!raw || !result) return PTR_CONV_NULL_POINTER;
    *result = create_typed_pointer(raw->address, type_id, PTR_FLAG_ALIGNED);
    return PTR_CONV_SUCCESS;
}

// Pointer arithmetic (type-aware)
TypedPointer typed_pointer_add(const TypedPointer *ptr, int64_t offset, const TypeInfo *type_info) {
    if (!ptr || !type_info) {
        return create_typed_pointer(NULL, 0, 0);
    }
    
    void *new_address = (char*)ptr->address + (offset * type_info->size);
    return create_typed_pointer(new_address, ptr->type_id, ptr->flags);
}

TypedPointer typed_pointer_sub(const TypedPointer *ptr, int64_t offset, const TypeInfo *type_info) {
    if (!ptr || !type_info) {
        return create_typed_pointer(NULL, 0, 0);
    }
    
    void *new_address = (char*)ptr->address - (offset * type_info->size);
    return create_typed_pointer(new_address, ptr->type_id, ptr->flags);
}

int64_t typed_pointer_diff(const TypedPointer *a, const TypedPointer *b, const TypeInfo *type_info) {
    if (!a || !b || !type_info || type_info->size == 0) {
        return 0;
    }
    
    ptrdiff_t byte_diff = (char*)a->address - (char*)b->address;
    return byte_diff / (int64_t)type_info->size;
}

// Raw pointer arithmetic (byte-wise)
RawPointer raw_pointer_add(const RawPointer *ptr, int64_t byte_offset) {
    if (!ptr) {
        return create_raw_pointer(NULL, 0, 0);
    }
    
    void *new_address = (char*)ptr->address + byte_offset;
    return create_raw_pointer(new_address, ptr->size, ptr->flags);
}

RawPointer raw_pointer_sub(const RawPointer *ptr, int64_t byte_offset) {
    if (!ptr) {
        return create_raw_pointer(NULL, 0, 0);
    }
    
    void *new_address = (char*)ptr->address - byte_offset;
    return create_raw_pointer(new_address, ptr->size, ptr->flags);
}

int64_t raw_pointer_diff(const RawPointer *a, const RawPointer *b) {
    if (!a || !b) {
        return 0;
    }
    
    return (char*)a->address - (char*)b->address;
}

// Memory access operations
bool typed_pointer_read(const TypedPointer *ptr, void *dest, size_t size) {
    if (!ptr || !dest || !ptr->address) {
        return false;
    }
    
    if (pointer_debugging_enabled) {
        log_pointer_operation("typed_read", ptr->address);
    }
    
    memcpy(dest, ptr->address, size);
    return true;
}

bool typed_pointer_write(const TypedPointer *ptr, const void *src, size_t size) {
    if (!ptr || !src || !ptr->address) {
        return false;
    }
    
    if (pointer_debugging_enabled) {
        log_pointer_operation("typed_write", ptr->address);
    }
    
    memcpy(ptr->address, src, size);
    return true;
}

bool raw_pointer_read(const RawPointer *ptr, void *dest, size_t size) {
    if (!ptr || !dest || !ptr->address) {
        return false;
    }
    
    if (!(ptr->flags & RAW_FLAG_READABLE)) {
        return false;
    }
    
    if (pointer_debugging_enabled) {
        log_pointer_operation("raw_read", ptr->address);
    }
    
    memcpy(dest, ptr->address, size);
    return true;
}

bool raw_pointer_write(const RawPointer *ptr, const void *src, size_t size) {
    if (!ptr || !src || !ptr->address) {
        return false;
    }
    
    if (!(ptr->flags & RAW_FLAG_WRITABLE)) {
        return false;
    }
    
    if (pointer_debugging_enabled) {
        log_pointer_operation("raw_write", ptr->address);
    }
    
    memcpy(ptr->address, src, size);
    return true;
}

// Atomic operations on pointers (stubs for now)
bool atomic_typed_pointer_read(const TypedPointer *ptr, void *dest, size_t size) {
    // In a real implementation, this would use atomic instructions
    return typed_pointer_read(ptr, dest, size);
}

bool atomic_typed_pointer_write(const TypedPointer *ptr, const void *src, size_t size) {
    // In a real implementation, this would use atomic instructions
    return typed_pointer_write(ptr, src, size);
}

bool atomic_typed_pointer_cas(const TypedPointer *ptr, void *expected, const void *new_value, size_t size) {
    // Stub implementation - would use CMPXCHG in real version
    (void)ptr; (void)expected; (void)new_value; (void)size;
    return false;
}

bool atomic_typed_pointer_swap(const TypedPointer *ptr, void *value, size_t size) {
    // Stub implementation - would use XCHG in real version
    (void)ptr; (void)value; (void)size;
    return false;
}

// Volatile operations for MMIO
bool volatile_raw_pointer_read(const RawPointer *ptr, void *dest, size_t size) {
    if (!ptr || !dest || !ptr->address) {
        return false;
    }
    
    if (!(ptr->flags & RAW_FLAG_MMIO)) {
        return false;
    }
    
    // Volatile read - prevent compiler optimization
    volatile char *src = (volatile char*)ptr->address;
    char *dst = (char*)dest;
    
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    return true;
}

bool volatile_raw_pointer_write(const RawPointer *ptr, const void *src, size_t size) {
    if (!ptr || !src || !ptr->address) {
        return false;
    }
    
    if (!(ptr->flags & RAW_FLAG_MMIO)) {
        return false;
    }
    
    // Volatile write - prevent compiler optimization
    volatile char *dst = (volatile char*)ptr->address;
    const char *source = (const char*)src;
    
    for (size_t i = 0; i < size; i++) {
        dst[i] = source[i];
    }
    
    return true;
}

// Alignment and bounds checking
bool is_aligned(const void *address, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false; // alignment must be power of 2
    }
    
    uintptr_t addr = (uintptr_t)address;
    return (addr & (alignment - 1)) == 0;
}

bool check_bounds(const void *address, size_t size, const void *base, size_t base_size) {
    if (!address || !base) {
        return false;
    }
    
    uintptr_t addr = (uintptr_t)address;
    uintptr_t base_addr = (uintptr_t)base;
    
    return (addr >= base_addr) && (addr + size <= base_addr + base_size);
}

size_t calculate_alignment(const void *address) {
    if (!address) {
        return 1;
    }
    
    uintptr_t addr = (uintptr_t)address;
    if (addr == 0) {
        return SIZE_MAX; // NULL is aligned to everything
    }
    
    // Find the largest power of 2 that divides the address
    size_t alignment = 1;
    while ((addr & alignment) == 0 && alignment <= 4096) {
        alignment <<= 1;
    }
    
    return alignment >> 1;
}

// Type system integration
void register_type_info(uint32_t type_id, const TypeInfo *info) {
    if (!info) {
        return;
    }
    
    // Expand registry if needed
    if (type_registry_size >= type_registry_capacity) {
        size_t new_capacity = type_registry_capacity ? type_registry_capacity * 2 : 16;
        TypeInfo *new_registry = realloc(type_registry, new_capacity * sizeof(TypeInfo));
        if (!new_registry) {
            return;
        }
        type_registry = new_registry;
        type_registry_capacity = new_capacity;
    }
    
    // Find existing entry or add new one
    for (size_t i = 0; i < type_registry_size; i++) {
        if (type_registry[i].type_id == type_id) {
            type_registry[i] = *info;
            return;
        }
    }
    
    // Add new entry
    type_registry[type_registry_size] = *info;
    type_registry[type_registry_size].type_id = type_id;
    type_registry_size++;
}

const TypeInfo *get_type_info(uint32_t type_id) {
    for (size_t i = 0; i < type_registry_size; i++) {
        if (type_registry[i].type_id == type_id) {
            return &type_registry[i];
        }
    }
    return NULL;
}

uint32_t get_type_id_by_name(const char *type_name) {
    if (!type_name) {
        return 0;
    }
    
    for (size_t i = 0; i < type_registry_size; i++) {
        if (type_registry[i].type_name && strcmp(type_registry[i].type_name, type_name) == 0) {
            return type_registry[i].type_id;
        }
    }
    return 0;
}

// Compiler enforcement functions
bool can_dereference_handle(const TypedHandle *handle) {
    // Handles cannot be dereferenced - this is enforced by the compiler
    (void)handle;
    return false;
}

bool can_dereference_typed_pointer(const TypedPointer *ptr) {
    return ptr && ptr->address != NULL;
}

bool can_dereference_raw_pointer(const RawPointer *ptr) {
    // Raw pointers cannot be dereferenced directly - must cast to typed pointer first
    (void)ptr;
    return false;
}

bool requires_explicit_cast(const void *from_ptr, const void *to_ptr) {
    // All pointer conversions in FCx require explicit casts
    (void)from_ptr; (void)to_ptr;
    return true;
}

// Syscall interface requirements
bool is_syscall_compatible_pointer(const RawPointer *ptr) {
    return ptr && (ptr->flags & RAW_FLAG_SYSCALL);
}

RawPointer prepare_syscall_pointer(const TypedPointer *typed) {
    if (!typed) {
        return create_raw_pointer(NULL, 0, 0);
    }
    
    return create_raw_pointer(typed->address, 0, RAW_FLAG_SYSCALL | RAW_FLAG_READABLE | RAW_FLAG_WRITABLE);
}

bool validate_syscall_parameters(const RawPointer *ptrs, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!is_syscall_compatible_pointer(&ptrs[i])) {
            return false;
        }
    }
    return true;
}

// Debug and safety features
void enable_pointer_debugging(bool enable) {
    pointer_debugging_enabled = enable;
}

void log_pointer_operation(const char *operation, const void *ptr) {
    if (pointer_debugging_enabled) {
        printf("[FCX DEBUG] %s: %p\n", operation, ptr);
    }
}

bool detect_use_after_free(const void *address) {
    // Stub implementation - would track freed addresses in real version
    (void)address;
    return false;
}

bool detect_double_free(const void *address) {
    // Stub implementation - would track freed addresses in real version
    (void)address;
    return false;
}

// Memory layout and MMIO support
RawPointer map_mmio_address(uint64_t physical_address, size_t size) {
    // In a real implementation, this would use mmap or similar
    void *mapped = (void*)physical_address; // Direct mapping for now
    return create_raw_pointer(mapped, (uint32_t)size, RAW_FLAG_MMIO | RAW_FLAG_READABLE | RAW_FLAG_WRITABLE);
}

bool unmap_mmio_address(const RawPointer *mmio_ptr) {
    // In a real implementation, this would use munmap
    return mmio_ptr && (mmio_ptr->flags & RAW_FLAG_MMIO);
}

bool is_mmio_address(const void *address) {
    // Stub implementation - would check against known MMIO ranges
    (void)address;
    return false;
}

// Stack allocation support
TypedPointer allocate_stack_memory(size_t size, size_t alignment, uint32_t type_id) {
    // Stub implementation - would manipulate stack pointer in real version
    (void)alignment; // Unused for now
    void *stack_mem = malloc(size); // Using malloc as placeholder
    return create_typed_pointer(stack_mem, type_id, PTR_FLAG_STACK | PTR_FLAG_ALIGNED);
}

bool free_stack_memory(const TypedPointer *ptr) {
    // Stub implementation - would adjust stack pointer in real version
    if (ptr && (ptr->flags & PTR_FLAG_STACK)) {
        free(ptr->address); // Using free as placeholder
        return true;
    }
    return false;
}

// Operator handler registry (stub - unused for now)
// static PointerOperatorHandler *operator_handlers = NULL;
// static size_t handler_count = 0;

void register_pointer_operator(const PointerOperatorHandler *handler) {
    // Stub implementation
    (void)handler;
}

bool execute_pointer_operator(const char *operator_symbol, const void *ptr, void *result) {
    // Stub implementation
    (void)operator_symbol; (void)ptr; (void)result;
    return false;
}

