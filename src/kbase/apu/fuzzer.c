#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

int main() {
    printf("=== APU IOCTL Validation Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    // Use a large buffer to ensure valid memory access
    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));

    // NR 32 to 35 are our known targets
    for (int nr = 32; nr <= 35; nr++) {
        // Based on ubfx w20, w1, #16, #14 and strace 0xC0284120
        // The size field in _IOWR might be expected at different bits or 
        // the driver might be using a non-standard encoding.
        // Let's try 40 bytes (0x28) first.
        
        unsigned int cmd = _IOWR('A', nr, 0x28);
        int ret = ioctl(fd, cmd, buf);
        if (ret == 0) {
            printf("[SUCCESS] ioctl(0x%08X, nr=%d, size=40) ret=0\n", cmd, nr);
            // Print returned data
            printf("  Response: ");
            for (int i = 0; i < 40; i++) printf("%02X ", buf[i]);
            printf("\n");
        } else {
            printf("[FAIL] ioctl(0x%08X, nr=%d, size=40) ret=%d error %d (%s)\n", cmd, nr, ret, errno, strerror(errno));
        }
    }

    close(fd);
    return 0;
}
