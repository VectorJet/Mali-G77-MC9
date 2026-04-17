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
#define KBASE_IOCTL_MEM_FREE      _IOC(_IOC_WRITE, 0x80, 7, 8)
#define KBASE_IOCTL_0x19          _IOC(_IOC_WRITE, 0x80, 0x19, 4)
#define KBASE_IOCTL_0x1B          _IOC(_IOC_WRITE, 0x80, 0x1B, 16)

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
    uint64_t gpu_va, target_va;
    void *cpu_mem;
    struct kbase_atom atom = {0};
    uint32_t *job;

    printf("=== Mali - WRITE_VALUE Test (Fixed) ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    if (ret < 0) { perror("VERSION_CHECK"); close(fd); return 1; }
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    if (ret < 0) { perror("SET_FLAGS"); close(fd); return 1; }
    printf("[OK] SET_FLAGS\n");

    uint64_t mem[4] = {0};
    mem[0] = 2;
    mem[1] = 2;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    if (ret < 0) { perror("MEM_ALLOC"); close(fd); return 1; }
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    /* Set initial value at offset 0x30 */
    target_va = gpu_va + 0x30;
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x30);
    *target = 0xDEADBEEF;
    printf("[OK] Target: *target = 0x%08x at GPU VA 0x%llx\n", *target, (unsigned long long)target_va);

    /* Build WRITE_VALUE job at offset 0 */
    job = (uint32_t *)cpu_mem;
    memset(job, 0, 128);
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE job type, Index=1 */
    job[8] = (uint32_t)target_va;   /* target address (low 32 bits) */
    job[9] = (uint32_t)(target_va >> 32);  /* target address (high 32 bits) */
    job[10] = 6;  /* Immediate32 */
    job[12] = 0xCAFEBABE;  /* value to write */

    printf("[OK] Job: WRITE_VALUE to 0x%llx with 0xCAFEBABE\n", (unsigned long long)target_va);

    /* Build atom */
    atom.jc = gpu_va;
    atom.core_req = 0x203;  /* CS + CF like Chrome */
    atom.atom_number = 1;
    atom.prio = 0;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);
    if (ret < 0) { perror("JOB_SUBMIT"); }

    /* Wait for GPU */
    usleep(500000);

    /* Check result at the CORRECT offset */
    printf("[CHECK] Target at offset 0x30: 0x%08x (was 0xDEADBEEF)\n", *target);

    if (*target == 0xCAFEBABE) {
        printf("\n*** SUCCESS: GPU EXECUTED! ***\n");
    } else if (*target == 0xDEADBEEF) {
        printf("\n*** FAIL: GPU did not execute ***\n");
    } else {
        printf("\n*** UNEXPECTED: GPU wrote something else ***\n");
    }

    uint8_t ev[24] = {0};
    ssize_t ev_bytes = read(fd, ev, sizeof(ev));
    if (ev_bytes > 0) {
        printf("[OK] Event: code=0x%08x\n", *(uint32_t *)ev);
    }

    munmap(cpu_mem, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}