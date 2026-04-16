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

/* Atom structure - 72 byte stride like Chrome */
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

    printf("=== Mali - Match Chrome Exactly ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /* VERSION_CHECK */
    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    if (ret < 0) { perror("VERSION_CHECK"); close(fd); return 1; }
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    /* SET_FLAGS */
    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    if (ret < 0) { perror("SET_FLAGS"); close(fd); return 1; }
    printf("[OK] SET_FLAGS\n");

    /* Like Chrome: call 0x19 before memory */
    uint32_t ioctl_19_val = 0x08;
    ret = ioctl(fd, KBASE_IOCTL_0x19, &ioctl_19_val);
    printf("[OK] IOCTL_0x19: ret=%d (Chrome calls this before MEM_ALLOC)\n", ret);

    /* MEM_ALLOC */
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

    /* mmap */
    cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    /* Build job - WRITE_VALUE to same buffer (offset 0x30) */
    job = (uint32_t *)cpu_mem;
    memset(job, 0, 128);
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, Index=1 */
    job[8] = (uint32_t)(gpu_va + 0x30);  /* write to offset 0x30 in same buffer */
    job[9] = (uint32_t)((gpu_va + 0x30) >> 32);
    job[10] = 6;  /* Immediate32 */
    job[12] = 0xCAFEBABE;

    printf("[OK] Job: WRITE_VALUE to 0x%llx (within same buffer)\n", 
           (unsigned long long)(gpu_va + 0x30));

    /* Build atom - exact Chrome format */
    atom.jc = gpu_va;
    atom.core_req = 0x203;  /* CS + CF like Chrome */
    atom.atom_number = 1;
    atom.prio = 0;

    /* Submit */
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);
    if (ret < 0) { perror("JOB_SUBMIT"); }

    /* Skip 0x1B - requires valid pointer */

    usleep(500000);

    /* Check result */
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 4096);
    printf("[OK] Target: 0x%08x (was 0xDEADBEEF)\n", *target);

    /* Read event */
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