#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

struct kbase_atom {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint16_t jit_id[2];
    uint8_t pre_dep_atom[2];
    uint8_t pre_dep_type[2];
    uint8_t atom_number;
    uint8_t prio;
    uint8_t device_nr;
    uint8_t jobslot;
    uint32_t core_req;
    uint8_t renderpass_id;
    uint8_t padding[7];
} __attribute__((packed));

void test_core_req(int fd, uint32_t core_req) {
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    uint32_t *job = (uint32_t *)((char*)cpu + 0x100);
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, index=1 */
    job[8] = (uint32_t)(mem[1] + 0x200);  /* target */
    job[9] = 0;
    job[10] = 6;  /* immediate32 */
    job[12] = 0xCAFEBABE;
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x200);
    *marker = 0xDEADBEEF;
    
    struct kbase_atom atom = {0};
    atom.jc = mem[1] + 0x100;
    atom.core_req = core_req;
    atom.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    
    printf("core_req=0x%03x: submit done, marker=0x%08x\n", core_req, *marker);
    
    munmap(cpu, 8192);
}

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Testing different core_req ===\n");
    
    test_core_req(fd, 0x000);
    test_core_req(fd, 0x001);
    test_core_req(fd, 0x002);
    test_core_req(fd, 0x010);
    test_core_req(fd, 0x100);
    test_core_req(fd, 0x200);
    test_core_req(fd, 0x201);
    test_core_req(fd, 0x202);
    test_core_req(fd, 0x203);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}