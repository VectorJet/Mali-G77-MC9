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
    printf("=== APU Absolute Precision Submission ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Prime
    uint32_t b40[10] = {0};
    ioctl(fd, 0xC0284120, b40);
    b40[0] = 1; b40[4] = 1; ioctl(fd, 0xC0284120, b40);
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);
    memset(b40, 0, sizeof(b40)); b40[0] = 2; b40[4] = 1; ioctl(fd, 0xC0284120, b40);

    // 2. Allocate Linked Buffers
    uint32_t mem[12] = {0};
    mem[6] = 4096; ioctl(fd, 0xC0304121, mem);
    int fd_data = mem[0];
    
    memset(mem, 0, sizeof(mem));
    mem[6] = 76; mem[10] = fd_data; ioctl(fd, 0xC0304121, mem);
    int fd_ctx = mem[0];
    
    printf("[OK] Linked FDs: %d -> %d\n", fd_data, fd_ctx);

    // 3. Prepare Job Packet (152 bytes) - OFFSET PERFECT
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    
    uint8_t s1[1024], s2[1024], s3[1024];
    memset(s1, 0, 1024); memset(s2, 0, 1024); memset(s3, 0, 1024);

    // Offset 24: Ptr1
    *(uintptr_t*)(job + 24) = (uintptr_t)s1;
    // Offset 32: Priority
    *(uint32_t*)(job + 32) = 0x14;
    // Offset 36: Timeout
    *(uint32_t*)(job + 36) = 0x7530;
    // Offset 56: Engine Count
    *(uint32_t*)(job + 56) = 0x1E;
    // Offset 72: Task Count
    *(uint32_t*)(job + 72) = 0x01;
    // Offset 100: Ptr2
    *(uintptr_t*)(job + 100) = (uintptr_t)s2;
    // Offset 108: Ptr3
    *(uintptr_t*)(job + 108) = (uintptr_t)s3;
    // Offset 116: Fence/Magic
    *(int64_t*)(job + 116) = -1;
    // Offset 124: CONTEXT HANDLE
    *(uint32_t*)(job + 124) = (uint32_t)fd_ctx;

    printf("Attempting submission with Absolute Precision offsets...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    
    if (ret == 0) {
        printf("[SUCCESS] !!! APU ACCEPTED THE JOB !!!\n");
        printf("Hardware status at offset 8: 0x%X\n", *(uint32_t*)(job + 8));
    } else {
        printf("[FAIL] ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        printf("Kernel status field [8]: 0x%X\n", *(uint32_t*)(job + 8));
    }

    close(fd_data); close(fd_ctx); close(fd);
    return 0;
}
