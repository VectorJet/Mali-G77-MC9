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
    printf("=== Test Full Pipeline: WRITE_VALUE + TILER + FRAGMENT ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate buffer */
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    /* Color buffer at 0x100 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;
    printf("Color at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x100));

    /* Marker at 0x30 to see if TILER writes here */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *marker = 0x11111111;
    printf("Marker at 0x%llx = 0x11111111\n", (unsigned long long)(gva+0x30));

    /* Buffer for polygon list at 0x200 */
    volatile uint32_t *poly = (volatile uint32_t *)((uint8_t*)cpu + 0x200);
    *poly = 0xAAAAAAAA;
    printf("Polygon list at 0x%llx = 0xAAAAAAAA (before)\n", (unsigned long long)(gva+0x200));

    /*
     * Job at offset 0:
     * - First part: WRITE_VALUE
     * - Second part: TILER
     * - Third part: FRAGMENT (would need to be in separate buffer normally)
     * 
     * Since we can only have one job per buffer at offset 0, let's try
     * just TILER and see what happens, then add FRAGMENT
     */
    
    /* TILER job at offset 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (4 << 1) | (1 << 16);  /* TILER type=4, index=1 */
    job[8] = (uint32_t)(gva + 0x200);  /* polygon list pointer */
    job[9] = 0;
    job[10] = 0;  /* bound min */
    job[11] = 0;  /* bound max */
    job[12] = 0;

    printf("\nTILER job: type=4, poly_list=0x%llx\n", (unsigned long long)(gva+0x200));

    /* Also setup FRAGMENT job at offset 0x400 (in same buffer) */
    uint32_t *frag_job = (uint32_t *)((uint8_t*)cpu + 0x400);
    frag_job[4] = (5 << 1) | (1 << 16);  /* FRAGMENT type=5 */
    frag_job[8] = (uint32_t)(gva + 0x300);  /* FBD at 0x300 */
    frag_job[9] = 0;
    frag_job[10] = 0;
    frag_job[12] = 0;

    /* SFBD at 0x300 */
    uint32_t *sfbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    sfbd[0x80/4 + 0] = 256;   /* width */
    sfbd[0x80/4 + 1] = 256;   /* height */
    sfbd[0x80/4 + 2] = 0x2;   /* format */
    sfbd[0x80/4 + 8] = (uint32_t)(gva + 0x380);  /* RT pointer */

    /* RT at 0x380 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x380);
    rt[0] = (uint32_t)(gva + 0x100);  /* color address */
    rt[2] = 256 * 4;          /* stride */

    printf("FRAGMENT job at 0x%llx, FBD=0x%llx\n", 
           (unsigned long long)(gva+0x400), (unsigned long long)(gva+0x300));

    /* Submit TILER only first */
    struct kbase_atom atom = {0};
    atom.jc = gva;  /* TILER at offset 0 */
    atom.core_req = 0x004;  /* Tiler only */
    atom.atom_number = 1;

    printf("\nSubmitting TILER (atom_number=1)...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(500000);

    printf("\nAfter TILER:\n");
    printf("  Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    printf("  Marker: 0x%08x (was 0x11111111)\n", *marker);
    printf("  Polygon list: 0x%08x (was 0xAAAAAAAA)\n", *poly);

    /* Now try with TILER + FRAGMENT using pre_dep */
    memset(cpu, 0, 16*4096);
    *color = 0xDEADBEEF;
    *marker = 0x22222222;
    *poly = 0xBBBBBBBB;

    /* Rebuild jobs */
    job[4] = (4 << 1) | (1 << 16);
    job[8] = (uint32_t)(gva + 0x200);
    job[9] = 0;
    job[10] = 0;
    job[11] = 0;
    job[12] = 0;

    frag_job[4] = (5 << 1) | (1 << 16);
    frag_job[8] = (uint32_t)(gva + 0x300);
    frag_job[9] = 0;
    frag_job[10] = 0;
    frag_job[12] = 0;

    /* Two atoms */
    struct kbase_atom atoms[2] = {0};

    atoms[0].jc = gva;           /* TILER at 0 */
    atoms[0].core_req = 0x004;   /* Tiler */
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x400;   /* FRAGMENT at 0x400 */
    atoms[1].core_req = 0x001;   /* Fragment */
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;   /* depends on atom 0 */
    atoms[1].pre_dep_type[0] = 1;   /* job complete */

    printf("\nSubmitting TILER + FRAGMENT...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});

    usleep(500000);

    printf("\nAfter TILER + FRAGMENT:\n");
    printf("  Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    printf("  Marker: 0x%08x (was 0x22222222)\n", *marker);
    printf("  Polygon list: 0x%08x (was 0xBBBBBBBB)\n", *poly);

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
    close(fd);
    return 0;
}