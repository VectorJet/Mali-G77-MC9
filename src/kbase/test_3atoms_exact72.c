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

/* Exact 72-byte atom - packed */
/* Map of known 72-byte kbase atom:
 * 0-7: seq_nr (8)
 * 8-15: jc (8)
 * 16-23: udata[0] (8)
 * 24-31: udata[1] (8)
 * 32-39: extres_list (8)
 * 40-41: nr_extres (2)
 * 42-43: jit_id[0] (2)
 * 44-45: jit_id[1] (2)
 * 46: pre_dep_atom[0]
 * 47: pre_dep_atom[1]
 * 48: pre_dep_type[0]
 * 49: pre_dep_type[1]
 * 50: atom_number
 * 51: prio
 * 52: device_nr
 * 53: jobslot
 * 54-57: core_req (4)
 * 58: renderpass_id
 * 59-65: padding (7)
 * Total: 66 bytes - need 6 more
 */
struct atom_72 {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata0;
    uint64_t udata1;
    uint64_t extres_list;
    uint16_t nr_extres;
    uint16_t jit_id0;
    uint16_t jit_id1;
    uint8_t pre_dep_atom0;
    uint8_t pre_dep_atom1;
    uint8_t pre_dep_type0;
    uint8_t pre_dep_type1;
    uint8_t atom_number;
    uint8_t prio;
    uint8_t device_nr;
    uint8_t jobslot;
    uint32_t core_req;
    uint8_t renderpass_id;
    uint8_t pad1;
    uint8_t pad2;
    uint8_t pad3;
    uint8_t pad4;
    uint8_t pad5;
    uint8_t pad6;
} __attribute__((packed));

_Static_assert(sizeof(struct atom_72) == 72, "atom must be 72 bytes");

int main(void) {
    printf("=== 3 atoms with EXACT 72-byte struct ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x600) = 1;

    /* Use exact 72-byte struct */
    struct atom_72 atoms[3] = {0};

    atoms[0].jc = gva;
    atoms[0].core_req = 0x004;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x004;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom0 = 0;
    atoms[1].pre_dep_type0 = 1;

    atoms[2].jc = gva + 0x400;
    atoms[2].core_req = 0x004;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom0 = 1;
    atoms[2].pre_dep_type0 = 1;

    printf("Submitting 3 atoms (72-byte struct)...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    sleep(2);
    printf("poly0=0x%08x poly1=0x%08x poly2=0x%08x\n",
           *(uint32_t*)((uint8_t*)cpu+0x200),
           *(uint32_t*)((uint8_t*)cpu+0x400),
           *(uint32_t*)((uint8_t*)cpu+0x600));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}