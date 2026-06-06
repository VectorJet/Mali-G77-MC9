#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

int main() {
    printf("=== APU Memory Mapper ===\n");

    void *handle = dlopen("/vendor/lib64/libapu_mdw.so", RTLD_NOW);
    if (!handle) return 1;

    void* (*createSession)() = dlsym(handle, "apusysSession_createInstance");
    void* (*createCmd)(void*) = dlsym(handle, "apusysSession_createCmd");

    void *session = createSession();
    if (session) {
        printf("[OK] Session created at %p\n", session);
        
        void *cmd = createCmd(session);
        printf("[OK] Command created at %p\n", cmd);

        printf("\n--- Memory Maps ---\n");
        system("cat /proc/self/maps | grep -iE \"apu|vpu|mdla|anon\"");
    }

    dlclose(handle);
    return 0;
}
