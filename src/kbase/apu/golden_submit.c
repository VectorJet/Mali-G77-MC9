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
    printf("=== APU MDLA Golden Submission ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Handshake
    uint32_t buf40[10] = {0};
    ioctl(fd, 0xC0284120, buf40); 
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 2. Allocate and LINK
    uint32_t mem1[12] = {0}; mem1[6] = 4096;
    ioctl(fd, 0xC0304121, mem1);
    int fd_cmd_data = mem1[0];

    memset(mem1, 0, sizeof(mem1));
    mem1[6] = 76;
    mem1[10] = fd_cmd_data;
    ioctl(fd, 0xC0304121, mem1);
    int fd_ctx = mem1[0];

    // 3. Load Golden Data into fd_cmd_data
    void *ptr = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd_cmd_data, 0);
    if (ptr != MAP_FAILED) {
        int gfd = open("/data/local/tmp/maxpower.bin", O_RDONLY);
        if (gfd >= 0) {
            read(gfd, ptr, 448);
            close(gfd);
            printf("[OK] Golden data loaded into command buffer\n");
        }
        munmap(ptr, 4096);
    }

    // 4. Job Submission
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    uint8_t s1[1024], s2[1024], s3[1024];
    
    *(uint64_t*)(job + 24) = (uintptr_t)s1;
    *(uint32_t*)(job + 32) = 0x14;
    *(uint32_t*)(job + 36) = 0x7530;
    *(uint32_t*)(job + 56) = 0x1E;
    *(uint32_t*)(job + 72) = 0x01;
    *(uint64_t*)(job + 80) = (uintptr_t)s2;
    *(uint64_t*)(job + 88) = (uintptr_t)s3;
    *(int64_t*)(job + 96) = -1;
    *(uint32_t*)(job + 104) = (uint32_t)fd_ctx;

    printf("Attempting submission of MDLA Golden Job...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    
    if (ret == 0) {
        printf("[SUCCESS] !!! APU ACCEPTED THE GOLDEN JOB !!!\n");
    } else {
        printf("[FAIL] ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        printf("Status Field at offset 8: 0x%X\n", *(uint32_t*)(job + 8));
    }

    close(fd_cmd_data); close(fd_ctx); close(fd);
    return 0;
}
