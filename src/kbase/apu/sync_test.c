#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <linux/dma-buf.h>

#ifndef DMA_BUF_SET_NAME_B
#define DMA_BUF_SET_NAME_B _IOW(DMA_BUF_BASE, 1, char[32])
#endif

int main() {
    printf("=== APU Sync-Based Submission Test ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Handshake
    uint32_t b40[10] = {0};
    ioctl(fd, 0xC0284120, b40);

    // 2. Wake
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 3. Alloc and Sync
    uint32_t mem[12] = {0};
    mem[6] = 4096;
    ioctl(fd, 0xC0304121, mem);
    int dma_fd = mem[0];

    // DMA_BUF_IOCTL_SYNC is often required before hardware access
    struct dma_buf_sync sync = {0};
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
    ioctl(dma_fd, DMA_BUF_IOCTL_SYNC, &sync);

    printf("[OK] Hardware buffer synchronized\n");

    // 4. Job Submission
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    uint8_t desc[1024] = {0};
    *(uint64_t*)(job + 24) = (uintptr_t)desc;
    *(uint32_t*)(job + 124) = (uint32_t)dma_fd;
    *(uint32_t*)(job + 8) = 1; // Sub-command count

    printf("Attempting submission with sync...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    printf("Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    ioctl(dma_fd, DMA_BUF_IOCTL_SYNC, &sync);

    close(dma_fd);
    close(fd);
    return 0;
}
