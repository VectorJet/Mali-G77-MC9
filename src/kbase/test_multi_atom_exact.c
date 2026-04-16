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
    printf("=== Test 8: Exact Working Pattern (Multi-Atom) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* 8 pages like working test */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Color buffer at 0x30 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *color = 0xDEADBEEF;
    printf("Color at 0x%llx\n", (unsigned long long)(gva+0x30));

    /* TILER job at offset 0 */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (4 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x200);
    job0[12] = 0;

    /* FRAGMENT job at offset 0x300 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x300);
    job1[4] = (5 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x500);
    job1[12] = 0;

    /* Polygon list at 0x200 */
    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;

    /* FBD at 0x500 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x500);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x600);

    /* RT at 0x600 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x600);
    rt[0] = (uint32_t)(gva + 0x30);
    rt[2] = 4;

    /* 2 atoms in SINGLE submission - like working test */
    struct kbase_atom atoms[2] = {0};
    atoms[0].jc = gva;
    atoms[0].core_req = 0x004;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x300;
    atoms[1].core_req = 0x003;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});

    printf("Submitted 2 atoms in single call...\n");

    usleep(300000);

    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    if (*color != 0xDEADBEEF) printf("*** SUCCESS! ***\n");
    else printf("No change\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}