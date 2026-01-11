#ifndef FCX_POINTER_TYPES_H
#define FCX_POINTER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Three-pointer type system for FCx
// Resolves the fundamental tension between safety and performance

// 1. Handles (4 bytes) - Opaque resource identifiers
typedef uint32_t Handle;        // File descriptors, resource IDs, array indices
typedef int32_t FileHandle;     // Signed for error checking (-1 = invalid)

// Handle type information
typedef enum {
    HANDLE_FILE,        // File descriptor
    HANDLE_RESOURCE,    // Resource ID
    HANDLE_ARRAY_INDEX, // Array index
    HANDLE_THREAD,      // Thread handle
    HANDLE_SOCKET,      // Socket handle
    HANDLE_INVALID      // Invalid handle
} HandleType;

// Handle validation and operations
typedef struct {
    Handle value;
    HandleType type;
    bool is_valid;
} TypedHandle;

// 2. Typed Pointers (8 bytes) - Native 64-bit virtual addresses with type information
typedef struct {
    void *address;      // Native 64-bit pointer
    uint32_t type_id;   // Type identifier for safety
    uint32_t flags;     // Pointer flags (alignment, volatility, etc.)
} TypedPointer;

// Typed pointer flags
#define PTR_FLAG_VOLATILE   0x01    // Volatile access required
#define PTR_FLAG_ALIGNED    0x02    // Guaranteed alignment
#define PTR_FLAG_NO_ALIAS   0x04    // No aliasing hint for optimization
#define PTR_FLAG_ATOMIC     0x08    // Atomic access required
#define PTR_FLAG_MMIO       0x10    // Memory-mapped I/O
#define PTR_FLAG_STACK      0x20    // Stack-allocated memory
#define PTR_FLAG_HEAP       0x40    // Heap-allocated memory

// 3. Raw Pointers (8 bytes) - Untyped 64-bit virtual addresses
typedef struct {
    void *address;      // Raw 64-bit address
    uint32_t size;      // Optional size hint
    uint32_t flags;     // Access flags
} RawPointer;

// Raw pointer flags
#define RAW_FLAG_READABLE   0x01    // Memory is readable
#define RAW_FLAG_WRITABLE   0x02    // Memory is writable
#define RAW_FLAG_EXECUTABLE 0x04    // Memory is executable
#define RAW_FLAG_MMIO       0x08    // Memory-mapped I/O region
#define RAW_FLAG_SYSCALL    0x10    // Used for syscall parameters

// Pointer conversion and validation
typedef enum {
    PTR_CONV_SUCCESS,
    PTR_CONV_TYPE_MISMATCH,
    PTR_CONV_NULL_POINTER,
    PTR_CONV_INVALID_HANDLE,
    PTR_CONV_ALIGNMENT_ERROR,
    PTR_CONV_BOUNDS_ERROR
} PointerConversionResult;

// Type system integration
typedef struct {
    uint32_t type_id;
    const char *type_name;
    size_t size;
    size_t alignment;
    bool is_primitive;
    bool is_pointer;
    bool is_atomic_safe;
} TypeInfo;

// Function declarations

// Handle operations
TypedHandle create_handle(Handle value, HandleType type);
bool is_valid_handle(const TypedHandle *handle);
bool handles_equal(const TypedHandle *a, const TypedHandle *b);
void invalidate_handle(TypedHandle *handle);

// Typed pointer operations
TypedPointer create_typed_pointer(void *address, uint32_t type_id, uint32_t flags);
bool is_valid_typed_pointer(const TypedPointer *ptr);
void *get_typed_pointer_address(const TypedPointer *ptr);
uint32_t get_typed_pointer_type(const TypedPointer *ptr);
bool typed_pointers_compatible(const TypedPointer *a, const TypedPointer *b);

// Raw pointer operations
RawPointer create_raw_pointer(void *address, uint32_t size, uint32_t flags);
bool is_valid_raw_pointer(const RawPointer *ptr);
void *get_raw_pointer_address(const RawPointer *ptr);
uint32_t get_raw_pointer_size(const RawPointer *ptr);

