#ifndef FCX_BOOTSTRAP_H
#define FCX_BOOTSTRAP_H

#include <stdint.h>
#include <stddef.h>

// FCx Bootstrap Runtime Header
// Resolves the bootstrap paradox by providing initial runtime functions
// implemented in assembly/C that allow the FCx runtime to be written in FCx

// Bootstrap memory management
void *_fcx_alloc(size_t size, size_t alignment);
void _fcx_free(void *ptr);
void *_fcx_stack_alloc(size_t size);
void _fcx_stack_free(void *ptr);

// Bootstrap syscall interface
long _fcx_syscall(long syscall_num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);
long _fcx_write(int fd, const void *buf, size_t count);
long _fcx_read(int fd, void *buf, size_t count);

// Bootstrap atomic operations
void _fcx_atomic_fence(void);
void _fcx_atomic_load_fence(void);
void _fcx_atomic_store_fence(void);

// Bootstrap MMIO operations
void *_fcx_mmio_map(uint64_t physical_address, size_t size);
void _fcx_mmio_unmap(void *address, size_t size);

// Bootstrap error handling
void _fcx_panic(const char *message);

// Bootstrap entry point
void fcx_bootstrap_start(void);

// FCx main function (to be implemented by user code)
extern int fcx_main(void);

#endif // FCX_BOOTSTRAP_H