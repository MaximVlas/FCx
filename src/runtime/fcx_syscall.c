#include "fcx_runtime.h"
#include <sys/syscall.h>
#include <sys/mman.h>
#include <fcntl.h>

// ============================================================================
// Direct Syscall Interface (Task 7.2)
// ============================================================================

// Raw syscall interface with x86_64 System V ABI (sys% operator)
long fcx_syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6) {
    long result;
    register long r10 __asm__("r10") = arg4;
    register long r8 __asm__("r8") = arg5;
    register long r9 __asm__("r9") = arg6;
    
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    
    return result;
}

// Compact write syscall ($/ operator)
long fcx_write_op(int fd, const void* buf, size_t count) {
    return fcx_syscall(FCX_SYS_WRITE, fd, (long)buf, count, 0, 0, 0);
}

// Compact read syscall (/$ operator)
long fcx_read_op(int fd, void* buf, size_t count) {
    return fcx_syscall(FCX_SYS_READ, fd, (long)buf, count, 0, 0, 0);
}

// ============================================================================
// Higher-Level Syscall Wrappers (@sys operators)
// ============================================================================

// Open file
int fcx_sys_open(const char* path, int flags, int mode) {
    long result = fcx_syscall(FCX_SYS_OPEN, (long)path, flags, mode, 0, 0, 0);
    return (int)result;
}

// Close file
int fcx_sys_close(int fd) {
    long result = fcx_syscall(FCX_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
    return (int)result;
}

// Memory map
void* fcx_sys_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    long result = fcx_syscall(FCX_SYS_MMAP, (long)addr, length, prot, flags, fd, offset);
    
    if (result < 0 && result > -4096) {
        return (void*)-1; // Error
    }
    
    return (void*)result;
}

// Memory unmap
int fcx_sys_munmap(void* addr, size_t length) {
    long result = fcx_syscall(FCX_SYS_MUNMAP, (long)addr, length, 0, 0, 0, 0);
    return (int)result;
}

// Exit process
void fcx_sys_exit(int status) {
    fcx_syscall(FCX_SYS_EXIT, status, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

// ============================================================================
// Error Handling with ?! Operator
// ============================================================================

// Checked syscall with error handling
FcxSyscallResult fcx_syscall_checked(long num, long arg1, long arg2, long arg3) {
    FcxSyscallResult result;
    result.value = fcx_syscall(num, arg1, arg2, arg3, 0, 0, 0);
    
    // Linux syscalls return -errno on error (values between -1 and -4095)
    if (result.value < 0 && result.value >= -4095) {
        result.error = (int)(-result.value);
        result.value = -1;
    } else {
        result.error = 0;
    }
    
    return result;
}

// ============================================================================
// Resource Management Operators (%$, $%)
// ============================================================================

// Resource query (%$ operator) - stub for now
long fcx_resource_query(int resource_type) {
    // Suppress unused parameter warning
    (void)resource_type;
    
    // Could query system resources like available memory, CPU count, etc.
    // For now, return 0
    return 0;
}

// Resource allocation ($% operator) - stub for now
long fcx_resource_alloc(int resource_type, size_t amount) {
    // Suppress unused parameter warnings
    (void)resource_type;
    (void)amount;
    
    // Could allocate system resources
    // For now, return 0
    return 0;
}

// ============================================================================
// Privilege and Capability Operators (#!, !#)
// ============================================================================

// Privilege escalation (#! operator) - stub for now
int fcx_privilege_escalate(void) {
    // Would use setuid/setgid syscalls
    // For now, return -1 (not implemented)
    return -1;
}

// Capability check (!# operator) - stub for now
int fcx_capability_check(uint64_t capability) {
    // Suppress unused parameter warning
    (void)capability;
    
    // Would check process capabilities
    // For now, return 0 (no capability)
    return 0;
}
