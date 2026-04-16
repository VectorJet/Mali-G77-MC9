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
#define KBASE_IOCTL_GET_GPUPROPS   _IOC(_IOC_WRITE, 0x80, 3, 16)
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
    printf("=== Debug: Warm up like Chrome, then 3 atoms ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Do a "warm up" sequence like Chrome */
    printf("Warm up...\n");

    /* Allocate and do a simple job first */
    uint64_t mem0[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem0);
    void *buf0 = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem0[1]);
    memset(buf0, 0, 2*4096);
    *(uint32_t*)((uint8_t*)buf0 + 0x100) = 1;

    struct kbase_atom warmup[1] = {0};
    warmup[0].jc = mem0[1];
    warmup[0].core_req = 0x004;
    warmup[0].atom_number = 1;
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&warmup, .nr = 1, .stride = 72});
    usleep(100000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("Warmup done\n");

    /* Now do ioctl 0x19 like Chrome does */
    printf("Calling ioctl 0x19...\n");
    uint32_t cfg = 0x180;
    ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x19, 4), &cfg);

    /* Try another 0x19 */
    cfg = 0x173;
    ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x19, 4), &cfg);

    /* Now try 3 atoms */
    printf("\n=== Now trying 3 atoms ===\n");
    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x600) = 1;

    struct kbase_atom atoms[3] = {0};
    atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1;
    atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 1;
    atoms[2].jc = gva + 0x400; atoms[2].core_req = 0x004; atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1; atoms[2].pre_dep_type[0] = 1;

    printf("Submitting 3 atoms...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    sleep(2);
    printf("poly0=0x%08x poly1=0x%08x poly2=0x%08x\n",
           *(uint32_t*)((uint8_t*)cpu+0x200),
           *(uint32_t*)((uint8_t*)cpu+0x400),
           *(uint32_t*)((uint8_t*)cpu+0x600));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    munmap(buf0, 2*4096);
    close(fd);
    return 0;
}