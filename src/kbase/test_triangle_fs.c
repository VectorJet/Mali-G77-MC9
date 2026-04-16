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

    printf("=== Mali - Draw Triangle with FS job ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 4 pages */
    uint64_t mem[4] = {0};
    mem[0] = 4;
    mem[1] = 4;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 4 * 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    memset(cpu_mem, 0, 4 * 4096);

    /* Marker at offset 0x100 */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x100);
    *marker = 0xDEADBEEF;
    printf("[OK] Marker at 0x%llx: 0x%08x\n", (unsigned long long)(gpu_va + 0x100), *marker);

    /* 
     * Try FS job (core_req = 0x1) - Fragment Shader
     * This should actually render pixels to the framebuffer
     */
    job = (uint32_t *)cpu_mem;
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, index=1 */
    job[8] = (uint32_t)(gpu_va + 0x100);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;

    printf("[OK] Job: WRITE_VALUE with FS (core_req=0x1)\n");

    /* Try FS instead of CS+CF */
    atom.jc = gpu_va;
    atom.core_req = 0x1;  /* FS - Fragment Shader */
    atom.atom_number = 1;
    atom.prio = 0;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);

    usleep(500000);

    printf("[CHECK] Marker: 0x%08x\n", *marker);
    if (*marker == 0xCAFEBABE) printf("*** SUCCESS: GPU EXECUTED! ***\n");
    else if (*marker == 0xDEADBEEF) printf("*** FAIL: GPU did not run ***\n");
    else printf("*** UNEXPECTED ***\n");

    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    if (*(uint32_t *)ev) printf("[OK] Event: 0x%08x\n", *(uint32_t *)ev);

    munmap(cpu_mem, 4 * 4096);
    close(fd);
    printf("=== Done ===\n");
    return 0;
}