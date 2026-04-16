/*
 * Replay the exact ioctl sequence observed from strace.
 *
 * Tries BOTH magic numbers:
 *   Phase 1: MTK magic 0x80 sequence (0x18, 0x16, 0x6, 0x1d, 0x5, 0x14)
 *   Phase 2: Standard kbase 0x67 sequence (VERSION_CHECK, SET_FLAGS, JOB_SUBMIT)
 *   Phase 3: Reversed - kbase first, then MTK
 *
 * Also tests: what if the NRs from strace ARE the kbase NRs but strace
 * decoded the magic wrong? We test both interpretations.
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

/* Buffers large enough for any ioctl */
static uint8_t buf[256];

static const char *err_name(int e) {
    switch (e) {
        case 0:  return "OK";
        case 1:  return "EPERM";
        case 13: return "EACCES";
        case 22: return "EINVAL";
        case 25: return "ENOTTY";
        default: return strerror(e);
    }
}

static int try_ioctl(int fd, const char *name, unsigned long cmd, void *arg) {
    errno = 0;
    int ret = ioctl(fd, cmd, arg);
    int e = errno;
    unsigned int dir   = _IOC_DIR(cmd);
    unsigned int magic = _IOC_TYPE(cmd);
    unsigned int nr    = _IOC_NR(cmd);
    unsigned int sz    = _IOC_SIZE(cmd);

    printf("  %-30s cmd=0x%08lx (magic=0x%02x nr=0x%02x sz=%u) ret=%d %s\n",
           name, cmd, magic, nr, sz, ret, ret < 0 ? err_name(e) : "OK");

    if (sz > 0 && arg && ret >= 0) {
        uint8_t *p = arg;
        printf("    data: ");
        for (unsigned i = 0; i < sz && i < 64; i++)
            printf("%02x ", p[i]);
        printf("\n");
    }
    return ret;
}

static void test_phase(int fd, const char *label) {
    printf("\n=== %s ===\n", label);
}

