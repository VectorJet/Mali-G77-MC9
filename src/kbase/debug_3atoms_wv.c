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
    printf("=== Debug: 3 atoms as WV (simplest job) ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    /* 3 simple WRITE_VALUE jobs - type 2, core_req 0x203 */
    /* WV at 0 */
    *(uint32_t*)((uint8_t*)cpu + 0x100) = 0x11111111;
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x100);
    job0[12] = 0x11111111;

    /* WV at 0x200 */
    *(uint32_t*)((uint8_t*)cpu + 0x300) = 0x22222222;
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job1[4] = (2 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x300);
    job1[12] = 0x22222222;

    /* WV at 0x400 */
    *(uint32_t*)((uint8_t*)cpu + 0x500) = 0x33333333;
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x400);
    job2[4] = (2 << 1) | (1 << 16);
    job2[8] = (uint32_t)(gva + 0x500);
    job2[12] = 0x33333333;

    /* 3 WV atoms */
    struct kbase_atom atoms[3] = {0};
    atoms[0].jc = gva;
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x203;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = gva + 0x400;
    atoms[2].core_req = 0x203;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    printf("Submitting 3 WV atoms...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    sleep(2);
    printf("target1=0x%08x target2=0x%08x target3=0x%08x\n",
           *(uint32_t*)((uint8_t*)cpu+0x100),
           *(uint32_t*)((uint8_t*)cpu+0x300),
           *(uint32_t*)((uint8_t*)cpu+0x500));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}