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
    printf("=== Test: Sequential with Dependencies (pre_dep) ===\n\n");

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

    /* Job 1: WRITE_VALUE at 0 */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x200);
    job0[12] = 0x11111111;

    struct kbase_atom atom0 = {0};
    atom0.jc = gva;
    atom0.core_req = 0x203;
    atom0.atom_number = 1;
    atom0.seq_nr = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom0, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("Job 1 done\n");

    /* Job 2: TILER with dependency on job 1 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x300);
    job1[4] = (4 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x400);
    job1[12] = 0;

    struct kbase_atom atom1 = {0};
    atom1.jc = gva + 0x300;
    atom1.core_req = 0x004;
    atom1.atom_number = 1;
    atom1.seq_nr = 2;
    atom1.pre_dep_atom[0] = 1;  /* depends on atom 1 (seq_nr) */
    atom1.pre_dep_type[0] = 1;  /* TYPE 1 = job complete */

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom1, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("Job 2 (with dep) done\n");

    printf("Target: 0x%08x (was 0xDEADBEEF)\n", *target);
    if (*target != 0xDEADBEEF) printf("*** SUCCESS! ***\n");
    else printf("No change\n");

    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}