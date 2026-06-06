#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU Hardware Wake-up Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    uint32_t packet[8]; // 32 bytes
    unsigned int cmd_power = 0xC0204123;

    // Test different device types at offset 0
    int types[] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        int type = types[i];
        printf("Probing Device Type %d...\n", type);
        
        for (int offset = 0; offset < 4; offset++) {
            memset(packet, 0, sizeof(packet));
            packet[offset] = type;
            
            // Also try setting a 'Power On' flag (likely 1) in the next field
            packet[offset + 1] = 1;

            int ret = ioctl(fd, cmd_power, packet);
            if (ret == 0) {
                printf("  [SUCCESS] type=%d at offset %d! ret=0\n", type, offset);
            } else if (errno != 19) { // 19 is ENODEV
                printf("  [INTEREST] type=%d at offset %d: error %d (%s)\n", 
                       type, offset, errno, strerror(errno));
            }
        }
    }

    close(fd);
    return 0;
}
