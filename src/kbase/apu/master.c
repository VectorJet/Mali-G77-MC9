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

int main() {
    printf("=== APU Device-Mapped Submission Test ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Prime Handshake
    uint32_t buf40[10] = {0};
    ioctl(fd, 0xC0284120, buf40);
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 2. Try mmap on /dev/apusys itself
    size_t map_size = 256 * 1024;
    void *m1 = mmap(NULL, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (m1 == MAP_FAILED) {
        printf("[FAIL] mmap on /dev/apusys failed: %s\n", strerror(errno));
        // Fallback to anonymous tagged-like mapping if needed
    } else {
        printf("[OK] mmap on /dev/apusys success at %p\n", m1);
    }

    // 3. Allocate the required context FD
    uint32_t mem[12] = {0};
    mem[6] = 76;
    ioctl(fd, 0xC0304121, mem);
    int fd_ctx = mem[0];

    // 4. Submission
    uint8_t job[152];
    memset(job, 0, sizeof(job));
    
    // We'll use the device-mapped memory for the pointers
    if (m1 != MAP_FAILED) {
        *(uint64_t*)(job + 24) = (uintptr_t)m1;
        *(uint64_t*)(job + 80) = (uintptr_t)m1 + 4096;
        *(uint64_t*)(job + 88) = (uintptr_t)m1 + 8192;
    }

    *(uint32_t*)(job + 104) = (uint32_t)fd_ctx;
    *(uint32_t*)(job + 32) = 0x14;
    *(uint32_t*)(job + 36) = 0x7530;
    *(uint32_t*)(job + 56) = 0x1E;
    *(uint32_t*)(job + 72) = 0x01;
    *(int64_t*)(job + 96) = -1;

    printf("Attempting submission with device-mapped pointers...\n");
    int ret = ioctl(fd, 0xC0984122, job);
    
    if (ret == 0) {
        printf("[SUCCESS] APU Hardware accepted the device-mapped job!\n");
    } else {
        printf("[FAIL] ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
    }

    if (m1 != MAP_FAILED) munmap(m1, map_size);
    close(fd_ctx); close(fd);
    return 0;
}
