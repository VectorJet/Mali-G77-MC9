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
    printf("=== APU Memory Mapping Verification ===\n");

    int fd = open("/dev/apusys", O_RDWR);
    if (fd < 0) {
        printf("[FAIL] Failed to open /dev/apusys\n");
        return 1;
    }

    uint32_t packet[12]; // 48 bytes
    unsigned int cmd_mem = 0xC0304121;
    size_t alloc_size = 4096;

    memset(packet, 0, sizeof(packet));
    packet[0] = 0; // ALLOC
    packet[6] = (uint32_t)alloc_size; 

    printf("Allocating hardware buffer...\n");
    if (ioctl(fd, cmd_mem, packet) != 0) {
        printf("[FAIL] Allocation ioctl failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    int dma_fd = packet[0];
    printf("[OK] Received DMA-BUF FD: %d\n", dma_fd);

    printf("Mapping FD %d to userspace...\n", dma_fd);
    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
    
    if (ptr == MAP_FAILED) {
        printf("[FAIL] mmap failed: %s\n", strerror(errno));
    } else {
        printf("[OK] Memory mapped at %p\n", ptr);

        // Verify R/W
        uint32_t *u32_ptr = (uint32_t*)ptr;
        printf("Writing pattern 0x12345678...\n");
        u32_ptr[0] = 0x12345678;
        
        if (u32_ptr[0] == 0x12345678) {
            printf("[OK] R/W verification successful!\n");
        } else {
            printf("[FAIL] R/W verification failed: read 0x%08X\n", u32_ptr[0]);
        }

        munmap(ptr, alloc_size);
    }

    close(dma_fd);
    close(fd);
    printf("Done.\n");
    return 0;
}