int main(int argc, char *argv[]) {
    int fd, ret;

    printf("=== Mali Ioctl Sequence Replay ===\n");
    printf("pid=%d uid=%d\n\n", getpid(), getuid());

    /*
     * PHASE 0: Check raw device access
     */
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /*
     * PHASE 1: MTK magic 0x80 sequence from strace
     * (exactly as lawnchair does it)
     */
    test_phase(fd, "Phase 1: MTK magic 0x80 sequence");

    /* 0x18 - version/handshake (32 bytes, W) */
    memset(buf, 0, sizeof(buf));
    /* Try with version 11.13 in first 8 bytes */
    uint32_t *u32 = (uint32_t *)buf;
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "MTK 0x18 (ver, W, 32)",
              _IOC(_IOC_WRITE, 0x80, 0x18, 0x20), buf);

    /* Also try RW variant */
    memset(buf, 0, sizeof(buf));
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "MTK 0x18 (ver, RW, 32)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x18, 0x20), buf);

    /* 0x16 - set flags/context (24 bytes, RW) */
    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x16 (flags, RW, 24)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x16, 0x18), buf);

    /* 0x6 - unknown (16 bytes, RW) */
    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x06 (query, RW, 16)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x06, 0x10), buf);

    /* 0x1d - unknown (16 bytes, W) */
    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x1d (init, W, 16)",
              _IOC(_IOC_WRITE, 0x80, 0x1d, 0x10), buf);

    /* 0x5 - memory (32 bytes, RW) */
    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x05 (mem, RW, 32)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x05, 0x20), buf);

    /* 0x14 - finalize (16 bytes, W) */
    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x14 (fin, W, 16)",
              _IOC(_IOC_WRITE, 0x80, 0x14, 0x10), buf);

    close(fd);

    /*
     * PHASE 2: Standard kbase 0x67 after fresh open
     */
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("reopen"); return 1; }

    test_phase(fd, "Phase 2: Standard kbase 0x67 (fresh fd)");

    memset(buf, 0, sizeof(buf));
    u32 = (uint32_t *)buf;
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "KBASE VERSION_CHECK (W, 8)",
              _IOC(_IOC_WRITE, 0x67, 0, 8), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "KBASE SET_FLAGS (W, 4)",
              _IOC(_IOC_WRITE, 0x67, 1, 4), buf);

    close(fd);

    /*
     * PHASE 3: MTK first, then kbase on SAME fd
     */
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("reopen"); return 1; }

    test_phase(fd, "Phase 3: MTK 0x80 first, then kbase 0x67 (same fd)");

    /* MTK handshake */
    memset(buf, 0, sizeof(buf));
    u32 = (uint32_t *)buf;
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "MTK 0x18 (W, 32)",
              _IOC(_IOC_WRITE, 0x80, 0x18, 0x20), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "MTK 0x16 (RW, 24)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0x16, 0x18), buf);

    /* Then standard kbase */
    memset(buf, 0, sizeof(buf));
    u32 = (uint32_t *)buf;
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "KBASE VERSION_CHECK (W, 8)",
              _IOC(_IOC_WRITE, 0x67, 0, 8), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "KBASE SET_FLAGS (W, 4)",
              _IOC(_IOC_WRITE, 0x67, 1, 4), buf);

    close(fd);

    /*
     * PHASE 4: What if strace decoded magic wrong?
     * Maybe the REAL magic is 0x80 and kbase NRs map differently.
     * Try the kbase NRs (0, 1, 2, 5) with magic 0x80.
     */
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("reopen"); return 1; }

    test_phase(fd, "Phase 4: kbase NRs with MTK magic 0x80");

    memset(buf, 0, sizeof(buf));
    u32 = (uint32_t *)buf;
    u32[0] = 11; u32[1] = 13;
    try_ioctl(fd, "0x80 NR=0 (ver? W, 8)",
              _IOC(_IOC_WRITE, 0x80, 0, 8), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "0x80 NR=1 (flags? W, 4)",
              _IOC(_IOC_WRITE, 0x80, 1, 4), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "0x80 NR=2 (submit? W, 48)",
              _IOC(_IOC_WRITE, 0x80, 2, 48), buf);

    memset(buf, 0, sizeof(buf));
    try_ioctl(fd, "0x80 NR=5 (mem? RW, 64)",
              _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 64), buf);

    close(fd);

    /*
     * PHASE 5: Brute-force magic scan
     * Try VERSION_CHECK (NR=0) with every common magic byte
     */
    test_phase(fd, "Phase 5: Magic number scan (NR=0, sz=8)");

    uint8_t magics[] = { 0x67, 0x80, 0x00, 0x01, 0x6b, 0x64, 0x4d, 0x47, 0x8e };
    for (int i = 0; i < (int)(sizeof(magics)/sizeof(magics[0])); i++) {
        fd = open("/dev/mali0", O_RDWR);
        if (fd < 0) continue;

        memset(buf, 0, sizeof(buf));
        u32 = (uint32_t *)buf;
        u32[0] = 11; u32[1] = 13;
        char label[64];
        snprintf(label, sizeof(label), "magic=0x%02x NR=0 sz=8", magics[i]);
        try_ioctl(fd, label,
                  _IOC(_IOC_WRITE, magics[i], 0, 8), buf);
        close(fd);
    }

    /*
     * PHASE 6: Try _IOC_NONE (no direction) variants
     * Some MTK drivers use _IO() which is direction=NONE
     */
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("reopen"); return 1; }

    test_phase(fd, "Phase 6: _IOC_NONE variants");

    try_ioctl(fd, "0x67 NR=0 NONE sz=0",
              _IOC(_IOC_NONE, 0x67, 0, 0), NULL);

    try_ioctl(fd, "0x80 NR=0 NONE sz=0",
              _IOC(_IOC_NONE, 0x80, 0, 0), NULL);

    /* Some drivers use raw numbers, not _IOC encoded */
    try_ioctl(fd, "raw 0x18",
              0x18, buf);

    try_ioctl(fd, "raw 0x16",
              0x16, buf);

    close(fd);

    printf("\n=== Done ===\n");
    return 0;
}
