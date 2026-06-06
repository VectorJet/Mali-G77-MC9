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
    printf("=== APU Userspace Mapping Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) return 1;

    uint32_t packet[12] = {0};
    unsigned int cmd_mem = 0xC0304121;

    // Use a page-aligned userspace buffer
    void *user_buf = NULL;
    posix_memalign(&user_buf, 4096, 4096);
    memset(user_buf, 0xAA, 4096);

    // Try op=1 (Suspected Import/Map)
    memset(packet, 0, sizeof(packet));
    packet[0] = 1; // Op 1
    *(uint64_t*)(packet + 16) = (uintptr_t)user_buf; // Pointer at offset 16?
    packet[6] = 4096; // Size at offset 24

    printf("Attempting userspace import (op=1) at %p...\n", user_buf);
    int ret = ioctl(fd, cmd_mem, packet);
    
    if (ret == 0) {
        printf("[SUCCESS] Userspace memory imported! Handle: %d\n", packet[0]);
    } else {
        printf("[FAIL] op=1 failed: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        
        // Try op=2 (Another candidate)
        packet[0] = 2;
        ret = ioctl(fd, cmd_mem, packet);
        if (ret == 0) {
            printf("[SUCCESS] Userspace memory imported via op=2! Handle: %d\n", packet[0]);
        } else {
            printf("[FAIL] op=2 failed: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));
        }
    }

    close(fd);
    free(user_buf);
    return 0;
}
