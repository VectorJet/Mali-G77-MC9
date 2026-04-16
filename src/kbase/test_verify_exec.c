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

/* Atom structure - 72 byte stride */
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

    printf("=== Mali - Verify GPU Execution ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 1 page only - simpler */
    uint64_t mem[4] = {0};
    mem[0] = 1;
    mem[1] = 1;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    /* Write marker BEFORE job */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x100);
    *marker = 0xAAAAAAA1;
    printf("[OK] Marker at +0x100: 0x%08x\n", *marker);

    /* Build job - WRITE_VALUE to marker location */
    job = (uint32_t *)cpu_mem;
    memset(job, 0, 128);
    /* Header at +0x10 */
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, Index=1 */
    /* Payload at +0x20 */
    job[8] = (uint32_t)(gpu_va + 0x100);  /* write to marker location */
    job[9] = (uint32_t)((gpu_va + 0x100) >> 32);
    job[10] = 6;  /* Immediate32 */
    job[12] = 0xDEADBEEF;  /* new value */

    printf("[OK] Job: WRITE_VALUE to 0x%llx value=0xDEADBEEF\n", 
           (unsigned long long)(gpu_va + 0x100));

    /* Dump job before submit */
    printf("[OK] Job memory before submit:\n");
    for (int i = 0; i < 16; i++) {
        printf("  [%02x] %08x\n", i*4, job[i]);
    }

    /* Atom */
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
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);

    /* Wait */
    usleep(500000);

    /* Check result */
    printf("[OK] Marker at +0x100 after wait: 0x%08x (was 0xAAAAAAA1)\n", *marker);

    /* Dump job after */
    printf("[OK] Job memory after submit:\n");
    for (int i = 0; i < 16; i++) {
        printf("  [%02x] %08x\n", i*4, job[i]);
    }

    /* Read event */
    uint8_t ev[24] = {0};
    ssize_t ev_bytes = read(fd, ev, sizeof(ev));
    if (ev_bytes > 0) {
        printf("[OK] Event: code=0x%08x\n", *(uint32_t *)ev);
    }

    munmap(cpu_mem, 4096);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}