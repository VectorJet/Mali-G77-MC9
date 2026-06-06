#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

int main() {
    printf("=== APU Device Virtual Address Dump ===\n");

    void *handle = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!handle) return 1;

    void* (*createSession)() = dlsym(handle, "apusysSession_createInstance");
    void* (*memAlloc)(void*, size_t) = dlsym(handle, "apusysSession_memAlloc");
    void* (*createExecutor)(void*) = dlsym(handle, "apusysSession_createExecutor");
    uint64_t (*getDva)(void*, void*) = dlsym(handle, "_ZN10ApuMdw_2_014apusysExecutor14memMapDeviceVaEPNS_9apusysMemE");

    void *session = createSession();
    if (session) {
        printf("[OK] Session created\n");
        void *mem = memAlloc(session, 4096);
        void *exec = createExecutor(session);
        
        if (getDva && exec && mem) {
            // Note: mem is IApusysMem*, which the mangled symbol expects
            uint64_t dva = getDva(exec, mem);
            printf("[SUCCESS] memMapDeviceVa returned DVA: 0x%016llX\n", (unsigned long long)dva);
        } else {
            printf("[FAIL] Symbols or objects missing: getDva=%p, exec=%p, mem=%p\n", getDva, exec, mem);
        }
    }

    dlclose(handle);
    return 0;
}
