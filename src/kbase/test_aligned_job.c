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

    printf("=== Mali - 128-byte Aligned WRITE_VALUE ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 2 pages for job + target */
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

    /* Job at offset 0 (start of allocation) - may need alignment */
    job = (uint32_t *)cpu_mem;
    memset(job, 0, 256);

    /* Write marker BEFORE job at offset 0x80 (128 bytes) */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x80);
    *marker = 0xDEADBEEF;
    printf("[OK] Marker at +0x80: 0x%08x\n", *marker);

    /* Build WRITE_VALUE job at offset 0 - ensuring 128-byte alignment */
    /* Header at +0x10 */
    job[4] = (2 << 1) | (1 << 16);  /* Job Type=2 (WRITE_VALUE), Index=1 */
    /* Next pointer at +0x18 - zero means last job */
    job[6] = 0;
    /* Payload at +0x20 */
    job[8] = (uint32_t)(gpu_va + 0x80);  /* target = marker location */
    job[9] = (uint32_t)((gpu_va + 0x80) >> 32);
    job[10] = 6;  /* Immediate32 */
    job[12] = 0xCAFEBABE;  /* value to write */

    printf("[OK] Job: WRITE_VALUE to gpu_va+0x80 = 0x%llx\n", 
           (unsigned long long)(gpu_va + 0x80));
    printf("[OK] Job format:\n");
    printf("  job[4]  (ctrl)   = 0x%08x\n", job[4]);
    printf("  job[8]  (addr lo)= 0x%08x\n", job[8]);
    printf("  job[9]  (addr hi)= 0x%08x\n", job[9]);
    printf("  job[10] (type)   = 0x%08x\n", job[10]);
    printf("  job[12] (value)  = 0x%08x\n", job[12]);

    /* Atom */
    atom.jc = gpu_va;  /* job at start of allocation */
    atom.core_req = 0x203;  /* CS + CF */
    atom.atom_number = 1;
    atom.prio = 0;

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

    /* Check result */
    printf("[OK] Marker after wait: 0x%08x (was 0xDEADBEEF)\n", *marker);

    /* Also check job header for exception status */
    printf("[OK] Job exception_status: 0x%08x\n", job[0]);
    printf("[OK] Job fault_pointer: 0x%llx\n", *(uint64_t *)&job[2]);

    /* Read events */
    uint8_t ev[24] = {0};
    while (read(fd, ev, sizeof(ev)) > 0) {
        printf("[OK] Event: code=0x%08x\n", *(uint32_t *)ev);
    }

    munmap(cpu_mem, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}