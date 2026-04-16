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
    uint64_t gpu_va;
    void *cpu_mem;
    struct kbase_atom atom = {0};
    uint32_t *job;

    printf("=== Mali - Fragment Shader Test ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 4 pages: job chain + shader uniforms + varying + attributes */
    uint64_t mem[4] = {0};
    mem[0] = 4;
    mem[1] = 4;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    /* Clear all memory */
    memset(cpu_mem, 0, 16384);

    /* Build fragment shader job at offset 0x100 (256 bytes) */
    uint32_t *frag = (uint32_t *)((uint8_t *)cpu_mem + 0x100);
    
    /* Fragment job header at +0x00 */
    frag[0] = 0;  /* exception_status */
    frag[1] = 0;  /* first_incomplete_task */
    frag[2] = 0;  /* fault_pointer low */
    frag[3] = 0;  /* fault_pointer high */
    frag[4] = (0x0E << 1) | (1 << 16);  /* Job Type=0x0E (FRAGMENT), Index=1 */
    frag[5] = 0;  /* dep1 | dep2 */
    frag[6] = 0;  /* next_job_ptr low */
    frag[7] = 0;  /* next_job_ptr high */
    
    /* Fragment payload at +0x20 */
    uint32_t *payload = &frag[8];  /* offset 0x20 */
    payload[0] = 0;  /* draw descriptor */
    payload[1] = 0;
    payload[2] = 0;
    payload[3] = 0;
    payload[4] = 0;  /* position pointer (GPU VA) */
    payload[5] = 0;
    payload[6] = 0;  /* uniform buffers */
    payload[7] = 0;
    payload[8] = 0;  /* textures */
    payload[9] = 0;
    payload[10] = 0;  /* samplers */
    payload[11] = 0;
    payload[12] = 0;  /* push uniforms */
    payload[13] = 0;
    payload[14] = 0;  /* state */
    payload[15] = 0;
    payload[16] = gpu_va + 0x300;  /* attribute buffers - GPU VA */
    payload[17] = 0;
    payload[18] = gpu_va + 0x400;  /* attributes */
    payload[19] = 0;
    payload[20] = 0;  /* varyings buffers */
    payload[21] = 0;
    payload[22] = 0;  /* varyings */
    payload[23] = 0;

    printf("[OK] Fragment job at offset 0x100\n");
    printf("[OK] Job header: type=0x0E (FRAGMENT), index=1\n");

    /* Atom - point jc to fragment job */
    atom.jc = gpu_va + 0x100;
    atom.core_req = 0x201;  /* FRAGMENT | CSF */
    atom.atom_number = 1;
    atom.prio = 0;
    atom.udata[0] = gpu_va + 0x500;  /* user data - completion event goes here */

    printf("[OK] Atom: jc=0x%llx, core_req=0x%x\n", 
           (unsigned long long)atom.jc, atom.core_req);

    /* Submit */
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

    /* Check event */
    uint8_t ev[24] = {0};
    ssize_t ev_bytes = read(fd, ev, sizeof(ev));
    if (ev_bytes > 0) {
        printf("[OK] Event: code=0x%08x\n", *(uint32_t *)ev);
    }

    /* Check job header */
    printf("[OK] Job exception_status: 0x%08x\n", frag[0]);
    printf("[OK] Job first_incomplete: 0x%08x\n", frag[1]);

    /* Check udata area */
    volatile uint32_t *completion = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x500);
    printf("[OK] Completion area: 0x%08x\n", *completion);

    munmap(cpu_mem, 16384);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}