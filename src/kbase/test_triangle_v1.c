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

    printf("=== Mali - Draw Triangle Test ===\n\n");

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

    /* Allocate 16 pages for vertex buffer + job */
    uint64_t mem[4] = {0};
    mem[0] = 16;
    mem[1] = 16;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    if (ret < 0) { perror("MEM_ALLOC"); close(fd); return 1; }
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 16 * 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    /* 
     * Layout:
     * 0x0000: Job header (fragment shader)
     * 0x0100: Fragment shader code (minimal - just output red)
     * 0x0200: Vertex buffer (3 vertices)
     * 0x0300: Render target (output)
     */
    memset(cpu_mem, 0, 16 * 4096);

    /* Marker at offset 0x300 - if GPU runs, will change */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x300);
    *marker = 0xDEADBEEF;
    printf("[OK] Marker at offset 0x300: 0x%08x\n", *marker);

    /* 
     * Fragment shader job at offset 0
     * 
     * Valhall Fragment Job Header (128 bytes):
     * +0x00: exception_status (4)
     * +0x04: first_incomplete_task (4)
     * +0x08: fault_pointer (8)
     * +0x10: control (4) - job type, barrier, index
     * +0x14: deps (4)
     * +0x18: next_job_ptr (8)
     * +0x20: payload...
     * 
     * For now, just use WRITE_VALUE to verify it works, 
     * then we can add actual fragment shader later
     */
    job = (uint32_t *)cpu_mem;
    
    /* Job header at offset 0 */
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE job type=2, index=1 */
    job[8] = (uint32_t)(gpu_va + 0x300);  /* target = marker */
    job[9] = 0;
    job[10] = 6;  /* Immediate32 */
    job[12] = 0xCAFEBABE;  /* value */

    printf("[OK] Job: WRITE_VALUE to marker\n");

    /* Submit with CS+CF like Chrome */
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

    printf("[CHECK] Marker: 0x%08x (was 0xDEADBEEF)\n", *marker);

    if (*marker == 0xCAFEBABE) {
        printf("\n*** SUCCESS: GPU EXECUTED! ***\n");
        printf("Now let's add actual vertex/fragment shader for triangle...\n");
    }

    /* Read event */
    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    if (*(uint32_t *)ev) printf("[OK] Event: 0x%08x\n", *(uint32_t *)ev);

    munmap(cpu_mem, 16 * 4096);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}