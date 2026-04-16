#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define KBASE_IOC_MAGIC 0x67

int main(int argc, char *argv[]) {
    int fd;
    int ret;

    printf("=== Simple Mali Test ===\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /* Try VERSION_CHECK only */
    printf("\n--- Testing VERSION_CHECK ---\n");
    struct { uint32_t major; uint32_t minor; } ver = {11, 13};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 0, 8), &ver);
    printf("VERSION_CHECK: ret=%d, major=%u, minor=%u, errno=%d (%s)\n", 
           ret, ver.major, ver.minor, errno, strerror(errno));

    /* Try SET_FLAGS */
    printf("\n--- Testing SET_FLAGS ---\n");
    uint32_t flags = 0;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 1, 4), &flags);
    printf("SET_FLAGS: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    /* Try MEM_ALLOC */
    printf("\n--- Testing MEM_ALLOC ---\n");
    struct {
        uint64_t va_pages;
        uint64_t commit_pages;
        uint32_t extent;
        uint32_t flags;
        uint32_t priority;
        uint32_t padding;
    } mem_req = {0};
    mem_req.va_pages = 16;
    mem_req.flags = 1; /* USAGE_GPU */
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 5, 24), &mem_req);
    printf("MEM_ALLOC: ret=%d, va_pages=%llu, errno=%d (%s)\n", 
           ret, (unsigned long long)mem_req.va_pages, errno, strerror(errno));

    close(fd);
    printf("\nDone.\n");
    return 0;
}