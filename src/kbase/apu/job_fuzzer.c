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
    printf("=== APU Job Packet exhaustive field search ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Prime
    uint32_t metadata[10] = {0};
    ioctl(fd, 0xC0284120, metadata);
    uint32_t pwr[8] = {0, 1, 1, 0, 0, 0, 0, 0};
    ioctl(fd, 0xC0204123, pwr);

    // 2. Allocate Context
    uint32_t mem[12] = {0};
    mem[6] = 76;
    ioctl(fd, 0xC0304121, mem);
    int fd_ctx = mem[0];

    // 3. Sweep
    uint8_t job[152];
    uint8_t s1[1024];
    
    printf("Sweeping 32-bit fields for acceptance...\n");

    for (int offset = 0; offset <= 152 - 4; offset += 4) {
        memset(job, 0, sizeof(job));
        
        // Base valid structure
        *(uint64_t*)(job + 24) = (uintptr_t)s1;
        *(uint32_t*)(job + 104) = (uint32_t)fd_ctx;
        *(int64_t*)(job + 96) = -1;

        // Try metadata version at current offset
        *(uint32_t*)(job + offset) = 0x2;

        int ret = ioctl(fd, 0xC0984122, job);
        if (errno != 22) {
            printf("[SUCCESS?] Offset 0x%02X (val=2): ret=%d, errno=%d\n", offset, ret, errno);
        }
        
        // Try other common constants
        *(uint32_t*)(job + offset) = 1;
        ret = ioctl(fd, 0xC0984122, job);
        if (errno != 22) {
            printf("[SUCCESS?] Offset 0x%02X (val=1): ret=%d, errno=%d\n", offset, ret, errno);
        }
    }

    close(fd_ctx);
    close(fd);
    return 0;
}
