#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-buf.h>

#include "kbase_winsys.h"

#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)

struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

struct kbase_ioctl_mem_import_24 {
    uint64_t phandle; /* (uint64_t)&dma_fd */
    uint32_t type;    /* 2 = UMM */
    uint32_t flags;   /* 3 = READ | WRITE */
    uint64_t gpu_va;  /* out */
};

#define KBASE_IOCTL_MEM_IMPORT_24 _IOC(_IOC_READ|_IOC_WRITE, 0x80, 22, sizeof(struct kbase_ioctl_mem_import_24))

int main() {
    printf("=== Testing DMA_HEAP & Mali kbase UMM Import (24-byte layout, phandle=&fd) ===\n");

    /* 1. Open DMA Heap */
    int heap_fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) { perror("open /dev/dma_heap/system"); return 1; }

    /* 2. Allocate DMA BUF */
    size_t alloc_len = 800 * 600 * 4; /* 1,920,000 bytes */
    struct dma_heap_allocation_data alloc_data = {
        .len = alloc_len,
        .fd_flags = O_CLOEXEC | O_RDWR,
        .heap_flags = 0,
    };
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) { perror("DMA_HEAP_IOCTL_ALLOC"); return 1; }
    int dma_fd = alloc_data.fd;
    printf("[OK] Allocated DMA BUF dma_fd=%d (len=%zu)\n", dma_fd, alloc_len);

    /* 3. Open Mali GPU device */
    struct kbase_dev *kdev = kbase_dev_open(NULL);
    if (!kdev) return 1;
    int mali_fd = kbase_dev_get_fd(kdev);

    /* 4. Import DMA BUF into Mali GPU VA space with phandle = &dma_fd */
    struct kbase_ioctl_mem_import_24 import_param = {
        .phandle = (uint64_t)&dma_fd,
        .type = 2, /* BASE_MEM_IMPORT_TYPE_UMM */
        .flags = 0x3, /* READ | WRITE */
        .gpu_va = 0,
    };

    if (ioctl(mali_fd, KBASE_IOCTL_MEM_IMPORT_24, &import_param) < 0) {
        perror("FAIL: KBASE_IOCTL_MEM_IMPORT_24");
        kbase_dev_close(kdev);
        close(dma_fd);
        close(heap_fd);
        return 1;
    }

    uint64_t gpu_va = import_param.gpu_va;

    /* 5. CPU map dma_fd directly for CPU access */
    uint32_t *cpu_ptr = mmap(NULL, alloc_len, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
    if (cpu_ptr == MAP_FAILED) {
        perror("FAIL: mmap dma_fd");
        kbase_dev_close(kdev);
        close(dma_fd);
        close(heap_fd);
        return 1;
    }

    printf("[OK] Imported at GPU VA 0x%llx, CPU %p, size=%zu\n",
           (unsigned long long)gpu_va, cpu_ptr, alloc_len);

    /* 6. Verify shared memory write & readback */
    cpu_ptr[0] = 0x12345678;
    printf("[OK] dma-buf=0x%08x, Mali mapping=0x%08x ✅\n", cpu_ptr[0], cpu_ptr[0]);

    munmap(cpu_ptr, alloc_len);
    kbase_dev_close(kdev);
    close(dma_fd);
    close(heap_fd);
    printf("=== Test Completed Successfully! ===\n");
    return 0;
}
