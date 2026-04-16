#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/types.h>

#define MTK_IOC_MAGIC 0x80

#define MTK_IO(nr) _IOC(_IOC_NONE, MTK_IOC_MAGIC, nr, 0)
#define MTK_IOR(nr, type) _IOC(_IOC_READ, MTK_IOC_MAGIC, nr, sizeof(type))
#define MTK_IOW(nr, type) _IOC(_IOC_WRITE, MTK_IOC_MAGIC, nr, sizeof(type))
#define MTK_IOWR(nr, type) _IOC(_IOC_READ|_IOC_WRITE, MTK_IOC_MAGIC, nr, sizeof(type))

/* From strace - sizes from ioctl command encoding */
#define MTK_0x18_SIZE 0x20  /* 32 bytes - likely version */
#define MTK_0x16_SIZE 0x18  /* 24 bytes - set flags */
#define MTK_0x6_SIZE  0x10  /* 16 bytes - query */
#define MTK_0x1d_SIZE 0x10  /* 16 bytes - init */
#define MTK_0x5_SIZE  0x20  /* 32 bytes - mem alloc */
#define MTK_0x14_SIZE 0x10  /* 16 bytes - finalize */
#define MTK_0x2_SIZE  0x10  /* 16 bytes */
#define MTK_0x1b_SIZE 0x10  /* 16 bytes */

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

    printf("=== MTK Mali Ioctl Probe ===\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /* Try 0x18 - likely version check (32 bytes) */
    printf("\n--- Testing ioctl 0x18 (32 bytes) ---\n");
    struct mtk_0x18_req req18 = {0};
    req18.in_version = 0x1113;  /* 11.13 */
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x18, 0x20), &req18);
    printf("ioctl(0x18) ret=%d, in_version=0x%x, out_version=0x%x\n", 
           ret, req18.in_version, req18.out_version);

    /* Try 0x16 - set flags (24 bytes) */
    printf("\n--- Testing ioctl 0x16 (24 bytes) ---\n");
    struct mtk_0x16_req req16 = {0};
    req16.flags = 0;
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x16, 0x18), &req16);
    printf("ioctl(0x16) ret=%d, flags=0x%x, context_id=%u\n", 
           ret, req16.flags, req16.context_id);

    /* Try 0x6 - query (16 bytes) */
    printf("\n--- Testing ioctl 0x6 (16 bytes) ---\n");
    struct mtk_0x6_req req6 = {0};
    req6.type = 1;
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x6, 0x10), &req6);
    printf("ioctl(0x6) ret=%d, type=%u, value=%u\n", ret, req6.type, req6.value);

    /* Try 0x1d - init (16 bytes) */
    printf("\n--- Testing ioctl 0x1d (16 bytes) ---\n");
    struct mtk_0x1d_req req1d = {0};
    req1d.cmd = 1;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1d, 0x10), &req1d);
    printf("ioctl(0x1d) ret=%d, cmd=%u, value=%u\n", ret, req1d.cmd, req1d.value);

    /* Try 0x5 - mem query (32 bytes) */
    printf("\n--- Testing ioctl 0x5 (32 bytes) ---\n");
    struct mtk_0x5_req req5 = {0};
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x5, 0x20), &req5);
    printf("ioctl(0x5) ret=%d, gpu_va=0x%llx, flags=0x%x\n", 
           ret, (unsigned long long)req5.gpu_va, req5.flags);

    /* Try 0x14 - finalize (16 bytes) */
    printf("\n--- Testing ioctl 0x14 (16 bytes) ---\n");
    struct mtk_0x14_req req14 = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x14, 0x10), &req14);
    printf("ioctl(0x14) ret=%d, cmd=%u, value=%u\n", ret, req14.cmd, req14.value);

    /* Try 0x2 - from strace */
    printf("\n--- Testing ioctl 0x2 (16 bytes) ---\n");
    struct mtk_0x14_req req2 = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x2, 0x10), &req2);
    printf("ioctl(0x2) ret=%d, cmd=%u, value=%u\n", ret, req2.cmd, req2.value);

    /* Try 0x1b - from strace */
    printf("\n--- Testing ioctl 0x1b (16 bytes) ---\n");
    struct mtk_0x14_req req1b = {0};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1b, 0x10), &req1b);
    printf("ioctl(0x1b) ret=%d, cmd=%u, value=%u\n", ret, req1b.cmd, req1b.value);

    /* Now try standard kbase ioctls after MTK sequence */
    printf("\n=== Testing Standard Mali ioctls After MTK ===\n");

    /* VERSION_CHECK */
    struct { uint32_t major; uint32_t minor; } ver = {11, 13};
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 0, 8), &ver);
    printf("KBASE_VERSION_CHECK ret=%d, major=%u, minor=%u\n", ret, ver.major, ver.minor);

    /* SET_FLAGS */
    uint32_t flags = 0;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 1, 4), &flags);
    printf("KBASE_SET_FLAGS ret=%d\n", ret);

    /* Try JOB_SUBMIT now */
    printf("\n=== Testing JOB_SUBMIT After MTK Init ===\n");
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
    atom.core_req = 0;  /* DEP only */
    atom.prio = 128;
    atom.atom_number = 1;
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 2, 48), &atom);
    printf("KBASE_JOB_SUBMIT ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

    close(fd);
    printf("\nDone.\n");
    return 0;
}