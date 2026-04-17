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

/*
 * Test: Job chain with DIFFERENT alignment requirements
 * 
 * Valhall job header is 128 bytes, but maybe:
 * - Need different alignment for first job vs subsequent
 * - Need to set some flag to indicate "this is a chain"
 * - next_job_ptr might be at different offset
 */
int main(void) {
    printf("=== Test: Job Chain with 256-byte alignment ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate bigger buffer */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gpu_va = mem[1];
    printf("Buffer: gpu_va=0x%llx, size=%d\n", (unsigned long long)gpu_va, 8*4096);

    memset(cpu, 0, 8*4096);

    /* Targets */
    volatile uint32_t *t1 = (volatile uint32_t *)((uint8_t*)cpu + 0x400);
    volatile uint32_t *t2 = (volatile uint32_t *)((uint8_t*)cpu + 0x800);
    *t1 = 0xAAAAAAAA;
    *t2 = 0xBBBBBBBB;

    /* Try with 256-byte (0x100) aligned jobs */
    /* Job1 at 0x200 (512 bytes from start) */
    uint32_t *j1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    j1[4] = (2 << 1) | (1 << 16);
    j1[8] = (uint32_t)(gpu_va + 0x400);
    j1[10] = 6;
    j1[12] = 0x11111111;
    /* next_job_ptr at 0x218 */
    *(uint64_t *)((uint8_t*)cpu + 0x218) = gpu_va + 0x300;  /* 0x300 = 768 bytes = 256-byte aligned */

    /* Job2 at 0x300 (256-byte aligned) */
    uint32_t *j2 = (uint32_t *)((uint8_t*)cpu + 0x300);
    j2[4] = (2 << 1) | (2 << 16);
    j2[8] = (uint32_t)(gpu_va + 0x800);
    j2[10] = 6;
    j2[12] = 0x22222222;
    *(uint64_t *)((uint8_t*)cpu + 0x318) = 0;  /* end */

    printf("Job1 at 0x%llx, next=0x%llx\n", (unsigned long long)(gpu_va+0x200), (unsigned long long)(gpu_va+0x300));
    printf("Job2 at 0x%llx, next=0\n", (unsigned long long)(gpu_va+0x300));

    /* Test with jc = 0x200 */
    struct kbase_atom atom = {0};
    atom.jc = gpu_va + 0x200;
    atom.core_req = 0x203;
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);

    usleep(500000);

    printf("\nResult: t1=0x%08x t2=0x%08x\n", *t1, *t2);
    if (*t1 == 0x11111111 && *t2 == 0x22222222) printf("SUCCESS!\n");
    else if (*t1 == 0x11111111) printf("First only\n");
    else printf("FAIL\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}