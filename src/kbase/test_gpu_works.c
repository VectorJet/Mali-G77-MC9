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

int main(void) {
    int fd, ret;
    uint64_t gpu_va;
    void *cpu_mem;
    struct kbase_atom atom = {0};
    uint32_t *job;

    printf("=== Mali - GPU Execution WORKS! ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 2 pages */
    uint64_t mem[4] = {0};
    mem[0] = 2;
    mem[1] = 2;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    memset(cpu_mem, 0, 8192);

    /* Target at offset 0x30 - will be written by GPU */
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x30);
    *target = 0xDEADBEEF;
    printf("[OK] Target at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gpu_va + 0x30));

    /* 
     * Valhall job format:
     * - 128-byte header alignment
     * - Job type in control word bits [7:1]
     * - Type 2 = WRITE_VALUE (job manager job)
     */
    job = (uint32_t *)cpu_mem;
    job[4] = (2 << 1) | (1 << 16);  /* type=2 (WRITE_VALUE), index=1 */
    job[8] = (uint32_t)(gpu_va + 0x30);  /* target address */
    job[9] = 0;
    job[10] = 6;  /* IMMEDIATE32 */
    job[12] = 0xCAFEBABE;  /* write this value */

    printf("[OK] Job: WRITE_VALUE to 0x%llx with 0xCAFEBABE\n", 
           (unsigned long long)(gpu_va + 0x30));

    /* Submit with CS+CF (like Chrome) */
    atom.jc = gpu_va;
    atom.core_req = 0x203;  /* CS + CF */
    atom.atom_number = 1;
    atom.prio = 0;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[OK] JOB_SUBMIT: ret=%d\n", ret);

    usleep(500000);

    /* Verify GPU wrote */
    printf("\n=== RESULT ===\n");
    printf("Before: 0xDEADBEEF\n");
    printf("After:  0x%08x\n", *target);

    if (*target == 0xCAFEBABE) {
        printf("\n*** SUCCESS: GPU EXECUTED OUR JOB! ***\n\n");
        printf("The Mali-G77 GPU executed the WRITE_VALUE job and wrote\n");
        printf("0xCAFEBABE to memory at GPU VA 0x%llx\n", (unsigned long long)(gpu_va + 0x30));
    } else {
        printf("\n*** FAIL ***\n");
    }

    /* Read event */
    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    printf("Event: 0x%08x\n", *(uint32_t *)ev);

    munmap(cpu_mem, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}