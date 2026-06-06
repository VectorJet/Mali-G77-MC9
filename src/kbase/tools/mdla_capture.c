// Capture the FULLY-FIXED-UP MDLA command stream that libmdla_ut sends to the kernel.
//
// Strategy: install an ARM64 trampoline at the entry of apusysSession_cmdBufAlloc
// inside the loaded libapu_mdw.so. The trampoline jumps to our wrapper which
// records every allocated buffer's userspace pointer + size. After execute_pattern,
// we hexdump every recorded buffer — one of them is the 0x1c0-byte MDLA command
// stream with all DVAs already resolved.
//
// We also keep the original prologue bytes so the wrapper can call the real
// function via a return-trampoline (jmp back after the patched bytes).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>

typedef int (*execute_pattern_fn)(int, char **);
typedef int (*mdla_init_fn)(void);
typedef void *(*cmdBufAlloc_t)(void *sess, size_t size);

typedef int (*cmd_run_t)(void *cmd);
typedef void *(*memAlloc_t)(void *sess, size_t size);

static cmdBufAlloc_t g_real_cmdBufAlloc = NULL;
static cmd_run_t     g_real_cmd_run     = NULL;
static memAlloc_t    g_real_memAlloc    = NULL;
static unsigned char g_orig_alloc[16], g_orig_run[16], g_orig_memAlloc[16];
static void *g_alloc_target = NULL, *g_run_target = NULL, *g_memAlloc_target = NULL;

#define MAX_REC 32
struct rec { void *ptr; size_t size; void *sess; const char *kind; };
static struct rec g_records[MAX_REC];
static int g_nrec = 0;
static int g_stage = 0; /* 0=before, 1=after */

/* ARM64 trampoline (16 bytes):
 *   LDR  X16, [PC, #8]   ; 0x58000050
 *   BR   X16             ; 0xd61f0200
 *   .quad target
 */
static void write_trampoline(void *target_fn, void *hook_fn) {
    unsigned char *p = (unsigned char *)target_fn;
    uint32_t ldr = 0x58000050;
    uint32_t br  = 0xd61f0200;
    memcpy(p + 0, &ldr, 4);
    memcpy(p + 4, &br, 4);
    memcpy(p + 8, &hook_fn, 8);
    __builtin___clear_cache((char *)p, (char *)p + 16);
}

static int unlock_page(void *target_fn) {
    long pagesize = sysconf(_SC_PAGESIZE);
    void *page = (void *)((uintptr_t)target_fn & ~(pagesize - 1));
    if (mprotect(page, pagesize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("mprotect");
        return -1;
    }
    return 0;
}

static void install_trampoline(void *target_fn, void *hook_fn,
                               unsigned char *save_to, void **target_out) {
    if (unlock_page(target_fn) != 0) return;
    memcpy(save_to, target_fn, 16);
    *target_out = target_fn;
    write_trampoline(target_fn, hook_fn);
}

static void hexdump(const char *tag, struct rec *r);
static void dump_all_records(void);

/* Hook for cmdBufAlloc */
static void *hook_cmdBufAlloc(void *sess, size_t size) {
    memcpy(g_alloc_target, g_orig_alloc, 16);
    __builtin___clear_cache(g_alloc_target, (char *)g_alloc_target + 16);
    void *result = g_real_cmdBufAlloc(sess, size);
    write_trampoline(g_alloc_target, (void *)hook_cmdBufAlloc);

    if (g_nrec < MAX_REC && result) {
        g_records[g_nrec].ptr  = result;
        g_records[g_nrec].size = size;
        g_records[g_nrec].sess = sess;
        g_records[g_nrec].kind = "cmdBuf";
        g_nrec++;
        fprintf(stderr, "[CAP] cmdBufAlloc(sess=%p, size=%zu) -> %p\n", sess, size, result);
    }
    return result;
}

/* Hook for memAlloc — captures the data buffers (activation/weight/output) */
static void *hook_memAlloc(void *sess, size_t size) {
    memcpy(g_memAlloc_target, g_orig_memAlloc, 16);
    __builtin___clear_cache(g_memAlloc_target, (char *)g_memAlloc_target + 16);
    void *result = g_real_memAlloc(sess, size);
    write_trampoline(g_memAlloc_target, (void *)hook_memAlloc);

    if (g_nrec < MAX_REC && result) {
        g_records[g_nrec].ptr  = result;
        g_records[g_nrec].size = size;
        g_records[g_nrec].sess = sess;
        g_records[g_nrec].kind = "mem";
        g_nrec++;
        fprintf(stderr, "[CAP] memAlloc(sess=%p, size=%zu) -> %p\n", sess, size, result);
    }
    return result;
}

/* Hook for apusysCmd_run: dump buffers right before submission, then again after. */
static int hook_cmd_run(void *cmd) {
    fprintf(stderr, "[CAP] >>> apusysCmd_run entry: dumping buffers BEFORE submit\n");
    g_stage = 0;
    dump_all_records();

    memcpy(g_run_target, g_orig_run, 16);
    __builtin___clear_cache(g_run_target, (char *)g_run_target + 16);
    int rc = g_real_cmd_run(cmd);
    write_trampoline(g_run_target, (void *)hook_cmd_run);

    fprintf(stderr, "[CAP] <<< apusysCmd_run rc=%d: dumping buffers AFTER submit\n", rc);
    g_stage = 1;
    dump_all_records();
    return rc;
}

static void hexdump(const char *tag, struct rec *r) {
    void *ptr = r->ptr; size_t n = r->size;
    fprintf(stderr, "[CAP] %s kind=%s ptr=%p size=%zu\n", tag, r->kind, ptr, n);
    char path[160];
    snprintf(path, sizeof path, "/sdcard/cap_%s_%s_%s_%zu.bin",
             g_stage ? "after" : "before", tag, r->kind, n);
    for (char *q = path; *q; q++) if (*q == '[' || *q == ']') *q = '_';
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(ptr, 1, n, f); fclose(f); fprintf(stderr, "  saved to %s\n", path); }
    size_t dump = n < 256 ? n : 256;
    unsigned char *b = ptr;
    for (size_t i = 0; i < dump; i += 16) {
        fprintf(stderr, "  %04zx ", i);
        for (size_t j = 0; j < 16 && i + j < dump; j++) fprintf(stderr, "%02x ", b[i + j]);
        fprintf(stderr, "\n");
    }
}

