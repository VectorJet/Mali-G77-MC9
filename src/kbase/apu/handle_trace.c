#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main() {
    printf("=== APU Handle Flow Probe ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) return 1;

    // 1. Handshake 1
    uint8_t h1[40] = {0};
    ioctl(fd, 0xC0284120, h1);
    uint64_t session_id = *(uint64_t*)h1;
    printf("[OK] Handshake 1 returned SessionID: 0x%016llX\n", (unsigned long long)session_id);

    // 2. Handshake 2 (MDLA)
    uint8_t h2[40] = {0};
    *(uint32_t*)h2 = 1; // Query
    *(uint32_t*)(h2 + 16) = 1; // MDLA
    ioctl(fd, 0xC0284120, h2);
    printf("[OK] Handshake 2 returned: 0x%016llX\n", *(unsigned long long*)h2);

    // 3. Handshake 3 (Init)
    uint8_t h3[40] = {0};
    *(uint32_t*)h3 = 2; // Init?
    *(uint32_t*)(h3 + 16) = 1; // MDLA
    ioctl(fd, 0xC0284120, h3);
    printf("[OK] Handshake 3 returned: 0x%016llX\n", *(unsigned long long*)h3);

    // 4. Allocation with SessionID
    uint32_t mem[12] = {0};
    mem[0] = 0; 
    mem[6] = 4096;
    // Try passing Handshake 3 result at offset 40
    *(uint64_t*)&mem[10] = *(uint64_t*)h3;
    
    int ret = ioctl(fd, 0xC0304121, mem);
    if (ret == 0) {
        printf("[SUCCESS] Allocation with handle! FD: %d\n", mem[0]);
    } else {
        printf("[FAIL] Allocation failed: %s\n", strerror(errno));
    }

    close(fd);
    return 0;
}
