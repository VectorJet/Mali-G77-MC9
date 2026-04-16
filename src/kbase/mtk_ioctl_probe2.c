#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define MTK_IOC_MAGIC 0x80

struct mtk_0x18_req {
    uint32_t in_version;
    uint32_t out_version;
    uint64_t reserved[4];
};

struct mtk_0x16_req {
    uint32_t flags;
    uint32_t context_id;
    uint64_t reserved[4];
};

struct mtk_0x5_req {
    uint64_t gpu_va;
    uint32_t flags;
    uint32_t page_count;
    uint64_t reserved[3];
};

struct mtk_0x6_req {
    uint32_t type;
    uint32_t value;
    uint64_t reserved[2];
};

struct mtk_0x1d_req {
    uint32_t cmd;
    uint32_t value;
    uint64_t reserved[2];
};

struct mtk_0x14_req {
    uint32_t cmd;
    uint32_t value;
    uint64_t reserved[2];
};

int main(int argc, char *argv[]) {
    int fd;
    int ret;

    printf("=== MTK Mali Ioctl Probe - Proper Init Order ===\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /* FIRST: Standard kbase VERSION_CHECK (required first) */
    printf("\n=== Step 1: Standard KBASE Init ===\n");
    struct { uint32_t major; uint32_t minor; } ver = {11, 13};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 0, 8), &ver);
    printf("KBASE_VERSION_CHECK ret=%d, major=%u, minor=%u, errno=%d\n", 
           ret, ver.major, ver.minor, errno);
    if (ret < 0) {
        printf("VERSION_CHECK failed - cannot continue\n");
        close(fd);
        return 1;
    }

    /* SET_FLAGS */
    uint32_t flags = 0;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 1, 4), &flags);
    printf("KBASE_SET_FLAGS ret=%d, errno=%d\n", ret, errno);

    /* NOW try MTK ioctls */
    printf("\n=== Step 2: MTK Custom Ioctls ===\n");

    /* 0x18 */
    struct mtk_0x18_req req18 = {0};
    req18.in_version = 0x1113;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x18, 0x20), &req18);
    printf("ioctl(0x18) ret=%d, out_version=0x%x, errno=%d\n", 
           ret, req18.out_version, errno);

    /* 0x16 */
    struct mtk_0x16_req req16 = {0};
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x16, 0x18), &req16);
    printf("ioctl(0x16) ret=%d, context_id=%u, errno=%d\n", 
           ret, req16.context_id, errno);

    /* 0x6 */
    struct mtk_0x6_req req6 = {0};
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x6, 0x10), &req6);
    printf("ioctl(0x6) ret=%d, value=%u, errno=%d\n", ret, req6.value, errno);

    /* 0x1d */
    struct mtk_0x1d_req req1d = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1d, 0x10), &req1d);
    printf("ioctl(0x1d) ret=%d, value=%u, errno=%d\n", ret, req1d.value, errno);

    /* 0x5 */
    struct mtk_0x5_req req5 = {0};
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x5, 0x20), &req5);
    printf("ioctl(0x5) ret=%d, gpu_va=0x%llx, errno=%d\n", 
           ret, (unsigned long long)req5.gpu_va, errno);

    /* 0x14 */
    struct mtk_0x14_req req14 = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x14, 0x10), &req14);
    printf("ioctl(0x14) ret=%d, value=%u, errno=%d\n", ret, req14.value, errno);

    /* 0x2 */
    struct mtk_0x14_req req2 = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x2, 0x10), &req2);
    printf("ioctl(0x2) ret=%d, value=%u, errno=%d\n", ret, req2.value, errno);

    /* 0x1b */
    struct mtk_0x14_req req1b = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1b, 0x10), &req1b);
    printf("ioctl(0x1b) ret=%d, value=%u, errno=%d\n", ret, req1b.value, errno);

    /* NOW try JOB_SUBMIT */
    printf("\n=== Step 3: Job Submit After MTK Ioctls ===\n");
    struct {
        uint64_t jc;
        uint64_t udata[2];
        uint64_t extres_list;
        uint16_t nr_extres;
        uint16_t compat_core_req;
        uint8_t pre_dep[24];
        uint16_t atom_number;
        uint8_t prio;
        uint8_t device_nr;
        uint8_t padding;
        uint32_t core_req;
    } atom = {0};

    atom.jc = 0;
    atom.core_req = 0;
    atom.prio = 128;
    atom.atom_number = 1;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 2, 48), &atom);
    printf("KBASE_JOB_SUBMIT ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    close(fd);
    printf("\nDone.\n");
    return 0;
}