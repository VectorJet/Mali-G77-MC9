// LD_PRELOAD shim for /dev/apusys ioctls.
// Dumps full request payloads to stderr (hex+ascii) so we can see exactly what
// libapu_mdw.so packs into IOCTL 0x20/0x21/0x22.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <fcntl.h>

static int (*real_ioctl)(int, int, ...) = NULL;
static int (*real_open)(const char *, int, ...) = NULL;
static int (*real_openat)(int, const char *, int, ...) = NULL;

static int g_apu_fd = -1;

__attribute__((constructor))
static void hook_init(void) {
    fprintf(stderr, "[HOOK] preload constructor running, pid=%d\n", getpid());
}

static void hexdump(const char *tag, int fd, unsigned int req, void *p, size_t n) {
    if (!p || n == 0) {
        fprintf(stderr, "[HOOK] %s fd=%d req=0x%x (no payload)\n", tag, fd, req);
        return;
    }
    fprintf(stderr, "[HOOK] %s fd=%d req=0x%x size=%zu\n", tag, fd, req, n);
    unsigned char *b = (unsigned char *)p;
    for (size_t i = 0; i < n; i += 16) {
        fprintf(stderr, "  %04zx  ", i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < n) fprintf(stderr, "%02x ", b[i + j]);
            else           fprintf(stderr, "   ");
        }
        fprintf(stderr, " |");
        for (size_t j = 0; j < 16 && i + j < n; j++) {
            unsigned char c = b[i + j];
            fputc((c >= 32 && c < 127) ? c : '.', stderr);
        }
        fprintf(stderr, "|\n");
    }
}

static void track_apu_open(int fd, const char *path) {
    if (path && strstr(path, "/dev/apusys")) {
        g_apu_fd = fd;
        fprintf(stderr, "[HOOK] /dev/apusys opened, fd=%d\n", fd);
    }
}

int open(const char *path, int flags, ...) {
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    }
    int fd = real_open(path, flags, mode);
    if (fd >= 0) track_apu_open(fd, path);
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...) {
    if (!real_openat) real_openat = dlsym(RTLD_NEXT, "openat");
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    }
    int fd = real_openat(dirfd, path, flags, mode);
    if (fd >= 0) track_apu_open(fd, path);
    return fd;
}

int ioctl(int fd, int op, ...) {
    if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    void *arg;
    va_list ap; va_start(ap, op); arg = va_arg(ap, void *); va_end(ap);
    unsigned int req = (unsigned int)op;

    if (fd == g_apu_fd) {
        unsigned int nr   = (req >> 0)  & 0xff;
        unsigned int magic= (req >> 8)  & 0xff;
        unsigned int sz   = (req >> 16) & 0x3fff;
        if (magic == 0x41) {
            char tag[32];
            snprintf(tag, sizeof tag, "BEFORE nr=0x%02x", nr);
            hexdump(tag, fd, req, arg, sz);
        }
    }

    int rc = real_ioctl(fd, op, arg);

    if (fd == g_apu_fd) {
        unsigned int nr   = (req >> 0)  & 0xff;
        unsigned int magic= (req >> 8)  & 0xff;
        unsigned int sz   = (req >> 16) & 0x3fff;
        if (magic == 0x41) {
            char tag[32];
            snprintf(tag, sizeof tag, "AFTER  nr=0x%02x rc=%d", nr, rc);
            hexdump(tag, fd, req, arg, sz);
        }
    }
    return rc;
}
