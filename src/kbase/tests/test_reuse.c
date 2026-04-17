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

int submit_one_separate_allocation(int fd, uint32_t core_req, int value) {
    /* Fresh allocation each time */
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    /* Job at offset 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x100);  /* target = marker */
    job[9] = 0;
    job[10] = 6;  /* Immediate32 */
    job[12] = value;
    
    /* Marker at offset 0x100 */
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xFFFFFFFF;
    
    printf("Submit: gpu_va=0x%llx, target=0x%llx, value=0x%08x\n",
           (unsigned long long)mem[1],
           (unsigned long long)(mem[1] + 0x100),
           value);
    
    struct kbase_atom atom = {0};
    atom.jc = mem[1];  /* job at start of allocation */
    atom.core_req = core_req;
    atom.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    
    uint8_t ev[24];
    read(fd, ev, sizeof(ev));
    
    printf("  ret=%d, marker after=0x%08x\n", ret, *marker);
    
    munmap(cpu, 8192);
    return ret;
}

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Reuse fd, fresh allocations ===\n");
    
    submit_one_separate_allocation(fd, 0x010, 0x11111111);
    submit_one_separate_allocation(fd, 0x010, 0x22222222);
    submit_one_separate_allocation(fd, 0x010, 0x33333333);
    submit_one_separate_allocation(fd, 0x203, 0x44444444);
    submit_one_separate_allocation(fd, 0x001, 0x55555555);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}