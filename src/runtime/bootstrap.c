#include "bootstrap.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

// FCx Bootstrap Runtime - Resolves Bootstrap Paradox
// Initial runtime functions implemented in assembly/C to bootstrap FCx runtime
// written in FCx

// Memory block header for bootstrap allocator
typedef struct BootstrapBlock {
  size_t size;
  int is_free;
  uint32_t magic;
  struct BootstrapBlock *next;
} BootstrapBlock;

#define BOOTSTRAP_MAGIC 0xFCB00000

// Global state for bootstrap allocator
static void *heap_start = NULL;
static void *heap_end = NULL;
static BootstrapBlock *free_list = NULL;

// Bootstrap syscall wrappers (direct assembly)
static inline long bootstrap_syscall1(long syscall_num, long arg1) {
  long result;
  __asm__ volatile("movq %1, %%rax\n\t"
                   "movq %2, %%rdi\n\t"
                   "syscall\n\t"
                   "movq %%rax, %0"
                   : "=r"(result)
                   : "r"(syscall_num), "r"(arg1)
                   : "rax", "rdi", "rcx", "r11", "memory");
  return result;
}

static inline __attribute__((unused)) long bootstrap_syscall3(long syscall_num, long arg1, long arg2,
                                      long arg3) {
  long result;
  __asm__ volatile("movq %1, %%rax\n\t"
                   "movq %2, %%rdi\n\t"
                   "movq %3, %%rsi\n\t"
                   "movq %4, %%rdx\n\t"
                   "syscall\n\t"
                   "movq %%rax, %0"
                   : "=r"(result)
                   : "r"(syscall_num), "r"(arg1), "r"(arg2), "r"(arg3)
                   : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
  return result;
}

// Bootstrap sys_brk wrapper
static void *bootstrap_brk(void *addr) {
  return (void *)bootstrap_syscall1(SYS_brk, (long)addr);
}

// Initialize bootstrap heap
static int init_bootstrap_heap(void) {
  if (heap_start != NULL) {
    return 0; // Already initialized
  }

  // Get current break
  heap_start = bootstrap_brk(NULL);
  if (heap_start == (void *)-1) {
    return -1;
  }

  // Allocate initial heap (64KB)
  heap_end = bootstrap_brk((char *)heap_start + 65536);
  if (heap_end == (void *)-1) {
    return -1;
  }

  // Initialize free list with entire heap
  free_list = (BootstrapBlock *)heap_start;
  free_list->size =
      (char *)heap_end - (char *)heap_start - sizeof(BootstrapBlock);
  free_list->is_free = 1;
  free_list->magic = BOOTSTRAP_MAGIC;
  free_list->next = NULL;

  return 0;
}

// Bootstrap memory allocator - _fcx_alloc implementation
void *_fcx_alloc(size_t size, size_t alignment) {
  if (init_bootstrap_heap() != 0) {
    return NULL;
  }

  // Align size to at least 8 bytes
  if (alignment < 8)
    alignment = 8;
  size = (size + alignment - 1) & ~(alignment - 1);

  // Find suitable free block
  BootstrapBlock *current = free_list;
  BootstrapBlock *prev = NULL;

  while (current) {
    if (current->magic != BOOTSTRAP_MAGIC) {
      // Heap corruption detected
      return NULL;
    }

    if (current->is_free && current->size >= size) {
      // Found suitable block
      if (current->size > size + sizeof(BootstrapBlock) + 16) {
        // Split block if it's significantly larger
        BootstrapBlock *new_block =
            (BootstrapBlock *)((char *)current + sizeof(BootstrapBlock) + size);
        new_block->size = current->size - size - sizeof(BootstrapBlock);
        new_block->is_free = 1;
        new_block->magic = BOOTSTRAP_MAGIC;
        new_block->next = current->next;

        current->size = size;
        current->next = new_block;
      }

      current->is_free = 0;
      return (char *)current + sizeof(BootstrapBlock);
    }

    prev = current;
    current = current->next;
  }

  // No suitable block found, extend heap
  size_t total_size = sizeof(BootstrapBlock) + size;
  void *new_heap_end = bootstrap_brk((char *)heap_end + total_size);
  if (new_heap_end == (void *)-1) {
    return NULL;
  }

  // Create new block at end of heap
  BootstrapBlock *new_block = (BootstrapBlock *)heap_end;
  new_block->size = size;
  new_block->is_free = 0;
  new_block->magic = BOOTSTRAP_MAGIC;
  new_block->next = NULL;

  // Link to free list
  if (prev) {
    prev->next = new_block;
  }

  heap_end = new_heap_end;
  return (char *)new_block + sizeof(BootstrapBlock);
}

// Bootstrap memory deallocator - _fcx_free implementation
void _fcx_free(void *ptr) {
  if (!ptr)
    return;

  BootstrapBlock *block =
      (BootstrapBlock *)((char *)ptr - sizeof(BootstrapBlock));

  // Validate magic number
  if (block->magic != BOOTSTRAP_MAGIC) {
    // Invalid pointer or heap corruption
    return;
  }

  if (block->is_free) {
    // Double free detected
    return;
  }

  block->is_free = 1;

  // Coalesce with next block if it's free
  if (block->next && block->next->is_free) {
    block->size += sizeof(BootstrapBlock) + block->next->size;
    block->next = block->next->next;
  }

  // Find previous block and coalesce if it's free
  BootstrapBlock *current = free_list;
  while (current && current->next != block) {
    current = current->next;
  }

  if (current && current->is_free) {
    current->size += sizeof(BootstrapBlock) + block->size;
    current->next = block->next;
  }
}

// Bootstrap stack allocator - _fcx_stack_alloc implementation
void *_fcx_stack_alloc(size_t size) {
  // Simple stack allocation using alloca-like behavior
  // In a real implementation, this would manipulate RSP directly
  return _fcx_alloc(size, 8);
}

// Bootstrap stack deallocator - _fcx_stack_free implementation
void _fcx_stack_free(void *ptr) {
  // In a real implementation, this would adjust RSP
  // For bootstrap, just use regular free
  _fcx_free(ptr);
}

// Bootstrap syscall interface - _fcx_syscall implementation
long _fcx_syscall(long syscall_num, long arg1, long arg2, long arg3, long arg4,
                  long arg5, long arg6) {
  long result;
  register long r10 __asm__("r10") = arg4;
  register long r8 __asm__("r8") = arg5;
  register long r9 __asm__("r9") = arg6;

  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(syscall_num), "D"(arg1), "S"(arg2), "d"(arg3),
                     "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory");
  return result;
}

