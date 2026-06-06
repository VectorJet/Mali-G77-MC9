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
    printf("=== APU Multi-Buffer Allocation Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    uint32_t mem[12] = {0};
    unsigned int cmd_mem = 0xC0304121;

    // 1. First Allocation
    mem[0] = 0; mem[6] = 4096;
    ioctl(fd, cmd_mem, mem);
    int fd1 = mem[0];
    printf("[OK] First FD: %d\n", fd1);

    // 2. Second Allocation (linked?)
    memset(mem, 0, sizeof(mem));
    mem[0] = 0; 
    mem[6] = 76; // Size 76 as seen in trace
    mem[10] = (uint32_t)fd1; // Put FD1 at offset 40 (index 10)
    
    int ret = ioctl(fd, cmd_mem, mem);
    if (ret == 0) {
        printf("[SUCCESS] Second allocation (linked to %d) ret=0, FD: %d\n", fd1, mem[0]);
    } else {
        printf("[FAIL] Second allocation failed: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
    }

    close(fd1);
    if (ret == 0) close(mem[0]);
    close(fd);
    return 0;
}
