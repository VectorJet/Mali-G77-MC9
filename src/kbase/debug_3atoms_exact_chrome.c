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
    printf("=== Debug: 3 atoms EXACT Chrome format ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* ioctl 0x19 like Chrome */
    uint32_t cfg = 0x180;
    ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x19, 4), &cfg);

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    /* Jobs */
    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x600) = 1;

    /* EXACT Chrome atom format from capture:
     * Atom 0: core_req=0x209, seq_nr=0
     * Atom 1: core_req=0x04e, seq_nr=0x11
     * Atom 2: core_req=0x001, seq_nr=0x11
     */
    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = gva;
    atoms[0].core_req = 0x209;  /* Chrome's atom 0 */
    atoms[0].atom_number = 1;
    atoms[0].seq_nr = 0;
    atoms[0].udata[0] = gva + 0x100;
    atoms[0].udata[1] = 0;

    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x04e;  /* Chrome's atom 1 */
    atoms[1].atom_number = 2;
    atoms[1].seq_nr = 0x11;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;
    atoms[1].udata[0] = gva + 0x120;
    atoms[1].udata[1] = 0;

    atoms[2].jc = gva + 0x400;
    atoms[2].core_req = 0x001;  /* Chrome's atom 2 */
    atoms[2].atom_number = 3;
    atoms[2].seq_nr = 0x11;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;
    atoms[2].udata[0] = gva + 0x140;
    atoms[2].udata[1] = 0;

    printf("Submitting 3 atoms Chrome-style...\n");
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});
    printf("ret=%d\n", ret);

    sleep(2);
    printf("After sleep\n");
    printf("poly0=0x%08x poly1=0x%08x poly2=0x%08x\n",
           *(uint32_t*)((uint8_t*)cpu+0x200),
           *(uint32_t*)((uint8_t*)cpu+0x400),
           *(uint32_t*)((uint8_t*)cpu+0x600));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}