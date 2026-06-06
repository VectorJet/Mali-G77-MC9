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
    printf("=== APU Final Linked-Buffer Submission ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    // 1. Hands
    uint32_t buf40[10] = {0};
    ioctl(fd, 0xC0284120, buf40); 
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 2. Allocate and LINK Buffers
    uint32_t mem[12] = {0};
    mem[6] = 4096; // Buffer 1
    ioctl(fd, 0xC0304121, mem);
    int fd_data = mem[0];
    ioctl(fd_data, DMA_BUF_SET_NAME_B, "APUCB0:1");

    memset(mem, 0, sizeof(mem));
    mem[6] = 76; // Buffer 2 (Context)
    mem[10] = fd_data; // LINK
    ioctl(fd, 0xC0304121, mem);
    int fd_ctx = mem[0];
    ioctl(fd_ctx, DMA_BUF_SET_NAME_B, "APUCB0:1");

    printf("[OK] Data FD: %d, Context FD: %d (Linked)\n", fd_data, fd_ctx);

    // 3. Prepare Job Packet (152 bytes)
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    uint8_t p1[256], p2[256], p3[256];
    *(uint64_t*)(job + 24) = (uintptr_t)p1;
    *(uint64_t*)(job + 80) = (uintptr_t)p2;
    *(uint64_t*)(job + 88) = (uintptr_t)p3;

    // Use Context FD as the job handle
    *(uint32_t*)(job + 124) = (uint32_t)fd_ctx;

    // Constants
    *(uint32_t*)(job + 32) = 0x14; 
    *(uint32_t*)(job + 36) = 30000;
    *(uint32_t*)(job + 56) = 0x1E;
    *(uint32_t*)(job + 72) = 0x01;
    *(int64_t*)(job + 96) = -1;

    printf("Attempting submission with Linked Handles...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    
    if (ret == 0) {
        printf("[SUCCESS] APU Hardware accepted the linked-buffer job packet!\n");
    } else {
        printf("[FAIL] ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        printf("Kernel status: 0x%X\n", *(uint32_t*)(job + 8));
    }

    close(fd_data); close(fd_ctx); close(fd);
    return 0;
}
