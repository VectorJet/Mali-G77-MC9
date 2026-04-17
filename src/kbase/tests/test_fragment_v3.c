#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)

#define MALI_FBD_TAG_IS_MFBD     (1ULL << 0)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test FRAGMENT Job v3 - Match WRITE_VALUE layout ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate multiple pages */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Color buffer at offset 0x100 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;
    printf("Color buffer at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x100));

    /* 
     * SFBD structure at 0x300:
     * Match the Panfrost decode layout exactly
     */
    uint32_t *sfbd = (uint32_t *)((uint8_t*)cpu + 0x300);

    /* LOCAL_STORAGE at offset 0 - tiler context data */
    memset(sfbd, 0, 0x80);

    /* PARAMETERS at offset 0x80 - framebuffer parameters */
    sfbd[0x80/4 + 0] = 256;         /* width */
    sfbd[0x80/4 + 1] = 256;         /* height */
    sfbd[0x80/4 + 2] = 0x2;         /* format: R8G8B8A8 */
    sfbd[0x80/4 + 3] = 0;           /* swizzle (RGBA default) */
    sfbd[0x80/4 + 4] = 0;           /* clear color low */
    sfbd[0x80/4 + 5] = 0;           /* clear color high */
    sfbd[0x80/4 + 6] = 0;           /* clear depth */
    sfbd[0x80/4 + 7] = 0;           /* clear stencil */

    /* Render target at offset 0x100 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x400);
    rt[0] = (uint32_t)(gva + 0x100);  /* color base - low */
    rt[1] = 0;                         /* color base - high */
    rt[2] = 256 * 4;                   /* row stride: 256 pixels * 4 bytes = 1024 */
    rt[3] = 0;                         /* padding */

    /* Point render target in SFBD parameters */
    sfbd[0x80/4 + 8] = (uint32_t)(gva + 0x400);  /* RT pointer low */
    sfbd[0x80/4 + 9] = 0;                         /* RT pointer high */
    sfbd[0x80/4 + 10] = 0;                        /* RT stride/flags? */
    sfbd[0x80/4 + 11] = 0;

    printf("SFBD at 0x%llx, RT at 0x%llx\n", 
           (unsigned long long)(gva+0x300), (unsigned long long)(gva+0x400));

    /*
     * FRAGMENT job at offset 0:
     * Layout matches WRITE_VALUE:
     * - word 4: type=5 (FRAGMENT), index=1
     * - word 8-9: FBD pointer (low, high) 
     * - word 10: payload word
     * - word 11: padding
     * - word 12: more payload
     */
    uint32_t *job = (uint32_t *)cpu;
    
    /* Control: type=5 (FRAGMENT), index=1 */
    job[4] = (5 << 1) | (1 << 16);  /* FRAGMENT type=5, index=1 */
    
    /* FBD pointer at word 8-9 (like target address for WRITE_VALUE) */
    job[8] = (uint32_t)(gva + 0x300);  /* FBD pointer low - NO TAG for SFBD */
    job[9] = 0;                          /* FBD pointer high */
    
    /* Payload: tile bounds (all 0 = full frame) */
    job[10] = 0;  /* bound_min_x | bound_min_y */
    job[11] = 0;  /* bound_max_x | bound_max_y */
    job[12] = 0;  /* state pointer (or zero for none) */

    printf("Job: type=5, FBD=0x%llx\n", (unsigned long long)(gva+0x300));
    printf("Job words[4]=0x%08x [8]=0x%08x [10]=0x%08x [12]=0x%08x\n",
           job[4], job[8], job[10], job[12]);

    /* Submit as FRAGMENT only */
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x001;  /* FS only */
    atom.atom_number = 1;
    atom.prio = 0;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(500000);

    printf("\nResult:\n");
    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    if (*color != 0xDEADBEEF) printf("*** GPU MODIFIED COLOR! ***\n");
    else printf("Color unchanged - fragment job didn't execute\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}