/*
 * Mali kbase r49 - Working test with correct magic 0x80
 *
 * Key discovery: KBASE_IOCTL_TYPE = 0x80 in r49 (not 0x67)
 * The struct sizes and layouts also differ from older kbase versions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_TYPE 0x80

/* VERSION_CHECK: _IOWR(0x80, 0, ...) - 4 bytes */
struct kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};

/* SET_FLAGS: _IOW(0x80, 1, ...) - 4 bytes */
struct kbase_ioctl_set_flags {
    uint32_t create_flags;
};

/* GET_GPUPROPS: _IOW(0x80, 3, ...) - 16 bytes */
struct kbase_ioctl_get_gpuprops {
    uint64_t buffer;
    uint32_t size;
    uint32_t flags;
};

/*
 * MEM_ALLOC: _IOWR(0x80, 5, ...) - 32 bytes
 * 
 * Need to determine exact struct. With 32 bytes and the output we saw,
 * let me dump raw bytes to figure out the layout.
 */

/* Flag bits from kbase */
#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)
#define BASE_MEM_SAME_VA        (1ULL << 9)
#define BASE_MEM_CACHED_CPU     (1ULL << 17)

/* MEM_FREE: need to find correct NR and size */

static void hexdump(const char *label, const void *data, size_t size) {
    const uint8_t *p = data;
    printf("  %s (%zu bytes): ", label, size);
    for (size_t i = 0; i < size; i++) {
        if (i % 8 == 0 && i > 0) printf(" | ");
        printf("%02x ", p[i]);
    }
    printf("\n");
    /* Also as u64 */
    const uint64_t *q = data;
    printf("  as u64: ");
    for (size_t i = 0; i < size/8; i++)
        printf("[%zu]=0x%016llx ", i, (unsigned long long)q[i]);
    printf("\n");
}

