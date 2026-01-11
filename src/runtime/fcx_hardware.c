#include "fcx_runtime.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cpuid.h>

// ============================================================================
// MMIO Operations (Task 7.4)
// ============================================================================

// Map physical address to virtual address (@> operator)
void* fcx_mmio_map(uint64_t physical_address, size_t size) {
    // Open /dev/mem for physical memory access
    int fd = fcx_sys_open("/dev/mem", O_RDWR | O_SYNC, 0);
    if (fd < 0) {
        return NULL;
    }
    
    // Map physical memory
    void* mapped = fcx_sys_mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        physical_address
    );
    
    fcx_sys_close(fd);
    
    if (mapped == (void*)-1) {
        return NULL;
    }
    
    return mapped;
}

// Unmap MMIO region (<@ operator)
void fcx_mmio_unmap(void* address, size_t size) {
    if (address && address != (void*)-1) {
        fcx_sys_munmap(address, size);
    }
}

// MMIO read with volatile semantics
uint64_t fcx_mmio_read_64(volatile void* address) {
    return fcx_volatile_load((volatile uint64_t*)address);
}

uint32_t fcx_mmio_read_32(volatile void* address) {
    uint32_t value;
    __asm__ volatile(
        "movl (%1), %0"
        : "=r"(value)
        : "r"(address)
        : "memory"
    );
    return value;
}

uint16_t fcx_mmio_read_16(volatile void* address) {
    uint16_t value;
    __asm__ volatile(
        "movw (%1), %0"
        : "=r"(value)
        : "r"(address)
        : "memory"
    );
    return value;
}

uint8_t fcx_mmio_read_8(volatile void* address) {
    uint8_t value;
    __asm__ volatile(
        "movb (%1), %0"
        : "=r"(value)
        : "r"(address)
        : "memory"
    );
    return value;
}

// MMIO write with volatile semantics
void fcx_mmio_write_64(volatile void* address, uint64_t value) {
    fcx_volatile_store((volatile uint64_t*)address, value);
}

void fcx_mmio_write_32(volatile void* address, uint32_t value) {
    __asm__ volatile(
        "movl %1, (%0)"
        :
        : "r"(address), "r"(value)
        : "memory"
    );
}

void fcx_mmio_write_16(volatile void* address, uint16_t value) {
    __asm__ volatile(
        "movw %1, (%0)"
        :
        : "r"(address), "r"(value)
        : "memory"
    );
}

void fcx_mmio_write_8(volatile void* address, uint8_t value) {
    __asm__ volatile(
        "movb %1, (%0)"
        :
        : "r"(address), "r"(value)
        : "memory"
    );
}

// ============================================================================
// Stack Manipulation (stack> operator)
// ============================================================================

// Dynamic stack allocation with RSP manipulation
void* fcx_stack_alloc_dynamic(size_t size) {
    void* ptr;
    
    // Align size to 16 bytes (required by System V ABI)
    size = (size + 15) & ~15;
    
    // Allocate on stack by adjusting RSP
    __asm__ volatile(
        "subq %1, %%rsp\n\t"
        "movq %%rsp, %0"
        : "=r"(ptr)
        : "r"(size)
        : "memory"
    );
    
    return ptr;
}

// Free dynamic stack allocation
void fcx_stack_free_dynamic(void* ptr, size_t size) {
    // Suppress unused parameter warning
    (void)ptr;
    
    // Align size to 16 bytes
    size = (size + 15) & ~15;
    
    // Restore RSP
    __asm__ volatile(
        "addq %0, %%rsp"
        :
        : "r"(size)
        : "memory"
    );
}

// Get current stack pointer
void* fcx_get_stack_pointer(void) {
    void* sp;
    __asm__ volatile(
        "movq %%rsp, %0"
        : "=r"(sp)
    );
    return sp;
}

// Get frame pointer
void* fcx_get_frame_pointer(void) {
    void* fp;
    __asm__ volatile(
        "movq %%rbp, %0"
        : "=r"(fp)
    );
    return fp;
}

// ============================================================================
// CPU Feature Detection
// ============================================================================

// Detect CPU features using CPUID
CpuFeatures fcx_detect_cpu_features(void) {
    CpuFeatures features = {0};
    
    uint32_t eax, ebx, ecx, edx;
    
    // Check if CPUID is supported
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        return features;
    }
    
    // Get feature flags from CPUID leaf 1
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        // SSE2 (bit 26 of EDX)
        if (edx & (1 << 26)) {
            features.features |= CPU_FEATURE_SSE2;
            features.vector_width = 128;
        }
    }
    
    // Get extended features from CPUID leaf 7
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        // AVX2 (bit 5 of EBX)
        if (ebx & (1 << 5)) {
            features.features |= CPU_FEATURE_AVX2;
            features.vector_width = 256;
        }
        
        // AVX512F (bit 16 of EBX)
        if (ebx & (1 << 16)) {
            features.features |= CPU_FEATURE_AVX512F;
            features.vector_width = 512;
        }
        
        // BMI2 (bit 8 of EBX)
        if (ebx & (1 << 8)) {
            features.features |= CPU_FEATURE_BMI2;
        }
    }
    
    // Set cache line size (typically 64 bytes)
    features.cache_line_size = 64;
    
    // Set red zone size (128 bytes for x86_64 System V ABI)
    features.red_zone_size = 128;
    
    // Set preferred alignment (16 bytes for x86_64)
    features.alignment_pref = 16;
    
    return features;
}

// Check if specific feature is available
bool fcx_has_feature(uint64_t feature) {
    static CpuFeatures cached_features = {0};
    static bool initialized = false;
    
    if (!initialized) {
        cached_features = fcx_detect_cpu_features();
        initialized = true;
    }
    
    return (cached_features.features & feature) != 0;
}

