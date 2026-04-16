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
    
    printf("=== Draw Triangle Test ===\n");
    
    /* Allocate memory for triangle */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 32768, 3, 1, fd, mem[1]);
    memset(cpu, 0, 32768);
    
    /* Layout:
     * 0x0000: Job chain header (not used directly)
     * 0x0100: Vertex job
     * 0x0200: Tiler job  
     * 0x0300: Fragment job
     * 0x0400: Vertex buffer (coordinates)
     * 0x0500: Tiler heap
     * 0x0600: Render target
     * 0x0700: Output buffer (for verification)
     */
    
    /* Marker to verify GPU execution */
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x700);
    *marker = 0xDEADBEEF;  /* Initial value */
    
    /* Write value to set marker after rendering */
    uint32_t *wv_job = (uint32_t *)((char*)cpu + 0x100);
    wv_job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, index=1 */
    wv_job[8] = (uint32_t)(mem[1] + 0x700);  /* target = marker */
    wv_job[9] = 0;
    wv_job[10] = 6;  /* Immediate32 */
    wv_job[12] = 0xCAFEBABE;  /* Write this to marker */
    
    printf("Setup: marker at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(mem[1] + 0x700));
    printf("Job: WRITE_VALUE to marker with 0xCAFEBABE\n");
    
    /* Submit just the WRITE_VALUE to verify setup works */
    struct kbase_atom atom = {0};
    atom.jc = mem[1] + 0x100;
    atom.core_req = 0x010;
    atom.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    
    /* Read event */
    uint8_t ev[24];
    read(fd, ev, sizeof(ev));
    
    printf("After submit: marker = 0x%08x (should be 0xCAFEBABE if GPU executed)\n", *marker);
    
    if (*marker == 0xCAFEBABE) {
        printf("SUCCESS: GPU executed our job!\n");
    } else {
        printf("FAIL: GPU did not execute\n");
    }
    
    munmap(cpu, 32768);
    close(fd);
    printf("=== Done ===\n");
    return 0;
}