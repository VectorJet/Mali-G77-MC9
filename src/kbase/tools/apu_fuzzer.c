#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU IOCTL Extended Range Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));

    // Wide sweep on NR from 0x24 to 0x3F
    for (int nr = 0x24; nr <= 0x3F; nr++) {
        // Try various sizes encoded in bits 16-29
        int sizes[] = {0, 8, 16, 24, 32, 40, 48, 64, 72, 80, 128, 152};
        for (int i = 0; i < sizeof(sizes)/sizeof(int); i++) {
            unsigned int cmd = 0xC0004100 | (sizes[i] << 16) | nr;
            int ret = ioctl(fd, cmd, buf);
            
            // If it's NOT ENOTTY or EINVAL, it's supported!
            if (errno != ENOTTY && errno != EINVAL) {
                printf("[HIT] ioctl(0x%08X, nr=0x%02X, size=%d) ret=%d error %d (%s)\n", 
                       cmd, nr, sizes[i], ret, errno, strerror(errno));
            }
        }
    }

    close(fd);
    return 0;
}
