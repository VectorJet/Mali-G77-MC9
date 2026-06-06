// Manual MDLA conv2d_depthwise replay — uses libapu_mdw.so only (no libmdla_ut).
//
// Reads the captured pattern files, allocates the same buffer set, queries each
// memAlloc DVA, patches the 448-byte cmd stream with the resolved DVAs, and
// submits to MDLA via apusysCmd_build/run/wait. Compares the output against the
// golden, masked by the golden mask.
//
// Build on device: gcc -o mdla_replay mdla_replay.c -ldl
// Run with vendor namespace: copy to /data/local/tmp/ and execute under su.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>

#define PATTERN_DIR "/data/local/tmp/mdla_data"

/* apusys_mem_info has DVA at offset 0 (32-bit). Output struct is 16 bytes
 * based on stack usage in libapu_mdw::apusysCmd_v3::submit. */
struct apusys_mem_info {
    uint32_t dva;
    uint32_t size;
    uint32_t flags;
    uint32_t kva;
};

typedef void *(*fn_createInstance)(void);
typedef int   (*fn_deleteInstance)(void *);
typedef void *(*fn_createCmd)(void *);
typedef int   (*fn_deleteCmd)(void *, void *);
typedef void *(*fn_createSubcmd)(void *, int);
/* The published apusysSession_memAlloc thin wrapper only zero-extends w1/w2 and
 * sets x5=NULL. Callers set x3,x4 themselves before the call. The internal C++
 * signature is memAlloc(this, ulong, ulong, type, ulong, char*). So from C we
 * call it with (sess, size, align, pad, type) and the registers line up. */
typedef void *(*fn_memAlloc)(void *sess, uint32_t size, uint32_t align,
                              uint32_t pad, uint32_t type);
typedef int   (*fn_memFree)(void *, void *);
typedef void *(*fn_cmdBufAlloc)(void *sess, uint32_t size, uint32_t flag);
typedef int   (*fn_cmdBufFree)(void *, void *);
typedef int   (*fn_subCmd_addCmdBuf)(void *, void *, int);
typedef int   (*fn_cmd_build)(void *);
typedef int   (*fn_cmd_run)(void *);
typedef int   (*fn_cmd_wait)(void *);
/* memGetInfoFromHostPtr internally is `memGetInfoFromHostPtr(void *host_ptr,
 * apusys_mem_info)` — the struct passes "by value" through registers.
 * In practice libmdla_ut calls with (sess, host_ptr, type=1) and uses the
 * return value in x0 directly. The struct fields end up returned via x0/x1. */
typedef uint64_t (*fn_memGetInfo)(void *sess, void *host_ptr, uint32_t type);

static fn_createInstance      createInstance;
static fn_deleteInstance      deleteInstance;
static fn_createCmd           createCmd;
static fn_deleteCmd           deleteCmd;
static fn_createSubcmd        createSubcmd;
static fn_memAlloc            memAlloc;
static fn_memFree             memFree;
static fn_cmdBufAlloc         cmdBufAlloc;
static fn_cmdBufFree          cmdBufFree;
static fn_subCmd_addCmdBuf    addCmdBuf;
static fn_cmd_build           cmd_build;
static fn_cmd_run             cmd_run;
static fn_cmd_wait            cmd_wait;
static fn_memGetInfo          memGetInfoFromHostPtr;

static int load_file(const char *name, void *dest, size_t expect) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s", PATTERN_DIR, name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "[FAIL] open %s: %s\n", path, strerror(errno)); return -1; }
    struct stat st; fstat(fd, &st);
    if ((size_t)st.st_size != expect) {
        fprintf(stderr, "[FAIL] %s: size %lld != expected %zu\n",
                path, (long long)st.st_size, expect);
        close(fd); return -1;
    }
    ssize_t n = read(fd, dest, expect);
    close(fd);
    if (n != (ssize_t)expect) { fprintf(stderr, "[FAIL] short read %s\n", name); return -1; }
    fprintf(stderr, "[OK] loaded %s (%zu bytes)\n", name, expect);
    return 0;
}

