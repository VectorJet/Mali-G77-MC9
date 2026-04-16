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

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Dump memory to verify job structure ===\n");
    
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    /* Job at offset 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);  /* control */
    job[8] = (uint32_t)(mem[1] + 0x100);  /* target low */
    job[9] = 0;  /* target high */
    job[10] = 6;  /* type: Immediate32 */
    job[12] = 0xCAFEBABE;  /* value */
    
    /* Marker at offset 0x100 */
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
    *marker = 0xDEADBEEF;
    
    printf("GPU VA: 0x%llx\n", (unsigned long long)mem[1]);
    printf("Job at 0: control=0x%08x, target=0x%08x%08x, type=0x%08x, value=0x%08x\n",
           job[4], job[9], job[8], job[10], job[12]);
    printf("Marker at 0x100: 0x%08x\n", *marker);
    
    struct kbase_atom atom = {0};
    atom.jc = mem[1];
    atom.core_req = 0x010;
    atom.atom_number = 1;
    
    printf("Atom: jc=0x%llx, core_req=0x%x, atom_number=%d\n",
           (unsigned long long)atom.jc, atom.core_req, atom.atom_number);
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("After submit...\n");
    
    uint8_t event[24];
    read(fd, event, sizeof(event));
    printf("Event: 0x%02x 0x%02x 0x%02x 0x%02x\n", event[0], event[1], event[2], event[3]);
    
    printf("Marker after: 0x%08x (expected 0xCAFEBABE)\n", *marker);
    
    /* Also dump job memory to see if GPU wrote anything */
    printf("\nJob memory after:\n");
    for (int i = 0; i < 16; i++) {
        printf("  [%02x] 0x%08x\n", i*4, job[i]);
    }
    
    munmap(cpu, 8192);
    close(fd);
    printf("=== Done ===\n");
    return 0;
}