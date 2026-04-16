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

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test TILER Job ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate buffer */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Target at offset 0x30 */
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *target = 0xDEADBEEF;
    printf("Target at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x30));

    /* TILER job - type=4 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (4 << 1) | (1 << 16);  /* TILER type=4, index=1 */
    
    /* Polygon list pointer at word 8 */
    job[8] = (uint32_t)(gva + 0x100);  /* polygon list at 0x100 */
    job[9] = 0;
    
    /* Tiler payload */
    job[10] = 0;  /* bound min */
    job[11] = 0;  /* bound max */
    job[12] = 0;  /* flags */

    printf("TILER job at 0x%llx, polygon_list=0x%llx\n", 
           (unsigned long long)gva, (unsigned long long)(gva+0x100));

    /* Try core_req=0x004 (Tiler only) */
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x004;  /* Tiler only */
    atom.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    usleep(500000);

    printf("\nResult: Target=0x%08x (was 0xDEADBEEF)\n", *target);
    if (*target != 0xDEADBEEF) printf("*** GPU MODIFIED! ***\n");
    else printf("No change\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}