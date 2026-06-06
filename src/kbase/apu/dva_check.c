#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

int main() {
    printf("=== APU C-API Metadata Discovery ===\n");

    void *handle = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!handle) return 1;

    void* (*createSession)() = dlsym(handle, "apusysSession_createInstance");
    void* (*memAlloc)(void*, size_t) = dlsym(handle, "apusysSession_memAlloc");
    int (*getInfo)(void*, void*, void*) = dlsym(handle, "apusysSession_memGetInfoFromHostPtr");

    void *session = createSession();
    if (session && memAlloc && getInfo) {
        void *mem = memAlloc(session, 4096);
        printf("[OK] Memory allocated at %p\n", mem);

        uint32_t info[12] = {0};
        if (getInfo(session, mem, info) == 0) {
            printf("[SUCCESS] Metadata retrieved!\n");
            for (int i = 0; i < 12; i++) {
                printf("  Offset 0x%02X: 0x%08X\n", i * 4, info[i]);
            }
            
            // Check if any field looks like a 64-bit address (usually starts with 0x0000...)
            for (int i = 0; i < 11; i++) {
                uint64_t val = *(uint64_t*)&info[i];
                if (val > 0x1000000) {
                    printf("[INTEREST] Potential address at offset 0x%02X: 0x%016llX\n", i * 4, (unsigned long long)val);
                }
            }
        }
    }

    dlclose(handle);
    return 0;
}
