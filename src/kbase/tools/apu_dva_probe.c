#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU DVA Mapping Discovery ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    // 1. Allocate a handle
    uint32_t mem[12] = {0};
    mem[6] = 4096; 
    ioctl(fd, 0xC0304121, mem);
    int handle_fd = mem[0];
    printf("[OK] Handle FD: %d\n", handle_fd);

    // 2. Probe for a mapping operation (likely returns a 64-bit DVA)
    for (int op = 0; op < 16; op++) {
        uint32_t packet[12];
        memset(packet, 0, sizeof(packet));
        packet[0] = op;
        packet[1] = (uint32_t)handle_fd; // Try FD as arg 1
        
        int ret = ioctl(fd, 0xC0304121, packet);
        if (ret == 0) {
            printf("[HIT] op=%d, ret=0\n", op);
            printf("  New Data: ");
            for (int i = 0; i < 12; i++) printf("%08X ", packet[i]);
            printf("\n");
        }
    }

    close(handle_fd);
    close(fd);
    return 0;
}
