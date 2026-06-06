#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU Legacy IOCTL Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));

    // Based on libapusys.so disassembly:
    // 0xC0284100: Handshake?
    // 0x40184106: Query?
    // 0xC0184107: Version?
    
    unsigned int cmds[] = {0xC0284100, 0x40184106, 0xC0184107};
    const char* names[] = {"Handshake (0x00)", "Query (0x06)", "Version (0x07)"};

    for (int i = 0; i < 3; i++) {
        memset(buf, 0, sizeof(buf));
        int ret = ioctl(fd, cmds[i], buf);
        printf("[%s] ret=%d, errno=%d (%s)\n", names[i], ret, errno, strerror(errno));
        if (ret == 0) {
            printf("  Data: ");
            for (int j = 0; j < 24; j++) printf("%02X ", buf[j]);
            printf("\n");
        }
    }

    close(fd);
    return 0;
}
