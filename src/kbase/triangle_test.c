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

#define BASE_JD_REQ_DEP  0
#define BASE_JD_REQ_FS   (1u << 0)
#define BASE_JD_REQ_CS   (1u << 1)
#define BASE_JD_REQ_T    (1u << 2)
#define BASE_JD_REQ_V    (1u << 4)

/* Valhall job header (128-byte aligned) */
struct valhall_job_header {
    uint32_t exception_status;
    uint32_t first_incomplete_task;
    uint64_t fault_pointer;
    uint32_t control_word;
    uint32_t dep_info;
    uint64_t next_job_ptr;
} __attribute__((packed));

/* Atom structure - 64 byte stride from breakthrough */
struct kbase_atom {
    uint64_t seq_nr;           /* +0x00 */
    uint64_t jc;               /* +0x08 - GPU VA of job chain */
    uint64_t udata[2];         /* +0x10 - user data */
    uint64_t extres_list;      /* +0x20 */
    uint16_t nr_extres;        /* +0x28 */
    uint16_t jit_id[2];        /* +0x2a */
    uint8_t pre_dep_atom[2];    /* +0x2c */
    uint8_t pre_dep_type[2];   /* +0x2e */
    uint8_t atom_number;       /* +0x30 */
    uint8_t prio;              /* +0x31 */
    uint8_t device_nr;          /* +0x32 */
    uint8_t jobslot;           /* +0x33 */
    uint32_t core_req;         /* +0x34 - 0x10 = V (value writeback) */
    uint8_t renderpass_id;     /* +0x38 */
    uint8_t padding[7];         /* +0x39 */
} __attribute__((packed));

int main(void) {
    int fd, ret;
    uint64_t gpu_va, target_va;
    void *cpu_mem, *target_mem;
    struct kbase_atom atom = {0};
    struct valhall_job_header *job;

    printf("=== Mali Triangle Draw Test (r49) ===\n\n");

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

    /* MEM_ALLOC - allocate 2 pages: 1 for job, 1 for target */
    uint64_t mem[4] = {0};
    mem[0] = 2;  /* va_pages */
    mem[1] = 2;  /* commit_pages */
    mem[2] = 0;  /* extension */
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

    /* Job goes at offset 0, target at offset 4096 */
    job = (struct valhall_job_header *)cpu_mem;
    target_va = gpu_va + 4096;
    target_mem = (uint8_t *)cpu_mem + 4096;

    /* Write sentinel to target */
    volatile uint32_t *target = (volatile uint32_t *)target_mem;
    *target = 0xDEADBEEF;
    printf("[OK] Target init: 0x%08x\n", *target);

    /* Build Valhall TILER job instead of FRAGMENT */
    memset(job, 0, sizeof(*job));
    /* Tiler job: job type 3 */
    job->control_word = (3 << 1) | (1 << 16);  /* Job type 3 = TILER, Index=1 */
    job->next_job_ptr = 0;  /* Last job */

    printf("[OK] Job built: type=TILER job\n");

    /* Build atom - 72 byte stride matches Chrome captures */
    atom.jc = gpu_va;  /* Job at start of allocation */
    atom.core_req = BASE_JD_REQ_CS;  /* Compute shader - matches Chrome captures (0x203) */
    atom.atom_number = 1;
    atom.prio = 0;

    /* Submit - use stride=72 like Chrome captures */
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);
    if (ret < 0) { perror("JOB_SUBMIT"); }

    /* Wait for completion */
    printf("[..] Waiting for GPU...\n");
    usleep(500000);

    /* Check result */
    printf("[OK] Target after wait: 0x%08x (was 0xDEADBEEF)\n", *target);

    /* Read event */
    uint8_t ev[24] = {0};
    ssize_t ev_bytes = read(fd, ev, sizeof(ev));
    if (ev_bytes > 0) {
        uint32_t event_code = *(uint32_t *)ev;
        printf("[OK] Event: code=0x%08x\n", event_code);
    }

    /* Cleanup */
    munmap(cpu_mem, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}