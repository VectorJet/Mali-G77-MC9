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
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

/* Exactly match test_multi pattern: job at offset 0 */
int test_offset_0(uint32_t core_req) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    /* Job at offset 0 (LIKE test_multi) */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x100);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    
    /* Marker at offset 0x100 */
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xDEADBEEF;
    
    struct kbase_atom atom = {0};
    atom.jc = mem[1];  /* job at start */
    atom.core_req = core_req;
    atom.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    read(fd, (uint8_t[24]){0}, 24);
    
    printf("core_req=0x%03x: marker=0x%08x (%s)\n", 
           core_req, *marker, (*marker == 0xCAFEBABE) ? "SUCCESS!" : "FAIL");
    
    munmap(cpu, 8192);
    close(fd);
    return 0;
}

int main(void) {
    printf("=== Job at offset 0 ===\n");
    test_offset_0(0x010);
    test_offset_0(0x203);
    test_offset_0(0x001);
    printf("=== Done ===\n");
    return 0;
}