#define RESOLVE(h, sym, var) do {                                 \
    var = dlsym(h, #sym);                                         \
    if (!var) { fprintf(stderr, "[FAIL] dlsym(%s)\n", #sym); return 1; } \
} while (0)

int main(void) {
    fprintf(stderr, "=== MDLA Manual Replay (conv2d_depthwise) ===\n");

    void *h = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "[FAIL] dlopen libapu_mdw: %s\n", dlerror()); return 1; }
    fprintf(stderr, "[OK] libapu_mdw.so loaded\n");

    RESOLVE(h, apusysSession_createInstance,         createInstance);
    RESOLVE(h, apusysSession_deleteInstance,         deleteInstance);
    RESOLVE(h, apusysSession_createCmd,              createCmd);
    RESOLVE(h, apusysSession_deleteCmd,              deleteCmd);
    RESOLVE(h, apusysCmd_createSubcmd,               createSubcmd);
    RESOLVE(h, apusysSession_memAlloc,               memAlloc);
    RESOLVE(h, apusysSession_memFree,                memFree);
    RESOLVE(h, apusysSession_cmdBufAlloc,            cmdBufAlloc);
    RESOLVE(h, apusysSession_cmdBufFree,             cmdBufFree);
    RESOLVE(h, apusysSubCmd_addCmdBuf,               addCmdBuf);
    RESOLVE(h, apusysCmd_build,                      cmd_build);
    RESOLVE(h, apusysCmd_run,                        cmd_run);
    RESOLVE(h, apusysCmd_wait,                       cmd_wait);
    RESOLVE(h, apusysSession_memGetInfoFromHostPtr,  memGetInfoFromHostPtr);

    void *sess = createInstance();
    fprintf(stderr, "[OK] session = %p\n", sess);
    if (!sess) return 2;

    /* Allocate the four data buffers (same args libmdla_ut::prepareData uses:
     * size, align=0x1000, pad=0, type=2). */
    void *act = memAlloc(sess, 528192, 0x1000, 0, 2);
    void *wgt = memAlloc(sess, 1536,   0x1000, 0, 2);
    void *qnt = memAlloc(sess, 128,    0x1000, 0, 2);
    void *out = memAlloc(sess, 31744,  0x1000, 0, 2);
    fprintf(stderr, "[OK] data bufs: act=%p wgt=%p qnt=%p out=%p\n", act, wgt, qnt, out);
    if (!act || !wgt || !qnt || !out) return 3;

    /* Populate inputs */
    if (load_file("conv2d_depthwise_Activation_1.bin", act, 528192)) return 4;
    if (load_file("conv2d_depthwise_Weight_1.bin",     wgt, 1536))   return 4;
    if (load_file("conv2d_depthwise_QuantTableAdd_1.bin", qnt, 128)) return 4;
    memset(out, 0xCD, 31744);

    /* Query DVAs (libmdla_ut passes type=1) */
    uint64_t r_act = memGetInfoFromHostPtr(sess, act, 1);
    uint64_t r_wgt = memGetInfoFromHostPtr(sess, wgt, 1);
    uint64_t r_qnt = memGetInfoFromHostPtr(sess, qnt, 1);
    uint64_t r_out = memGetInfoFromHostPtr(sess, out, 1);
    uint32_t dva_act = (uint32_t)r_act;
    uint32_t dva_wgt = (uint32_t)r_wgt;
    uint32_t dva_qnt = (uint32_t)r_qnt;
    uint32_t dva_out = (uint32_t)r_out;
    fprintf(stderr, "[DVA] act=0x%016llx wgt=0x%016llx qnt=0x%016llx out=0x%016llx\n",
            (unsigned long long)r_act, (unsigned long long)r_wgt,
            (unsigned long long)r_qnt, (unsigned long long)r_out);

    /* Allocate the 72-byte info cmd buf (mdlaCmd::prepareData allocs this first).
     * Captured layout: u32[2] (off 0x08) = cmd_size (0x1c0); u32[5] (off 0x14) = count (1). */
    void *info_cb = cmdBufAlloc(sess, 72, 0x100);
    fprintf(stderr, "[OK] info cmd buf = %p\n", info_cb);
    if (!info_cb) return 5;
    memset(info_cb, 0, 72);
    ((uint32_t *)info_cb)[2] = 0x1c0;  /* cmd_size */
    ((uint32_t *)info_cb)[5] = 1;      /* count */
    /* Allocate cmd buffer and load Command.bin */
    void *cb = cmdBufAlloc(sess, 448, 0x100);
    fprintf(stderr, "[OK] cmd buf = %p\n", cb);
    if (!cb) return 5;
    if (load_file("conv2d_depthwise_Command.bin", cb, 448)) return 6;

    /* Patch DVAs at the four known offsets */
    uint32_t *cmd32 = (uint32_t *)cb;
    fprintf(stderr, "[*] patching cmd: [0x000]=0x%08x->0x%08x (act)\n", cmd32[0], dva_act);
    cmd32[0]      = dva_act;
    fprintf(stderr, "[*] patching cmd: [0x004]=0x%08x->0x%08x (qnt)\n", cmd32[1], dva_qnt);
    cmd32[1]      = dva_qnt;
    fprintf(stderr, "[*] patching cmd: [0x008]=0x%08x->0x%08x (out)\n", cmd32[2], dva_out);
    cmd32[2]      = dva_out;
    fprintf(stderr, "[*] patching cmd: [0x054]=0x%08x->0x%08x (wgt)\n", cmd32[0x54/4], dva_wgt);
    cmd32[0x54/4] = dva_wgt;
    /* libmdla_ut sets cmd[0x154] = 0x01000000 BEFORE submit (verified via
     * cap_before_buf_5 vs embedded Command.bin diff). Not kernel-written. */
    fprintf(stderr, "[*] patching cmd: [0x154]=0x%08x->0x01000000\n", cmd32[0x154/4]);
    cmd32[0x154/4] = 0x01000000;

    /* Build and submit */
    void *cmd = createCmd(sess);
    void *sub = createSubcmd(cmd, 2);  /* 2 = MDLA */
    fprintf(stderr, "[OK] cmd=%p subcmd=%p\n", cmd, sub);
    if (!cmd || !sub) return 7;
    int rc = addCmdBuf(sub, cb, 0);
    fprintf(stderr, "[*] addCmdBuf(cb, mode=0) rc=%d\n", rc);
    rc = addCmdBuf(sub, info_cb, 1);
    fprintf(stderr, "[*] addCmdBuf(info, mode=1) rc=%d\n", rc);
    rc = cmd_build(cmd);
    fprintf(stderr, "[*] cmd_build rc=%d\n", rc);
    rc = cmd_run(cmd);
    fprintf(stderr, "[*] cmd_run  rc=%d\n", rc);
    rc = cmd_wait(cmd);
    fprintf(stderr, "[*] cmd_wait rc=%d\n", rc);

    /* Compare against golden */
    unsigned char *golden = malloc(31744), *mask = malloc(31744);
    if (load_file("conv2d_depthwise_Golden_1.bin",      golden, 31744) ||
        load_file("conv2d_depthwise_Golden_Mask_1.bin", mask,   31744)) {
        return 8;
    }
    int mismatch = 0;
    unsigned char *o = out;
    for (int i = 0; i < 31744; i++) {
        if (mask[i] && o[i] != golden[i]) {
            if (mismatch < 8) {
                fprintf(stderr, "  diff[%d]: out=0x%02x golden=0x%02x mask=0x%02x\n",
                        i, o[i], golden[i], mask[i]);
            }
            mismatch++;
        }
    }
    fprintf(stderr, "=== Comparison: %d masked bytes mismatch (%s) ===\n",
            mismatch, mismatch == 0 ? "PASS ✓" : "FAIL");

    cmdBufFree(sess, cb);
    memFree(sess, act); memFree(sess, wgt); memFree(sess, qnt); memFree(sess, out);
    deleteCmd(sess, cmd);
    deleteInstance(sess);
    return mismatch == 0 ? 0 : 9;
}
