#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

int main() {
    printf("=== APU Memory Address Correlation v2 ===\n");

    void *handle = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!handle) {
        printf("dlopen fail: %s\n", dlerror());
        return 1;
    }

    void* (*createSession)() = dlsym(handle, "apusysSession_createInstance");
    void* (*memAlloc)(void*, size_t) = dlsym(handle, "apusysSession_memAlloc");
    void* (*createCmd)(void*) = dlsym(handle, "apusysSession_createCmd");
    void* (*createSubcmd)(void*, int) = dlsym(handle, "apusysCmd_createSubcmd");
    int (*addCmdBuf)(void*, void*, int) = dlsym(handle, "apusysSubCmd_addCmdBuf");
    int (*build)(void*) = dlsym(handle, "apusysCmd_build");

    void *session = createSession();
    if (session) {
        void *mem = memAlloc(session, 4096);
        printf("[OK] memAlloc returned userspace pointer: %p\n", mem);

        void *cmd = createCmd(session);
        void *sub = createSubcmd(cmd, 1);
        addCmdBuf(sub, mem, 0);

        printf("Building...\n");
        build(cmd);

        // Dump the job packet from the cmd object (offset 0x70)
        uint8_t *job = (uint8_t*)cmd + 0x70;
        uint64_t p1, p2, p3;
        memcpy(&p1, job + 24, 8);
        memcpy(&p2, job + 100, 8);
        memcpy(&p3, job + 108, 8);

        printf("Job Pointer 1: 0x%016llX\n", (unsigned long long)p1);
        printf("Job Pointer 2: 0x%016llX\n", (unsigned long long)p2);
        printf("Job Pointer 3: 0x%016llX\n", (unsigned long long)p3);

        if (mem && p1 >= (uintptr_t)mem && p1 < (uintptr_t)mem + 4096) {
            printf("[MATCH] Pointer 1 is INSIDE the DMA-BUF at offset %llu\n", (unsigned long long)(p1 - (uintptr_t)mem));
        } else {
            printf("[OFF] Pointer 1 is OUTSIDE the DMA-BUF\n");
        }
    }

    dlclose(handle);
    return 0;
}
