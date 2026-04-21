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
#define BASE_MEM_SAME_VA       (1ULL << 13)

#pragma pack(push, 1)
struct kbase_atom {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t  jit_id[2];
    uint8_t  pre_dep_atom[2];
    uint8_t  pre_dep_type[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;
};
#pragma pack(pop)

int main(void) {
    int fd, ret;
    uint64_t gpu_va;
    void *cpu_mem;
    struct kbase_atom atom = {0};

    printf("=== Mali-G77 Basic Test ===\n\n");

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
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
              BASE_MEM_SAME_VA;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    if (ret < 0) { perror("MEM_ALLOC"); close(fd); return 1; }

    printf("[OK] MEM_ALLOC: [0]=0x%llx, [1]=0x%llx, [2]=0x%llx, [3]=0x%llx\n",
            (unsigned long long)mem[0], (unsigned long long)mem[1],
            (unsigned long long)mem[2], (unsigned long long)mem[3]);

    gpu_va = mem[1];
    printf("[OK] GPU VA: 0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x30);
    *target = 0xDEADBEEF;
    printf("[OK] Target: 0x%08x at offset 0x30\n", *target);

    uint32_t *job = (uint32_t *)cpu_mem;
    memset(job, 0, 128);
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(gpu_va + 0x30);
    job[9] = (uint32_t)((gpu_va + 0x30) >> 32);
    job[10] = 6;
    job[12] = 0xCAFEBABE;

    printf("[OK] WRITE_VALUE job: target=0x%llx, value=0xCAFEBABE\n",
            (unsigned long long)(gpu_va + 0x30));

    atom.jc = gpu_va;
    atom.core_req = 0x203;
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr, stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);
    if (ret < 0) { perror("ioctl"); }

    usleep(100000);

    printf("[OK] Target after: 0x%08x (was 0xDEADBEEF)\n", *target);

    munmap(cpu_mem, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}