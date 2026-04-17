#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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
 * Debug: Job chaining with next_job_ptr
 * 
 * Valhall job format (128-byte header):
 * +0x00: exception_status (4)
 * +0x04: first_incomplete_task (4)  
 * +0x08: fault_pointer (8)
 * +0x10: control (4) - job type in bits [7:1], index in [31:16]
 * +0x14: dep1 | dep2 (4)
 * +0x18: next_job_ptr (8) - GPU VA of next job
 * +0x20: payload starts
 */
int main(void) {
    printf("=== Debug Job Chaining (next_job_ptr) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    printf("[OK] Init done\n");

    /* Allocate 4 pages - enough for 2 jobs at 128-byte alignment */
    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gpu_va = mem[1];
    printf("[OK] Buffer: gpu_va=0x%llx, size=%d\n", (unsigned long long)gpu_va, 4*4096);

    memset(cpu, 0, 4*4096);

    /* Targets */
    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    volatile uint32_t *target2 = (volatile uint32_t *)((uint8_t*)cpu + 0x200);
    *target1 = 0xAAAAAAAA;
    *target2 = 0xBBBBBBBB;
    printf("[OK] target1 at 0x%llx = 0xAAAAAAAA\n", (unsigned long long)(gpu_va + 0x100));
    printf("[OK] target2 at 0x%llx = 0xBBBBBBBB\n\n", (unsigned long long)(gpu_va + 0x200));

    /* Job 1 at offset 0 (128-byte aligned) */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0);
    job1[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, index=1 */
    job1[8] = (uint32_t)(gpu_va + 0x100);  /* target1 */
    job1[9] = 0;
    job1[10] = 6;  /* IMMEDIATE32 */
    job1[12] = 0x11111111;
    /* next_job_ptr at offset 0x18 -> job2 */
    *(uint64_t *)((uint8_t*)cpu + 0x18) = gpu_va + 0x80;  /* 128 bytes from job1 start */

    printf("Job1 at 0x%llx:\n", (unsigned long long)gpu_va);
    printf("  control=0x%08x\n", job1[4]);
    printf("  target=0x%llx\n", (unsigned long long)(gpu_va + 0x100));
    printf("  next_job_ptr=0x%llx\n", *(uint64_t*)((uint8_t*)cpu + 0x18));

    /* Job 2 at offset 0x80 (128-byte aligned = 128 bytes from start = 0x80) */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x80);
    job2[4] = (2 << 1) | (2 << 16);  /* WRITE_VALUE, index=2 */
    job2[8] = (uint32_t)(gpu_va + 0x200);  /* target2 */
    job2[9] = 0;
    job2[10] = 6;
    job2[12] = 0x22222222;
    *(uint64_t *)((uint8_t*)cpu + 0x98) = 0;  /* end of chain */

    printf("\nJob2 at 0x%llx:\n", (unsigned long long)(gpu_va + 0x80));
    printf("  control=0x%08x\n", job2[4]);
    printf("  target=0x%llx\n", (unsigned long long)(gpu_va + 0x200));
    printf("  next_job_ptr=0x%llx (end)\n", *(uint64_t*)((uint8_t*)cpu + 0x98));

    /* Single atom pointing to first job */
    struct kbase_atom atom = {0};
    atom.jc = gpu_va;  /* start of job chain */
    atom.core_req = 0x203;
    atom.atom_number = 1;
    atom.prio = 0;

    printf("\nAtom: jc=0x%llx (points to job1)\n", (unsigned long long)atom.jc);

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("\n[OK] JOB_SUBMIT: ret=%d\n", ret);

    usleep(500000);

    printf("\n=== RESULT ===\n");
    printf("target1: 0x%08x (expected 0x11111111)\n", *target1);
    printf("target2: 0x%08x (expected 0x22222222)\n", *target2);

    if (*target1 == 0x11111111 && *target2 == 0x22222222) {
        printf("\n*** SUCCESS: Job chain executed both jobs! ***\n");
    } else if (*target1 == 0x11111111) {
        printf("\n*** PARTIAL: First job ran, second didn't (chain broken) ***\n");
    } else {
        printf("\n*** FAIL: No jobs ran ***\n");
    }

    /* Check event */
    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    printf("Event: 0x%08x\n", *(uint32_t*)ev);

    munmap(cpu, 4*4096);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}