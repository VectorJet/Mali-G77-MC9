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

#define BASE_MEM_PROT_GPU_RDWR 0xF

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int test(int fd, const char *name, uint32_t core_req) {
    uint64_t mem[4] = {2, 2, 0, BASE_MEM_PROT_GPU_RDWR};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    uint32_t *job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (0 << 1) | (1 << 16);
    
    struct kbase_atom a = {0};
    a.jc = mem[1] + 0x100;
    a.core_req = core_req;
    a.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms, stride; } s = {.addr=(uint64_t)&a, .nr_atoms=1, .stride=72};
    int r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("%s: %d\n", name, r);
    
    munmap(cpu, 8192);
    return r;
}

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Quick - just test a few */
    test(fd, "000", 0x000);
    test(fd, "001", 0x001);
    test(fd, "002", 0x002);
    test(fd, "010", 0x010);
    test(fd, "100", 0x100);
    test(fd, "200", 0x200);
    test(fd, "203", 0x203);
    
    close(fd);
    return 0;
}