// Use the proprietary C API for APU and let strace capture the real ioctl payloads.
// Build on device, run under strace -e ioctl to see exactly how libapu_mdw populates
// the 152-byte job submission and the 76-byte context buffer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>

typedef void* (*fn_createInstance_t)(void);
typedef int   (*fn_deleteInstance_t)(void*);
typedef void* (*fn_createCmd_t)(void*);
typedef int   (*fn_deleteCmd_t)(void*, void*);
typedef void* (*fn_createSubcmd_t)(void*, int /*device_type*/);
typedef void* (*fn_cmdBufAlloc_t)(void*, size_t);
typedef int   (*fn_cmdBufFree_t)(void*, void*);
typedef int   (*fn_subCmd_addCmdBuf_t)(void*, void*, int /*mode*/);
typedef int   (*fn_cmd_build_t)(void*);
typedef int   (*fn_cmd_run_t)(void*);
typedef int   (*fn_cmd_wait_t)(void*);

#define R(sym) printf("  " #sym " = %p\n", sym)

int main(int argc, char **argv) {
    printf("=== APU C API Probe ===\n");
    printf("[*] PID = %d\n", getpid());

    void *h = dlopen("libapu_mdw.so", RTLD_NOW);
    if (!h) h = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!h) {
        printf("[FAIL] dlopen: %s\n", dlerror());
        return 1;
    }
    printf("[OK] libapu_mdw.so loaded\n");

    fn_createInstance_t createInstance = dlsym(h, "apusysSession_createInstance");
    fn_deleteInstance_t deleteInstance = dlsym(h, "apusysSession_deleteInstance");
    fn_createCmd_t      createCmd      = dlsym(h, "apusysSession_createCmd");
    fn_deleteCmd_t      deleteCmd      = dlsym(h, "apusysSession_deleteCmd");
    fn_createSubcmd_t   createSubcmd   = dlsym(h, "apusysCmd_createSubcmd");
    fn_cmdBufAlloc_t    cmdBufAlloc    = dlsym(h, "apusysSession_cmdBufAlloc");
    fn_cmdBufFree_t     cmdBufFree     = dlsym(h, "apusysSession_cmdBufFree");
    fn_subCmd_addCmdBuf_t addCmdBuf    = dlsym(h, "apusysSubCmd_addCmdBuf");
    fn_cmd_build_t      cmd_build      = dlsym(h, "apusysCmd_build");
    fn_cmd_run_t        cmd_run        = dlsym(h, "apusysCmd_run");
    fn_cmd_wait_t       cmd_wait       = dlsym(h, "apusysCmd_wait");

    R(createInstance); R(deleteInstance);
    R(createCmd); R(deleteCmd); R(createSubcmd);
    R(cmdBufAlloc); R(cmdBufFree); R(addCmdBuf);
    R(cmd_build); R(cmd_run); R(cmd_wait);

    if (!createInstance) {
        printf("[FAIL] missing apusysSession_createInstance\n");
        return 1;
    }
    void *sess = createInstance();
    printf("[OK] session = %p\n", sess);
    if (!sess) return 2;

    void *cmd = createCmd ? createCmd(sess) : NULL;
    printf("[OK] cmd = %p\n", cmd);
    if (!cmd) goto out;

    // Enumerate ALL valid device_type values (apusys_device_type)
    int valid_devs[16]; int nvalid = 0;
    for (int dev = 0; dev < 16; dev++) {
        void *c2 = createCmd(sess);
        if (!c2) continue;
        void *s = createSubcmd ? createSubcmd(c2, dev) : NULL;
        printf("    createSubcmd(dev=%d) = %p\n", dev, s);
        if (s) valid_devs[nvalid++] = dev;
        deleteCmd(sess, c2);
    }
    printf("[*] %d valid device types\n", nvalid);

    int chosen_dev = (argc > 1) ? atoi(argv[1]) : (nvalid ? valid_devs[0] : 1);
    size_t cb_size = (argc > 2) ? (size_t)atoi(argv[2]) : 4096;
    printf("[*] using dev=%d, cb_size=%zu\n", chosen_dev, cb_size);

    void *sub = createSubcmd(cmd, chosen_dev);
    printf("[OK] subcmd = %p\n", sub);

    void *cb = cmdBufAlloc ? cmdBufAlloc(sess, cb_size) : NULL;
    printf("[OK] cmdBufAlloc(%zu) = %p\n", cb_size, cb);

    if (sub && cb && addCmdBuf) {
        int rc = addCmdBuf(sub, cb, 0);
        printf("[OK] addCmdBuf rc=%d\n", rc);
    }

    /* Pre-fill cmd buffer with a magic so we can see what lib overwrites */
    if (cb) memset(cb, 0xCD, cb_size);

    if (cmd_build) printf("[OK] cmd_build rc=%d\n", cmd_build(cmd));

    /* Dump cmd buffer contents AFTER build (lib may write a header) */
    if (cb) {
        printf("[*] cmd buffer contents after build (first 256 bytes):\n");
        unsigned char *b = (unsigned char *)cb;
        size_t n = cb_size < 256 ? cb_size : 256;
        for (size_t i = 0; i < n; i += 16) {
            printf("  %04zx ", i);
            for (size_t j = 0; j < 16 && i + j < n; j++) printf("%02x ", b[i + j]);
            printf("\n");
        }
    }

    if (cmd_run)   printf("[OK] cmd_run   rc=%d\n", cmd_run(cmd));
    if (cmd_wait)  printf("[OK] cmd_wait  rc=%d\n", cmd_wait(cmd));

    /* Dump cmd buffer contents AFTER run (kernel may have written results) */
    if (cb) {
        printf("[*] cmd buffer contents after run (first 256 bytes):\n");
        unsigned char *b = (unsigned char *)cb;
        size_t n = cb_size < 256 ? cb_size : 256;
        for (size_t i = 0; i < n; i += 16) {
            printf("  %04zx ", i);
            for (size_t j = 0; j < 16 && i + j < n; j++) printf("%02x ", b[i + j]);
            printf("\n");
        }
    }

    if (cb && cmdBufFree) cmdBufFree(sess, cb);
    if (cmd && deleteCmd) deleteCmd(sess, cmd);
out:
    if (deleteInstance) deleteInstance(sess);
    dlclose(h);
    return 0;
}
