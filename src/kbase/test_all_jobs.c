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

int fd;

void setup_gpu(uint64_t *gpu_va, void **cpu_mem) {
    uint64_t mem[4] = {0};
    mem[0] = 4;
    mem[1] = 4;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    int ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    *gpu_va = mem[1];
    *cpu_mem = mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_SHARED, fd, *gpu_va);
    memset(*cpu_mem, 0, 16384);
}

int submit_job(uint64_t jc, uint32_t core_req, uint32_t job_type) {
    struct kbase_atom atom = {0};
    atom.jc = jc;
    atom.core_req = core_req;
    atom.atom_number = 1;
    atom.prio = 0;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    return ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
}

void build_job_header(void *job, uint32_t type, uint32_t index) {
    uint32_t *j = (uint32_t *)job;
    j[0] = 0;  /* exception_status */
    j[1] = 0;  /* first_incomplete */
    j[2] = 0;  /* fault_pointer lo */
    j[3] = 0;  /* fault_pointer hi */
    j[4] = (type << 1) | (index << 16);  /* control word */
    j[5] = 0;  /* deps */
    j[6] = 0;  /* next lo */
    j[7] = 0;  /* next hi */
}

void check_event() {
    uint8_t ev[24] = {0};
    while (read(fd, ev, sizeof(ev)) > 0) {
        printf("    Event: code=0x%08x\n", *(uint32_t *)ev);
    }
}

int test_job_type(const char *name, uint32_t job_type, uint32_t core_req) {
    printf("\n=== Test: %s (job_type=%u, core_req=0x%03x) ===\n", name, job_type, core_req);
    
    uint64_t gpu_va;
    void *cpu_mem;
    setup_gpu(&gpu_va, &cpu_mem);
    
    /* Build job at offset 0x100 */
    void *job = (uint8_t *)cpu_mem + 0x100;
    build_job_header(job, job_type, 1);
    
    /* Add marker at target */
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x300);
    *marker = 0xDEADBEEF;
    
    printf("    jc=0x%llx, marker=0x%llx\n", (unsigned long long)(gpu_va + 0x100), 
           (unsigned long long)(gpu_va + 0x300));
    
    int ret = submit_job(gpu_va + 0x100, core_req, job_type);
    printf("    JOB_SUBMIT: ret=%d\n", ret);
    
    usleep(200000);
    
    /* Check marker */
    printf("    Marker before: 0xDEADBEEF, after: 0x%08x\n", *marker);
    
    /* Check job exception status */
    uint32_t *j = (uint32_t *)job;
    printf("    exception_status: 0x%08x\n", j[0]);
    printf("    first_incomplete: 0x%08x\n", j[1]);
    
    check_event();
    
    munmap(cpu_mem, 16384);
    return ret;
}

int main(void) {
    printf("=== Mali - All Job Types Test ===\n\n");
    
    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);
    
    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);
    
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    printf("[OK] SET_FLAGS\n\n");
    
    /* Test all job types with various core_req values */
    
    /* DEP - no hardware */
    test_job_type("DEP (type=0, core_req=0x000)", 0, 0x000);
    
    /* Fragment (0x0E) with different core_req */
    test_job_type("FRAGMENT (type=14, core_req=0x201)", 14, 0x201);
    test_job_type("FRAGMENT (type=14, core_req=0x203)", 14, 0x203);
    test_job_type("FRAGMENT (type=14, core_req=0x001)", 14, 0x001);
    
    /* Compute Shader (0x03) */
    test_job_type("CS (type=3, core_req=0x202)", 3, 0x202);
    test_job_type("CS (type=3, core_req=0x002)", 3, 0x002);
    
    /* Tiler (0x0D) */
    test_job_type("TILER (type=13, core_req=0x204)", 13, 0x204);
    test_job_type("TILER (type=13, core_req=0x004)", 13, 0x004);
    
    /* WRITE_VALUE (0x02) */
    test_job_type("WRITE_VALUE (type=2, core_req=0x010)", 2, 0x010);
    test_job_type("WRITE_VALUE (type=2, core_req=0x210)", 2, 0x210);
    test_job_type("WRITE_VALUE (type=2, core_req=0x203)", 2, 0x203);
    
    /* Vertex (0x04) */
    test_job_type("VERTEX (type=4, core_req=0x008)", 4, 0x008);
    
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}