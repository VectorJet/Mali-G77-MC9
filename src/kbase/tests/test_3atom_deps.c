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
    printf("=== Test 10: Multi-atom with DEPENDENCIES (3 atoms) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *color = 0xDEADBEEF;

    /* TILER job at 0 */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (4 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x200);
    job0[12] = 0;

    /* TILER job at 0x300 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x300);
    job1[4] = (4 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x400);
    job1[12] = 0;

    /* FRAGMENT at 0x500 */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x500);
    job2[4] = (5 << 1) | (1 << 16);
    job2[8] = (uint32_t)(gva + 0x700);
    job2[12] = 0;

    /* Polygon lists */
    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;

    /* FBD */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x700);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x800);

    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x800);
    rt[0] = (uint32_t)(gva + 0x30);
    rt[2] = 4;

    /* 3 atoms: T0 -> T1 -> F */
    struct kbase_atom atoms[3] = {0};
    atoms[0].jc = gva;
    atoms[0].core_req = 0x004;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x300;
    atoms[1].core_req = 0x004;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = gva + 0x500;
    atoms[2].core_req = 0x003;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    printf("Submitting 3 atoms with dependencies...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    usleep(300000);
    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);
    if (*color != 0xDEADBEEF) printf("*** SUCCESS! ***\n");
    else printf("No change - HANG?\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}