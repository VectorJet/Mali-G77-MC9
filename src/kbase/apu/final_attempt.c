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
    printf("=== APU Final Precision Integrated Test ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Handshake Sequence
    uint32_t buf40[10] = {0};
    ioctl(fd, 0xC0284120, buf40); 
    buf40[0] = 1; buf40[4] = 1;
    ioctl(fd, 0xC0284120, buf40); 
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);
    buf40[0] = 2; buf40[4] = 1;
    ioctl(fd, 0xC0284120, buf40); 

    // 2. Allocate Context
    uint32_t mem[12] = {0};
    mem[6] = 76;
    ioctl(fd, 0xC0304121, mem);
    int fd_ctx = mem[0];
    ioctl(fd_ctx, DMA_BUF_SET_NAME_B, "APUCB0:1");

    // 3. Prepare Job Packet (152 bytes) - PRECISE OFFSETS
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    
    // Create valid secondary buffers for pointers
    uint8_t s1[1024], s2[1024], s3[1024];
    memset(s1, 0, 1024); memset(s2, 0, 1024); memset(s3, 0, 1024);

    // Offset 24: Descriptor Pointer
    *(uintptr_t*)(job + 24) = (uintptr_t)s1;
    // Offset 32: 20
    *(uint32_t*)(job + 32) = 0x14;
    // Offset 36: 30000
    *(uint32_t*)(job + 36) = 0x7530;
    // Offset 56: 30
    *(uint32_t*)(job + 56) = 0x1E;
    // Offset 72: 1
    *(uint32_t*)(job + 72) = 0x01;
    // Offset 80: Pointer 2
    *(uintptr_t*)(job + 80) = (uintptr_t)s2;
    // Offset 88: Pointer 3
    *(uintptr_t*)(job + 88) = (uintptr_t)s3;
    // Offset 96: -1 (8 bytes)
    *(int64_t*)(job + 96) = -1;
    // Offset 104: THE CONTEXT HANDLE
    *(uint32_t*)(job + 104) = (uint32_t)fd_ctx;

    printf("Attempting final precision submission...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    
    if (ret == 0) {
        printf("[SUCCESS] APU Hardware accepted the job!\n");
        printf("Hardware status code: 0x%X\n", *(uint32_t*)(job + 8));
    } else {
        printf("[FAIL] ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        printf("Kernel status field: 0x%X\n", *(uint32_t*)(job + 8));
    }

    close(fd_ctx);
    close(fd);
    return 0;
}
