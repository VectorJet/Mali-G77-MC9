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

// Some headers might not have this, define manually
#ifndef DMA_BUF_SET_NAME_B
#define DMA_BUF_SET_NAME_B _IOW(DMA_BUF_BASE, 1, char[32])
#endif

int main() {
    printf("=== APU Final Named-Buffer Submission ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    // 1. Prime
    uint32_t buf40[10] = {0};
    ioctl(fd, 0xC0284120, buf40);
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 2. Allocate and NAME the context buffer
    uint32_t mem2[12] = {0}; mem2[6] = 76;
    ioctl(fd, 0xC0304121, mem2);
    int fd_ctx = mem2[0];
    
    char name[] = "APUCB0:2";
    if (ioctl(fd_ctx, DMA_BUF_SET_NAME_B, name) == 0) {
        printf("[OK] Named context buffer: %s\n", name);
    }

    // 3. Prepare Job
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    uint8_t desc[256];
    memset(desc, 0, sizeof(desc));
    *(uint64_t*)(job + 24) = (uintptr_t)desc;
    *(uint32_t*)(job + 124) = (uintptr_t)fd_ctx;

    // Blueprint constants
    *(uint32_t*)(job + 32) = 20;
    *(uint32_t*)(job + 36) = 30000;
    *(uint32_t*)(job + 64) = 30;
    *(uint32_t*)(job + 88) = 1;
    *(int64_t*)(job + 116) = -1;

    printf("Attempting submission with named buffer...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    printf("Result: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    close(fd_ctx);
    close(fd);
    return 0;
}
