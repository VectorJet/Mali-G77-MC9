#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int EGLBoolean;

typedef EGLDisplay (*PFN_eglGetDisplay)(int);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, int*, int*);
typedef EGLBoolean (*PFN_eglTerminate)(EGLDisplay);

int main() {
    printf("=== Direct EGL Loader Test ===\n");
    
    void *egl = dlopen("/vendor/lib/egl/libGLES_mali.so", RTLD_NOW);
    if (!egl) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }
    printf("Loaded libGLES_mali.so: %p\n", egl);
    
    PFN_eglGetDisplay getDisplay = dlsym(egl, "eglGetDisplay");
    if (getDisplay) {
        printf("eglGetDisplay: %p\n", getDisplay);
    } else {
        printf("No eglGetDisplay\n");
    }
    
    dlclose(egl);
    return 0;
}