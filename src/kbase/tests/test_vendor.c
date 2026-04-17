#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

struct submit_args {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

int main(void) {
    /* Try dlopen libgpud.so */
    printf("=== Try dlopen libgpud ===\n");
    
    void *handle = dlopen("libgpud.so", RTLD_NOW);
    if (!handle) {
        printf("dlopen failed: %s\n", dlerror());
        
        /* Try with full path */
        handle = dlopen("/vendor/lib64/libgpud.so", RTLD_NOW);
        if (!handle) {
            printf("dlopen /vendor/lib64/libgpud.so failed: %s\n", dlerror());
            
            /* Try /system/lib64/ */
            handle = dlopen("/system/lib64/libgpud.so", RTLD_NOW);
            if (!handle) {
                printf("dlopen /system/lib64/libgpud.so failed: %s\n", dlerror());
            } else {
                printf("SUCCESS: loaded /system/lib64/libgpud.so\n");
                dlclose(handle);
            }
        } else {
            printf("SUCCESS: loaded /vendor/lib64/libgpud.so\n");
            dlclose(handle);
        }
    } else {
        printf("SUCCESS: loaded libgpud.so\n");
        dlclose(handle);
    }
    
    /* Also check what libraries are available */
    printf("\n=== Check for Mali libraries ===\n");
    system("ls -la /vendor/lib64/libmali* 2>/dev/null || echo no libmali in /vendor/lib64");
    system("ls -la /system/lib64/libmali* 2>/dev/null || echo no libmali in /system/lib64");
    system("ls -la /vendor/lib/libmali* 2>/dev/null || echo no libmali in /vendor/lib");
    system("ls -la /system/lib/libmali* 2>/dev/null || echo no libmali in /system/lib");
    
    /* Try libmali */
    printf("\n=== Try libmali ===\n");
    handle = dlopen("libmali.so", RTLD_NOW);
    if (!handle) {
        printf("libmali.so: %s\n", dlerror());
    } else {
        printf("libmali loaded!\n");
        dlclose(handle);
    }
    
    handle = dlopen("/vendor/lib64/libmali.so", RTLD_NOW);
    if (!handle) {
        printf("/vendor/lib64/libmali.so: %s\n", dlerror());
    } else {
        printf("/vendor/lib64/libmali loaded!\n");
        dlclose(handle);
    }
    
    /* If we can't use vendor lib, try direct approach one more time */
    printf("\n=== Last try: exact Chrome format ===\n");
    
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Chrome uses: core_req=0x203 (CS+CF), job_type in atom is what? */
    /* Try exactly matching captured Chrome: core_req=0x203 */
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        /* WRITE_VALUE but with different format */
        job[0] = 0;  /* exception status */
        job[1] = 0;  /* first incomplete */
        job[2] = 0;  /* fault pointer */
        job[3] = 0;
        job[4] = 0x00010004;  /* type=2, index=1 (exact from docs) */
        job[5] = 0;  /* deps */
        job[6] = 0;  /* next low */
        job[7] = 0;  /* next high */
        /* payload */
        job[8] = (uint32_t)(mem[1] + 0x100);  /* target */
        job[9] = 0;
        job[10] = 6;  /* Immediate32 */
        job[11] = 0;
        job[12] = 0x12345678;
        job[13] = 0;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xABCDEF00;
        
        /* Try with core_req=0x203 like Chrome atom */
        struct kbase_atom atom = {0};
        atom.jc = mem[1];  /* job at start */
        atom.core_req = 0x203;  /* Chrome's core_req */
        atom.atom_number = 1;
        
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        
        uint8_t ev[24];
        read(fd, ev, sizeof(ev));
        
        printf("marker=0x%08x (expected 0x12345678)\n", *marker);
        
        /* Also check job header */
        printf("job[0]=0x%08x (exception)\n", job[0]);
        
        munmap(cpu, 8192);
    }
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}