#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU IOCTL Size Probe v2 ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));

    // We know 0x20 works with 0x28 size.
    // Let's test 0x21, 0x22, 0x23 with various sizes.
    int nrs[] = {0x21, 0x22, 0x23};
    for (int i = 0; i < 3; i++) {
        int nr = nrs[i];
        printf("Probing NR 0x%02X...\n", nr);
        for (int size = 0; size < 512; size += 8) {
            unsigned int cmd = 0xC0004100 | (size << 16) | nr;
            int ret = ioctl(fd, cmd, buf);
            if (ret == 0) {
                printf("  [MATCH] size=%d (0x%02X) ret=0\n", size, size);
                break; 
            } else if (errno != EFAULT) {
                // If it's NOT Bad Address, the size might be okay but content is wrong
                printf("  [HIT] size=%d (0x%02X) error %d (%s)\n", size, size, errno, strerror(errno));
            }
        }
    }

    close(fd);
    return 0;
}