// Bootstrap write syscall - _fcx_write implementation
long _fcx_write(int fd, const void *buf, size_t count) {
  return _fcx_syscall(SYS_write, fd, (long)buf, count, 0, 0, 0);
}

// Bootstrap read syscall - _fcx_read implementation
long _fcx_read(int fd, void *buf, size_t count) {
  return _fcx_syscall(SYS_read, fd, (long)buf, count, 0, 0, 0);
}

// Bootstrap atomic operations
void _fcx_atomic_fence(void) { __asm__ volatile("mfence" ::: "memory"); }

void _fcx_atomic_load_fence(void) { __asm__ volatile("lfence" ::: "memory"); }

void _fcx_atomic_store_fence(void) { __asm__ volatile("sfence" ::: "memory"); }

// Bootstrap MMIO operations (stubs for now)
void *_fcx_mmio_map(uint64_t physical_address, size_t size) {
  // In a real implementation, this would use mmap
  (void)physical_address;
  (void)size;
  return NULL;
}

void _fcx_mmio_unmap(void *address, size_t size) {
  // In a real implementation, this would use munmap
  (void)address;
  (void)size;
}

// Bootstrap error handling
void _fcx_panic(const char *message) {
  _fcx_write(2, "FCx PANIC: ", 11);
  _fcx_write(2, message, strlen(message));
  _fcx_write(2, "\n", 1);
  _fcx_syscall(SYS_exit, 1, 0, 0, 0, 0, 0);
}

// Bootstrap entry point - fcx_bootstrap_start implementation
void fcx_bootstrap_start(void) {
  // Initialize bootstrap runtime
  if (init_bootstrap_heap() != 0) {
    _fcx_panic("Failed to initialize bootstrap heap");
  }

  // Call FCx main function
  int result = fcx_main();

  // Exit with result
  _fcx_syscall(SYS_exit, result, 0, 0, 0, 0, 0);
}

// Weak symbol for fcx_main - can be overridden by user code
__attribute__((weak)) int fcx_main(void) {
  _fcx_write(1, "Hello from FCx bootstrap runtime!\n", 34);
  return 0;
}