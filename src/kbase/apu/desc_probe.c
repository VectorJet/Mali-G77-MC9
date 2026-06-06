#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>

int main() {
    printf("=== APU Descriptor Internal Probe v2 ===\n");

    void *handle = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!handle) return 1;

    void* (*createSession)() = dlsym(handle, "apusysSession_createInstance");
    void* (*createCmd)(void*) = dlsym(handle, "apusysSession_createCmd");
    void* (*createSubcmd)(void*, int) = dlsym(handle, "apusysCmd_createSubcmd");
    int (*build)(void*) = dlsym(handle, "apusysCmd_build");

    void *session = createSession();
    if (session) {
        void *cmd = createCmd(session);
        void *sub = createSubcmd(cmd, 1); // MDLA

        printf("Building command...\n");
        build(cmd);

        printf("Dumping Cmd Object at %p (512 bytes):\n", cmd);
        uint8_t *cptr = (uint8_t*)cmd;
        for (int i = 0; i < 512; i++) {
            printf("%02X ", cptr[i]);
            if ((i+1) % 16 == 0) printf("\n");
        }
    }

    dlclose(handle);
    return 0;
}
