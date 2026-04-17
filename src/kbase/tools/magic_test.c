/*
 * Test: kbase r49 uses KBASE_IOCTL_TYPE = 0x80, NOT 0x67
 *
 * The VERSION_CHECK ioctl is:
 *   _IOWR(0x80, 0, struct { u16 major; u16 minor; })  = 0xc0048000
 *
 * We've been sending:
 *   _IOW(0x67, 0, struct { u32 major; u32 minor; })   = 0x40086700
 *
 * Both the magic AND the struct size were wrong!
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>

/* r49 kbase uses IOCTL_TYPE 0x80 */
#define KBASE_IOCTL_TYPE 0x80

/* VERSION_CHECK: _IOWR(0x80, 0, struct kbase_ioctl_version_check) */
struct kbase_ioctl_version_check {
    uint16_t major;  /* in/out */
    uint16_t minor;  /* in/out */
};

/* SET_FLAGS: _IOW(0x80, 1, struct kbase_ioctl_set_flags) */
struct kbase_ioctl_set_flags {
    uint32_t create_flags;
};

/* GET_GPUPROPS: _IOW(0x80, 3, struct kbase_ioctl_get_gpuprops) */
struct kbase_ioctl_get_gpuprops {
    uint64_t buffer;
    uint32_t size;
    uint32_t flags;
};

/* MEM_ALLOC: _IOWR(0x80, 5, ...) */
struct kbase_ioctl_mem_alloc {
    uint64_t va_pages;
    uint64_t commit_pages;
    uint64_t extension;
    uint64_t flags;
    uint64_t gpu_va;
};

int main(void) {
    int fd, ret;

    printf("=== Mali kbase r49 Magic Test ===\n\n");

    /* Show the encoded ioctl values */
    printf("Ioctl encodings:\n");
    printf("  VERSION_CHECK _IOWR(0x80,0,4B) = 0x%08lx\n",
           (unsigned long)_IOC(_IOC_READ|_IOC_WRITE, 0x80, 0,
                               sizeof(struct kbase_ioctl_version_check)));
    printf("  VERSION_CHECK _IOW(0x67,0,8B)  = 0x%08lx  (OLD WRONG)\n",
           (unsigned long)_IOC(_IOC_WRITE, 0x67, 0, 8));
    printf("  SET_FLAGS _IOW(0x80,1,4B)      = 0x%08lx\n",
           (unsigned long)_IOC(_IOC_WRITE, 0x80, 1,
                               sizeof(struct kbase_ioctl_set_flags)));
    printf("  MEM_ALLOC _IOWR(0x80,5,40B)    = 0x%08lx\n",
           (unsigned long)_IOC(_IOC_READ|_IOC_WRITE, 0x80, 5,
                               sizeof(struct kbase_ioctl_mem_alloc)));
    printf("\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n\n", fd);

    /* VERSION_CHECK with correct magic 0x80 and correct struct (u16+u16) */
    struct kbase_ioctl_version_check ver = { .major = 11, .minor = 13 };
    unsigned long cmd_ver = _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0,
                                 sizeof(struct kbase_ioctl_version_check));
    errno = 0;
    ret = ioctl(fd, cmd_ver, &ver);
    printf("VERSION_CHECK (0x%08lx): ret=%d, major=%u, minor=%u, errno=%d (%s)\n",
           cmd_ver, ret, ver.major, ver.minor, errno, strerror(errno));

    if (ret < 0) {
        printf("\nFailed with magic 0x80 too. Trying more variants...\n\n");

        /* Maybe the struct has u32 fields? */
        close(fd);
        fd = open("/dev/mali0", O_RDWR);

        struct { uint32_t major; uint32_t minor; } ver32 = { 11, 13 };
        unsigned long cmd_ver32 = _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 8);
        errno = 0;
        ret = ioctl(fd, cmd_ver32, &ver32);
        printf("VERSION_CHECK u32 (0x%08lx): ret=%d, major=%u, minor=%u, errno=%d (%s)\n",
               cmd_ver32, ret, ver32.major, ver32.minor, errno, strerror(errno));

        if (ret < 0) {
            /* Try IOW instead of IOWR */
            close(fd);
            fd = open("/dev/mali0", O_RDWR);

            struct kbase_ioctl_version_check ver2 = { .major = 11, .minor = 13 };
            unsigned long cmd_iow = _IOC(_IOC_WRITE, 0x80, 0,
                                         sizeof(struct kbase_ioctl_version_check));
            errno = 0;
            ret = ioctl(fd, cmd_iow, &ver2);
            printf("VERSION_CHECK IOW (0x%08lx): ret=%d, major=%u, minor=%u, errno=%d (%s)\n",
                   cmd_iow, ret, ver2.major, ver2.minor, errno, strerror(errno));
        }

        close(fd);
        return 1;
    }

    printf("\n=== VERSION_CHECK WORKS! Continuing... ===\n\n");

    /* SET_FLAGS */
    struct kbase_ioctl_set_flags flags = { .create_flags = 0 };
    unsigned long cmd_flags = _IOC(_IOC_WRITE, 0x80, 1,
                                    sizeof(struct kbase_ioctl_set_flags));
    errno = 0;
    ret = ioctl(fd, cmd_flags, &flags);
    printf("SET_FLAGS (0x%08lx): ret=%d, errno=%d (%s)\n",
           cmd_flags, ret, errno, strerror(errno));

    /* GET_GPUPROPS - query size first */
    struct kbase_ioctl_get_gpuprops props = { .buffer = 0, .size = 0, .flags = 0 };
    unsigned long cmd_props = _IOC(_IOC_WRITE, 0x80, 3,
                                    sizeof(struct kbase_ioctl_get_gpuprops));
    errno = 0;
    ret = ioctl(fd, cmd_props, &props);
    printf("GET_GPUPROPS (0x%08lx): ret=%d (size=%d), errno=%d (%s)\n",
           cmd_props, ret, ret, errno, strerror(errno));

    /* MEM_ALLOC */
    struct kbase_ioctl_mem_alloc mem = {0};
    mem.va_pages = 1;
    mem.commit_pages = 1;
    mem.extension = 0;
    mem.flags = (1 << 0) | (1 << 1); /* SAME_VA | GPU_RD */
    unsigned long cmd_mem = _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5,
                                  sizeof(struct kbase_ioctl_mem_alloc));
    errno = 0;
    ret = ioctl(fd, cmd_mem, &mem);
    printf("MEM_ALLOC (0x%08lx): ret=%d, gpu_va=0x%llx, errno=%d (%s)\n",
           cmd_mem, ret, (unsigned long long)mem.gpu_va, errno, strerror(errno));

    close(fd);
    printf("\nDone.\n");
    return 0;
}
