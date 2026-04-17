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

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

void test_frag(int fd, uint32_t core_req, const char *name) {
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Color at 0x100 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;

    /* FRAGMENT job at 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);
    job[8] = (uint32_t)(gva + 0x300);
    job[12] = 0;

    /* FBD at 0x300 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 2] = 0x2;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x380);

    /* RT at 0x380 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x380);
    rt[0] = (uint32_t)(gva + 0x100);
    rt[2] = 1024;

    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = core_req;
    atom.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(200000);

    printf("%s: Color=0x%08x\n", name, *color);

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
}

void test_frag_renderer_state(int fd, uint32_t core_req, const char *name) {
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    /* Color at 0x100 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;

    /* FRAGMENT job at 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);
    job[8] = (uint32_t)(gva + 0x300);  /* FBD */
    /* RENDERER_STATE at word 12 */
    job[12] = (uint32_t)(gva + 0x500);  /* render state at 0x500 */

    /* FBD at 0x300 - minimal */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x380);

    /* RT at 0x380 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x380);
    rt[0] = (uint32_t)(gva + 0x100);
    rt[2] = 1024;

    /* RENDERER_STATE at 0x500 - minimal shader pointer */
    uint32_t *rs = (uint32_t *)((uint8_t*)cpu + 0x500);
    rs[0] = 0;  /* shader pointer low (0 = no shader) */
    rs[1] = 0;  /* shader pointer high */
    rs[2] = 0;  /* attribute count */
    rs[3] = 0;  /* varying count */

    printf("%s (rs): Color=0x%08x\n", name, *color);

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
}

void test_frag_mfbd(int fd, uint32_t core_req, const char *name) {
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;

    /* FRAGMENT job */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);
    /* FBD with MFBD tag (bit 0 = 1) */
    job[8] = (uint32_t)(gva + 0x300) | 1;  /* MFBD tag */
    job[12] = 0;

    /* MFBD header at 0x300 */
    uint32_t *mfbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    mfbd[0] = 256;  /* width */
    mfbd[1] = 256;  /* height */
    mfbd[2] = 1;    /* render target count */
    mfbd[0x80/4 + 0] = (uint32_t)(gva + 0x400);  /* RT0 */

    /* RT at 0x400 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x400);
    rt[0] = (uint32_t)(gva + 0x100);
    rt[2] = 1024;

    printf("%s (MFBD): Color=0x%08x\n", name, *color);

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
}

void test_frag_with_varying(int fd, uint32_t core_req, const char *name) {
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;

    /* FRAGMENT with varying buffer pointer at word 11 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);
    job[8] = (uint32_t)(gva + 0x300);
    job[11] = (uint32_t)(gva + 0x500);  /* varying buffer */
    job[12] = 0;

    /* FBD */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x380);

    /* RT */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x380);
    rt[0] = (uint32_t)(gva + 0x100);
    rt[2] = 1024;

    /* Varying buffer at 0x500 */
    volatile uint32_t *vary = (volatile uint32_t *)((uint8_t*)cpu + 0x500);
    *vary = 0x55555555;

    printf("%s (vary): Color=0x%08x\n", name, *color);

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
}

int main(void) {
    printf("=== Comprehensive FRAGMENT Tests ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    printf("--- Basic tests ---\n");
    test_frag(fd, 0x001, "FS");
    test_frag(fd, 0x201, "FS+FC");
    test_frag(fd, 0x101, "FS+TF");

    printf("\n--- With RENDERER_STATE ---\n");
    test_frag_renderer_state(fd, 0x001, "FS");
    test_frag_renderer_state(fd, 0x201, "FS+FC");
    test_frag_renderer_state(fd, 0x101, "FS+TF");

    printf("\n--- With MFBD ---\n");
    test_frag_mfbd(fd, 0x001, "FS");
    test_frag_mfbd(fd, 0x201, "FS+FC");

    printf("\n--- With Varying buffer ---\n");
    test_frag_with_varying(fd, 0x001, "FS");
    test_frag_with_varying(fd, 0x201, "FS+FC");

    printf("\n--- Done ---\n");

    close(fd);
    return 0;
}