// Get CPU vendor string
void fcx_get_cpu_vendor(char vendor[13]) {
    uint32_t ebx, ecx, edx;
    
    if (__get_cpuid(0, (uint32_t*)&vendor[0], &ebx, &ecx, &edx)) {
        *(uint32_t*)(vendor + 0) = ebx;
        *(uint32_t*)(vendor + 4) = edx;
        *(uint32_t*)(vendor + 8) = ecx;
        vendor[12] = '\0';
    } else {
        vendor[0] = '\0';
    }
}

// Get CPU model name
void fcx_get_cpu_model(char model[49]) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check if extended CPUID is supported
    if (!__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) || eax < 0x80000004) {
        model[0] = '\0';
        return;
    }
    
    // Get model name from CPUID leaves 0x80000002-0x80000004
    for (int i = 0; i < 3; i++) {
        __get_cpuid(0x80000002 + i, &eax, &ebx, &ecx, &edx);
        *(uint32_t*)(model + i * 16 + 0) = eax;
        *(uint32_t*)(model + i * 16 + 4) = ebx;
        *(uint32_t*)(model + i * 16 + 8) = ecx;
        *(uint32_t*)(model + i * 16 + 12) = edx;
    }
    
    model[48] = '\0';
}

// ============================================================================
// Register Hinting and Frame Pointer Management
// ============================================================================

// Force inline hint (inline> operator)
// This is a compiler attribute, implemented as macro
#define FCX_INLINE __attribute__((always_inline)) inline

// Tail call hint (tail> operator)
// This is a compiler optimization, implemented as macro
#define FCX_TAIL_CALL __attribute__((musttail))

// Fastcall hint (fastcall> operator)
// Use register calling convention
#define FCX_FASTCALL __attribute__((regparm(3)))

// No frame pointer optimization
#define FCX_NO_FRAME_POINTER __attribute__((optimize("omit-frame-pointer")))

// Red zone optimization for leaf functions
#define FCX_LEAF_FUNCTION __attribute__((leaf))

// ============================================================================
// Performance Monitoring
// ============================================================================

// Read timestamp counter
uint64_t fcx_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

// Read timestamp counter with fence (serializing)
uint64_t fcx_rdtscp(void) {
    uint32_t lo, hi;
    __asm__ volatile(
        "rdtscp"
        : "=a"(lo), "=d"(hi)
        :
        : "rcx"
    );
    return ((uint64_t)hi << 32) | lo;
}

// Pause instruction for spin loops
void fcx_pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

// Prefetch data
void fcx_prefetch(const void* addr) {
    __asm__ volatile(
        "prefetcht0 (%0)"
        :
        : "r"(addr)
    );
}

// Prefetch for write
void fcx_prefetch_write(const void* addr) {
    __asm__ volatile(
        "prefetchw (%0)"
        :
        : "r"(addr)
    );
}

// ============================================================================
// Cache Control
// ============================================================================

// Flush cache line
void fcx_clflush(const void* addr) {
    __asm__ volatile(
        "clflush (%0)"
        :
        : "r"(addr)
        : "memory"
    );
}

// Flush cache line optimized
void fcx_clflushopt(const void* addr) {
    if (fcx_has_feature(CPU_FEATURE_AVX2)) {
        __asm__ volatile(
            "clflushopt (%0)"
            :
            : "r"(addr)
            : "memory"
        );
    } else {
        fcx_clflush(addr);
    }
}

// Write back cache line
void fcx_clwb(const void* addr) {
    if (fcx_has_feature(CPU_FEATURE_AVX512F)) {
        __asm__ volatile(
            "clwb (%0)"
            :
            : "r"(addr)
            : "memory"
        );
    } else {
        fcx_clflushopt(addr);
    }
}

// ============================================================================
// Adaptive Instruction Selection
// ============================================================================

// Example: Optimized memory copy based on CPU features
void fcx_memcpy_adaptive(void* dest, const void* src, size_t n) {
    if (fcx_has_feature(CPU_FEATURE_AVX512F) && n >= 512) {
        // Use AVX512 for large copies
        // (Full implementation would use AVX512 instructions)
        __builtin_memcpy(dest, src, n);
    } else if (fcx_has_feature(CPU_FEATURE_AVX2) && n >= 256) {
        // Use AVX2 for medium copies
        __builtin_memcpy(dest, src, n);
    } else if (fcx_has_feature(CPU_FEATURE_SSE2) && n >= 128) {
        // Use SSE2 for small copies
        __builtin_memcpy(dest, src, n);
    } else {
        // Fallback to byte copy
        uint8_t* d = (uint8_t*)dest;
        const uint8_t* s = (const uint8_t*)src;
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
}

// Example: Optimized memory set based on CPU features
void fcx_memset_adaptive(void* dest, int value, size_t n) {
    if (fcx_has_feature(CPU_FEATURE_AVX512F) && n >= 512) {
        // Use AVX512 for large sets
        __builtin_memset(dest, value, n);
    } else if (fcx_has_feature(CPU_FEATURE_AVX2) && n >= 256) {
        // Use AVX2 for medium sets
        __builtin_memset(dest, value, n);
    } else if (fcx_has_feature(CPU_FEATURE_SSE2) && n >= 128) {
        // Use SSE2 for small sets
        __builtin_memset(dest, value, n);
    } else {
        // Fallback to byte set
        uint8_t* d = (uint8_t*)dest;
        for (size_t i = 0; i < n; i++) {
            d[i] = (uint8_t)value;
        }
    }
}
