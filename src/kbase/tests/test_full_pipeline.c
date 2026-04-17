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

int main(void) {
    printf("=== Full Pipeline: WRITE_VALUE + VERTEX + TILER + FRAGMENT ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate single buffer for all jobs */
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    /* Color buffer at 0x100 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;
    printf("Color at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x100));

    /* WRITE_VALUE job at offset 0 */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE */
    job0[8] = (uint32_t)(gva + 0x30);  /* write to marker */
    job0[12] = 0xAAAAAAAA;  /* value */

    /* Marker at 0x30 */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *marker = 0x11111111;
    printf("Marker at 0x%llx = 0x11111111\n", (unsigned long long)(gva+0x30));

    /* VERTEX job at offset 0x200 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job1[4] = (3 << 1) | (1 << 16);  /* VERTEX */
    job1[8] = (uint32_t)(gva + 0x300);  /* attribute buffer */
    job1[12] = 0;

    /* TILER job at offset 0x400 */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x400);
    job2[4] = (4 << 1) | (1 << 16);  /* TILER */
    job2[8] = (uint32_t)(gva + 0x500);  /* polygon list */
    job2[12] = 0;

    /* Polygon list at 0x500 */
    volatile uint32_t *poly = (volatile uint32_t *)((uint8_t*)cpu + 0x500);
    *poly = 0x22222222;

    /* FRAGMENT job at offset 0x600 */
    uint32_t *job3 = (uint32_t *)((uint8_t*)cpu + 0x600);
    job3[4] = (5 << 1) | (1 << 16);  /* FRAGMENT */
    job3[8] = (uint32_t)(gva + 0x700);  /* FBD */
    job3[12] = 0;

    /* FBD at 0x700 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x700);
    fbd[0x80/4] = 256;  /* width */
    fbd[0x80/4 + 1] = 256;  /* height */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x780);  /* RT */

    /* RT at 0x780 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x780);
    rt[0] = (uint32_t)(gva + 0x100);  /* color */
    rt[2] = 1024;

    printf("\nJobs: WV=0x%llx V=0x%llx T=0x%llx F=0x%llx\n",
           (unsigned long long)gva, (unsigned long long)(gva+0x200),
           (unsigned long long)(gva+0x400), (unsigned long long)(gva+0x600));

    /* 4 atoms */
    struct kbase_atom atoms[4] = {0};

    atoms[0].jc = gva;
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x008;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = gva + 0x400;
    atoms[2].core_req = 0x004;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    atoms[3].jc = gva + 0x600;
    atoms[3].core_req = 0x001;
    atoms[3].atom_number = 4;
    atoms[3].pre_dep_atom[0] = 2;
    atoms[3].pre_dep_type[0] = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 4, .stride = 72});

    usleep(300000);

    printf("\nResults:\n");
    printf("  Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    printf("  Marker: 0x%08x (was 0x11111111)\n", *marker);
    printf("  Polygon: 0x%08x (was 0x22222222)\n", *poly);

    if (*color != 0xDEADBEEF) printf("\n*** SUCCESS! GPU RENDERED! ***\n");
    else printf("\nColor unchanged\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
    close(fd);
    return 0;
}