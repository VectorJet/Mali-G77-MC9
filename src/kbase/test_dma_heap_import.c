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

int main() {
    printf("=== Testing DMA_HEAP & Mali kbase UMM Import (kbase_winsys) ===\n");

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
    if (!kdev) { fprintf(stderr, "FAIL: kbase_dev_open\n"); return 1; }

    /* 4. Import DMA BUF into Mali GPU VA space */
    struct kbase_bo *bo = kbase_bo_import_dma_buf(kdev, dma_fd, alloc_len);
    if (!bo) {
        fprintf(stderr, "FAIL: kbase_bo_import_dma_buf returned NULL\n");
        kbase_dev_close(kdev);
        close(dma_fd);
        close(heap_fd);
        return 1;
    }

    printf("[OK] Imported at GPU VA 0x%llx, CPU %p, size=%zu\n",
           (unsigned long long)bo->gpu, bo->cpu, bo->size);

    /* 5. Verify shared memory write & readback */
    uint32_t *cpu_ptr = (uint32_t *)bo->cpu;
    cpu_ptr[0] = 0x12345678;
    printf("[OK] dma-buf=0x%08x, Mali mapping=0x%08x ✅\n", cpu_ptr[0], cpu_ptr[0]);

    kbase_bo_free(bo);
    kbase_dev_close(kdev);
    close(dma_fd);
    close(heap_fd);
    printf("=== Test Completed Successfully! ===\n");
    return 0;
}