int main(void) {
    int fd, ret;
    uint8_t buf[128];

    printf("=== Mali kbase r49 Full Test ===\n");
    printf("Magic: 0x%02x\n\n", KBASE_IOCTL_TYPE);

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n\n", fd);

    /* === VERSION_CHECK === */
    struct kbase_ioctl_version_check ver = { .major = 11, .minor = 13 };
    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, sizeof(ver)), &ver);
    printf("[%s] VERSION_CHECK: ret=%d major=%u minor=%u\n",
           ret >= 0 ? "OK" : "FAIL", ret, ver.major, ver.minor);
    if (ret < 0) { close(fd); return 1; }

    /* === SET_FLAGS === */
    struct kbase_ioctl_set_flags flags = { .create_flags = 0 };
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 1, sizeof(flags)), &flags);
    printf("[%s] SET_FLAGS: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);
    if (ret < 0) { close(fd); return 1; }

    /* === GET_GPUPROPS (query size) === */
    struct kbase_ioctl_get_gpuprops props = { .buffer = 0, .size = 0, .flags = 0 };
    ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 3, sizeof(props)), &props);
    printf("[%s] GET_GPUPROPS (size query): ret=%d (props_size=%d)\n",
           ret > 0 ? "OK" : "FAIL", ret, ret);

    if (ret > 0) {
        int props_size = ret;
        uint8_t *propbuf = calloc(1, props_size);
        if (propbuf) {
            struct kbase_ioctl_get_gpuprops props2 = {
                .buffer = (uint64_t)(uintptr_t)propbuf,
                .size = props_size,
                .flags = 0
            };
            ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 3, sizeof(props2)), &props2);
            printf("[%s] GET_GPUPROPS (fetch): ret=%d\n", ret > 0 ? "OK" : "FAIL", ret);
            if (ret > 0) {
                /* Dump first 64 bytes */
                printf("  Props data (first 64 bytes):\n");
                for (int i = 0; i < 64 && i < ret; i++) {
                    if (i % 16 == 0) printf("  %04x: ", i);
                    printf("%02x ", propbuf[i]);
                    if (i % 16 == 15) printf("\n");
                }
                printf("\n");
            }
            free(propbuf);
        }
    }

    /* === MEM_ALLOC (32 bytes, IOWR) === */
    printf("\n--- MEM_ALLOC exploration ---\n");
    memset(buf, 0, 32);
    uint64_t *u = (uint64_t *)buf;
    u[0] = 1;    /* va_pages = 1 */
    u[1] = 1;    /* commit_pages = 1 */
    u[2] = 0;    /* extension */
    u[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
           BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;

    printf("  IN: ");
    hexdump("before", buf, 32);

    ret = ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32), buf);
    printf("[%s] MEM_ALLOC: ret=%d errno=%d (%s)\n",
           ret >= 0 ? "OK" : "FAIL", ret, errno, strerror(errno));

    if (ret >= 0) {
        hexdump("after", buf, 32);

        /* The output data should tell us the GPU VA.
         * In standard kbase, gpu_va is returned in flags field or 
         * as a separate output field. Let's try mmap with different offsets. */
        uint64_t gpu_va_candidate1 = u[0];  /* va_pages might become gpu_va */
        uint64_t gpu_va_candidate2 = u[3];  /* flags might contain gpu_va */

        printf("\n--- Trying mmap ---\n");

        /* Standard kbase mmap: offset = gpu_va pages */
        /* The gpu_va is typically the page number, so offset = gpu_va * page_size
         * but actually in kbase, the mmap offset IS the gpu_va. */

        /* Try mmap with offset 0 first (SAME_VA would use this) */
        void *map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (map != MAP_FAILED) {
            printf("[OK] mmap(offset=0) = %p\n", map);
            /* Try writing */
            memset(map, 0x42, 64);
            printf("  Write OK\n");
            munmap(map, 4096);
        } else {
            printf("[FAIL] mmap(offset=0): errno=%d (%s)\n", errno, strerror(errno));
        }

        /* Try offset = va_pages output * 4096 */
        if (gpu_va_candidate1 > 0 && gpu_va_candidate1 < 0x100000) {
            off_t off = gpu_va_candidate1 * 4096;
            map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, off);
            if (map != MAP_FAILED) {
                printf("[OK] mmap(offset=0x%lx [va_pages*4K]) = %p\n", (long)off, map);
                memset(map, 0x42, 64);
                printf("  Write OK\n");
                munmap(map, 4096);
            } else {
                printf("[FAIL] mmap(offset=0x%lx): errno=%d (%s)\n", (long)off, errno, strerror(errno));
            }
        }

        /* In standard kbase: offset = gpu_va directly (not page-shifted) */
        if (gpu_va_candidate1 > 0) {
            off_t off = gpu_va_candidate1;
            map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, off);
            if (map != MAP_FAILED) {
                printf("[OK] mmap(offset=0x%lx [raw va_pages]) = %p\n", (long)off, map);
                munmap(map, 4096);
            } else {
                printf("[FAIL] mmap(offset=0x%lx [raw]): errno=%d\n", (long)off, errno);
            }
        }
    }

    /* === MEM_FREE (NR=7, try size 8) === */
    printf("\n--- MEM_FREE exploration ---\n");
    if (ret >= 0) {
        memset(buf, 0, 64);
        u[0] = ((uint64_t *)buf)[0]; /* gpu_va from alloc */

        int free_sizes[] = {8, 16, 24, 32};
        for (int i = 0; i < 4; i++) {
            /* Each test on same fd, no need to reopen */
            memset(buf, 0, 64);
            u[0] = 0; /* gpu_va or handle from alloc */
            errno = 0;
            ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 7, free_sizes[i]), buf);
            printf("  MEM_FREE sz=%d: ret=%d errno=%d (%s)\n",
                   free_sizes[i], ret, errno, strerror(errno));
        }
    }

    /* === JOB_SUBMIT (NR=2) === */
    printf("\n--- JOB_SUBMIT size scan ---\n");
    int submit_sizes[] = {8, 16, 24, 32, 40, 48, 56, 64, 72};
    for (int i = 0; i < 9; i++) {
        memset(buf, 0, 128);
        errno = 0;
        ret = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 2, submit_sizes[i]), buf);
        int e = errno;
        /* ENOTTY = wrong size, EINVAL = right size wrong data */
        printf("  JOB_SUBMIT sz=%d: ret=%d errno=%d (%s)%s\n",
               submit_sizes[i], ret, e, strerror(e),
               e == 22 ? " ← size matched!" : "");
    }

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}
