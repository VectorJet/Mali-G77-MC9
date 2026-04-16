#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>

typedef int (*gpud_init_t)(void);

int main(int argc, char *argv[]) {
    void *handle;
    gpud_init_t gpudInitialize;
    char *error;
    int ret;

    printf("=== Load and call libgpud.so ===\n");

    /* Load libgpud.so */
    handle = dlopen("/vendor/lib64/libgpud.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    printf("[OK] Loaded libgpud.so\n");

    /* Get gpudInitialize */
    gpudInitialize = (gpud_init_t)dlsym(handle, "gpudInitialize");
    error = dlerror();
    if (error) {
        fprintf(stderr, "dlsym failed: %s\n", error);
        dlclose(handle);
        return 1;
    }
    printf("[OK] Found gpudInitialize\n");

    /* Call it */
    printf("Calling gpudInitialize...\n");
    ret = gpudInitialize();
    printf("gpudInitialize returned: %d\n", ret);

    /* Now try /dev/mali0 */
    printf("\n=== Try /dev/mali0 after gpudInitialize ===\n");
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
    } else {
        printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

        /* Try VERSION_CHECK */
        struct { uint32_t major; uint32_t minor; } ver = {11, 13};
        ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 0, 8), &ver);
        printf("VERSION_CHECK: ret=%d, major=%u, minor=%u, errno=%d (%s)\n", 
               ret, ver.major, ver.minor, errno, strerror(errno));

        /* Try SET_FLAGS */
        uint32_t flags = 0;
        ret = ioctl(fd, _IOC(_IOC_WRITE, 0x67, 1, 4), &flags);
        printf("SET_FLAGS: ret=%d, errno=%d (%s)\n", ret, errno, strerror(errno));

        close(fd);
    }

    dlclose(handle);
    printf("\nDone.\n");
    return 0;
}