static void dump_all_records(void) {
    for (int i = 0; i < g_nrec; i++) {
        char tag[32];
        snprintf(tag, sizeof tag, "buf_%d", i);
        hexdump(tag, &g_records[i]);
    }
}

int main(int argc, char **argv) {
    fprintf(stderr, "=== MDLA Cmd-Buffer Capture ===\n");

    void *h_apu = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h_apu) { fprintf(stderr, "[FAIL] libapu_mdw: %s\n", dlerror()); return 1; }
    void *h_mdla = dlopen("/vendor/lib64/libmdla_ut.so", RTLD_NOW);
    if (!h_mdla) { fprintf(stderr, "[FAIL] libmdla_ut: %s\n", dlerror()); return 1; }
    fprintf(stderr, "[OK] vendor libs loaded\n");

    g_real_cmdBufAlloc = (cmdBufAlloc_t)dlsym(h_apu, "apusysSession_cmdBufAlloc");
    g_real_cmd_run     = (cmd_run_t)    dlsym(h_apu, "apusysCmd_run");
    g_real_memAlloc    = (memAlloc_t)   dlsym(h_apu, "apusysSession_memAlloc");
    if (!g_real_cmdBufAlloc || !g_real_cmd_run || !g_real_memAlloc) {
        fprintf(stderr, "[FAIL] missing symbols\n"); return 2;
    }
    fprintf(stderr, "[OK] cmdBufAlloc @ %p, cmd_run @ %p, memAlloc @ %p\n",
            g_real_cmdBufAlloc, g_real_cmd_run, g_real_memAlloc);

    install_trampoline((void *)g_real_cmdBufAlloc, (void *)hook_cmdBufAlloc,
                       g_orig_alloc, &g_alloc_target);
    install_trampoline((void *)g_real_cmd_run, (void *)hook_cmd_run,
                       g_orig_run, &g_run_target);
    install_trampoline((void *)g_real_memAlloc, (void *)hook_memAlloc,
                       g_orig_memAlloc, &g_memAlloc_target);
    fprintf(stderr, "[OK] trampolines installed\n");

    mdla_init_fn mdla_init = dlsym(h_mdla, "_Z18mdla_platform_initv");
    if (mdla_init) mdla_init();

    execute_pattern_fn ep = dlsym(h_mdla, "execute_pattern");
    if (!ep) { fprintf(stderr, "[FAIL] no execute_pattern\n"); return 3; }

    char *cli[] = {"capture", "-f", (argc > 1 ? argv[1] : "Test"), NULL};
    fprintf(stderr, "[*] running execute_pattern -f %s\n", cli[2]);
    int rc = ep(3, cli);
    fprintf(stderr, "[*] execute_pattern rc=%d\n", rc);

    return 0;
}
