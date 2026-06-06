#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU Job Packet Fuzzer ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    // 1. Allocate a valid handle to use in fuzzer
    uint32_t mem[12] = {0}; mem[6] = 4096;
    ioctl(fd, 0xC0304121, mem);
    int handle = mem[0];

    uint8_t job[152];
    unsigned int cmd_submit = 0xC0984122;

    // Test different magic numbers at offset 120 (Suspected command ID)
    for (int id = 0; id < 10; id++) {
        memset(job, 0, sizeof(job));
        
        // Pointers
        uint8_t dummy[1024];
        *(uint64_t*)(job + 24) = (uintptr_t)dummy;
        
        // Use the handle
        *(uint32_t*)(job + 124) = (uint32_t)handle;
        
        // Set ID at offset 120
        *(uint32_t*)(job + 120) = id;

        // Common constants
        *(uint32_t*)(job + 32) = 0x14;
        *(uint32_t*)(job + 36) = 0x7530;
        *(uint32_t*)(job + 88) = 1;

        int ret = ioctl(fd, cmd_submit, job);
        uint32_t status = *(uint32_t*)(job + 8);
        
        if (status != 0 && status != 6) {
            printf("[INTEREST] id=%d: ret=%d, status=0x%X\n", id, ret, status);
        }
    }

    close(handle);
    close(fd);
    return 0;
}
