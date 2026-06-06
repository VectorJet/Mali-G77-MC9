// Wraps libmdla_ut.so::execute_pattern to run the built-in "Test" MDLA pattern.
// libmdla_ut bundles real MDLA cmd/golden data and depends on libapu_mdw.
// This proves end-to-end MDLA hardware execution from our toolchain.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

typedef int (*execute_pattern_fn)(int, char **);
typedef int (*mdla_init_fn)(void);

int main(int argc, char **argv) {
    printf("=== MDLA Built-In Pattern Runner ===\n");
    printf("[*] PID = %d\n", getpid());

    void *h = dlopen("/vendor/lib64/libmdla_ut.so", RTLD_NOW);
    if (!h) { printf("[FAIL] dlopen libmdla_ut.so: %s\n", dlerror()); return 1; }
    printf("[OK] libmdla_ut.so loaded\n");

    mdla_init_fn mdla_init = dlsym(h, "_Z18mdla_platform_initv");
    if (mdla_init) {
        int rc = mdla_init();
        printf("[OK] mdla_platform_init rc=%d\n", rc);
    }

    execute_pattern_fn execute_pattern = dlsym(h, "execute_pattern");
    if (!execute_pattern) { printf("[FAIL] no execute_pattern symbol\n"); return 2; }
    printf("[OK] execute_pattern @ %p\n", execute_pattern);

    /* Build argv: -f Test (run built-in Test pattern) */
    char *cli[16] = {0};
    int n = 0;
    cli[n++] = "mdla_run_builtin";
    /* Allow extra options to be passed through */
    cli[n++] = "-f";
    cli[n++] = (argc > 1) ? argv[1] : "Test";
    for (int i = 2; i < argc && n < 15; i++) cli[n++] = argv[i];
    printf("[*] argv =");
    for (int i = 0; i < n; i++) printf(" %s", cli[i]);
    printf("\n");

    int rc = execute_pattern(n, cli);
    printf("[OK] execute_pattern returned %d\n", rc);

    dlclose(h);
    return rc;
}