// Pointer conversions (explicit casts required by FCx type system)
PointerConversionResult handle_to_typed_pointer(const TypedHandle *handle, TypedPointer *result);
PointerConversionResult typed_pointer_to_raw_pointer(const TypedPointer *typed, RawPointer *result);
PointerConversionResult raw_pointer_to_typed_pointer(const RawPointer *raw, uint32_t type_id, TypedPointer *result);

// Pointer arithmetic (type-aware)
TypedPointer typed_pointer_add(const TypedPointer *ptr, int64_t offset, const TypeInfo *type_info);
TypedPointer typed_pointer_sub(const TypedPointer *ptr, int64_t offset, const TypeInfo *type_info);
int64_t typed_pointer_diff(const TypedPointer *a, const TypedPointer *b, const TypeInfo *type_info);

// Raw pointer arithmetic (byte-wise)
RawPointer raw_pointer_add(const RawPointer *ptr, int64_t byte_offset);
RawPointer raw_pointer_sub(const RawPointer *ptr, int64_t byte_offset);
int64_t raw_pointer_diff(const RawPointer *a, const RawPointer *b);

// Memory access operations
bool typed_pointer_read(const TypedPointer *ptr, void *dest, size_t size);
bool typed_pointer_write(const TypedPointer *ptr, const void *src, size_t size);
bool raw_pointer_read(const RawPointer *ptr, void *dest, size_t size);
bool raw_pointer_write(const RawPointer *ptr, const void *src, size_t size);

// Atomic operations on pointers
bool atomic_typed_pointer_read(const TypedPointer *ptr, void *dest, size_t size);
bool atomic_typed_pointer_write(const TypedPointer *ptr, const void *src, size_t size);
bool atomic_typed_pointer_cas(const TypedPointer *ptr, void *expected, const void *new_value, size_t size);
bool atomic_typed_pointer_swap(const TypedPointer *ptr, void *value, size_t size);

// Volatile operations for MMIO
bool volatile_raw_pointer_read(const RawPointer *ptr, void *dest, size_t size);
bool volatile_raw_pointer_write(const RawPointer *ptr, const void *src, size_t size);

// Alignment and bounds checking
bool is_aligned(const void *address, size_t alignment);
bool check_bounds(const void *address, size_t size, const void *base, size_t base_size);
size_t calculate_alignment(const void *address);

// Type system integration
void register_type_info(uint32_t type_id, const TypeInfo *info);
const TypeInfo *get_type_info(uint32_t type_id);
uint32_t get_type_id_by_name(const char *type_name);

// Compiler enforcement functions (used during semantic analysis)
bool can_dereference_handle(const TypedHandle *handle);
bool can_dereference_typed_pointer(const TypedPointer *ptr);
bool can_dereference_raw_pointer(const RawPointer *ptr);
bool requires_explicit_cast(const void *from_ptr, const void *to_ptr);

// Syscall interface requirements
bool is_syscall_compatible_pointer(const RawPointer *ptr);
RawPointer prepare_syscall_pointer(const TypedPointer *typed);
bool validate_syscall_parameters(const RawPointer *ptrs, size_t count);

// Debug and safety features
void enable_pointer_debugging(bool enable);
void log_pointer_operation(const char *operation, const void *ptr);
bool detect_use_after_free(const void *address);
bool detect_double_free(const void *address);

// Memory layout and MMIO support
RawPointer map_mmio_address(uint64_t physical_address, size_t size);
bool unmap_mmio_address(const RawPointer *mmio_ptr);
bool is_mmio_address(const void *address);

// Stack allocation support
TypedPointer allocate_stack_memory(size_t size, size_t alignment, uint32_t type_id);
bool free_stack_memory(const TypedPointer *ptr);

// Integration with FCx operators
typedef struct {
    const char *operator_symbol;
    bool (*handle_operation)(const TypedHandle *handle, void *result);
    bool (*typed_pointer_operation)(const TypedPointer *ptr, void *result);
    bool (*raw_pointer_operation)(const RawPointer *ptr, void *result);
} PointerOperatorHandler;

// Register operator handlers for three-pointer system
void register_pointer_operator(const PointerOperatorHandler *handler);
bool execute_pointer_operator(const char *operator_symbol, const void *ptr, void *result);

#endif // FCX_POINTER_TYPES_H