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
#define KBASE_IOCTL_0x19          _IOC(_IOC_WRITE, 0x80, 0x19, 4)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

struct submit_args {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Chrome calls 0x19 before MEM_ALLOC - try it */
    printf("=== Test: Call ioctl 0x19 (like Chrome) ===\n");
    uint32_t ioctl_19_val = 0x08;
    ioctl(fd, KBASE_IOCTL_0x19, &ioctl_19_val);
    printf("ioctl_0x19 returned\n");
    
    /* Now try job */
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x100);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xDEADBEEF;
    
    struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
    struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    read(fd, (uint8_t[24]){0}, 24);
    printf("marker=0x%08x\n", *marker);
    
    munmap(cpu, 8192);
    
    /* Test different 0x19 values */
    printf("=== Test 0x19 value 0x01 ===\n");
    ioctl_19_val = 0x01;
    ioctl(fd, KBASE_IOCTL_0x19, &ioctl_19_val);
    
    mem[0]=2; mem[1]=2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x100);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    
    marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xDEADBEEF;
    
    atom.jc = mem[1];
    s.addr = (uint64_t)&atom;
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    read(fd, (uint8_t[24]){0}, 24);
    printf("marker=0x%08x\n", *marker);
    
    munmap(cpu, 8192);
    
    printf("=== Test 0x19 value 0x10 ===\n");
    ioctl_19_val = 0x10;
    ioctl(fd, KBASE_IOCTL_0x19, &ioctl_19_val);
    
    mem[0]=2; mem[1]=2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x100);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    
    marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xDEADBEEF;
    
    atom.jc = mem[1];
    s.addr = (uint64_t)&atom;
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    read(fd, (uint8_t[24]){0}, 24);
    printf("marker=0x%08x\n", *marker);
    
    munmap(cpu, 8192);
    close(fd);
    printf("=== Done ===\n");
    return 0;
}