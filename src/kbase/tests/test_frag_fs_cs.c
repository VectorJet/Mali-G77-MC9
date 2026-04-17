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
    printf("=== FRAGMENT: Try core_req 0x003 (FS+CS) ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 4*4096);

    volatile uint32_t *target = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *target = 0xDEADBEEF;

    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);  /* FRAGMENT */
    job[8] = (uint32_t)(gva + 0x100);  /* FBD */
    job[12] = 0;

    /* Minimal FBD */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x100);
    fbd[0x80/4] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x200);

    /* RT points to target! */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x200);
    rt[0] = (uint32_t)(gva + 0x30);  /* directly to target */
    rt[2] = 4;

    printf("RT points to target at 0x%llx\n", (unsigned long long)(gva+0x30));

    /* Try FS+CS combo */
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x003;  /* FS + CS */
    atom.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(200000);

    printf("Result: Target=0x%08x (was 0xDEADBEEF)\n", *target);
    if (*target != 0xDEADBEEF) printf("*** SUCCESS! ***\n");
    else printf("No change\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}