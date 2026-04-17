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

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test FRAGMENT Job v2 - Proper FBD ===\n\n");

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

    /* SFBD (Single Target Framebuffer Descriptor) structure at 0x300:
     * Based on Panfrost decode, SFBD layout is:
     * - 0x00: LOCAL_STORAGE (tiler context?)
     * - 0x80: PARAMETERS (includes width, height, format, swizzle)
     * - 0x100+: RENDER_TARGET (color buffer descriptor)
     */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x300);

    /* PARAMETERS at offset 0x80 */
    fbd[0x80/4 + 0] = 256;   /* width */
    fbd[0x80/4 + 1] = 256;   /* height */
    fbd[0x80/4 + 2] = 0x2;   /* format? RGBA8 */
    fbd[0x80/4 + 3] = 0;     /* swizzle */
    fbd[0x80/4 + 4] = 0;     /* clear color low */
    fbd[0x80/4 + 5] = 0;     /* clear color high */
    fbd[0x80/4 + 6] = 0;     /* clear depth */
    fbd[0x80/4 + 7] = 0;     /* clear stencil */

    /* RENDER_TARGET at offset 0x100 - color buffer address */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x400);
    rt[0] = (uint32_t)(gva + 0x100);  /* color base address */
    rt[1] = 0;                         /* row stride (0 = linear?) */
    rt[2] = 0;                        /* padding */
    rt[3] = 0;

    /* Link render target in parameters */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x400);  /* rt pointer */
    fbd[0x80/4 + 9] = 0;

    /* FRAGMENT job at offset 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);  /* FRAGMENT type=5, index=1 */

    /* FBD pointer at offset 0x38 (0x30 + 8?) - need to find right spot */
    /* Based on decode: fbd field is at some offset within job descriptor */
    *(uint64_t *)((uint8_t*)cpu + 0x38) = gva + 0x300;

    /* Job header with fragment-specific fields */
    job[8] = (uint32_t)(gva + 0x300);  /* FBD pointer low */
    job[9] = 0;                         /* FBD pointer high */
    job[10] = 0;                        /* unused */
    job[11] = 0;                        /* unused */

    printf("Job: type=5 (FRAGMENT)\n");
    printf("FBD at 0x%llx\n", (unsigned long long)(gva+0x300));
    printf("Color at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x100));

    /* Try different core_req values */
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x001;  /* FS only */
    atom.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(500000);

    printf("\nResult:\n");
    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    if (*color != 0xDEADBEEF) printf("GPU wrote!\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}