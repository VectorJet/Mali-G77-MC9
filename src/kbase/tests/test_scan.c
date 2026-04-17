#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <signal.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)

/* Atom structure */
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

uint64_t gpu_va;
void *cpu_mem;

void alloc_memory() {
    uint64_t mem[4] = {0};
    mem[0] = 4;
    mem[1] = 4;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    gpu_va = mem[1];
    cpu_mem = mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    memset(cpu_mem, 0, 16384);
}

int submit_job(uint64_t jc, uint32_t core_req) {
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

void build_header(void *job, uint32_t type, uint32_t index) {
    uint32_t *j = (uint32_t *)job;
    j[0] = 0; j[1] = 0; j[2] = 0; j[3] = 0;
    j[4] = (type << 1) | (index << 16);
    j[5] = 0; j[6] = 0; j[7] = 0;
}

void drain_events() {
    uint8_t ev[24];
    while (read(fd, ev, sizeof(ev)) > 0) {}
}

int test(const char *name, uint32_t job_type, uint32_t core_req) {
    printf("\n=== %s (type=%u, core_req=0x%03x) ===\n", name, job_type, core_req);
    fflush(stdout);
    
    /* Fresh allocation each time */
    if (cpu_mem) munmap(cpu_mem, 16384);
    alloc_memory();
    
    void *job = (uint8_t *)cpu_mem + 0x100;
    build_header(job, job_type, 1);
    
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x300);
    *marker = 0xDEADBEEF;
    
    int ret = submit_job(gpu_va + 0x100, core_req);
    printf("submit=%d ", ret);
    fflush(stdout);
    
    /* Non-blocking check */
    usleep(100000);
    
    printf("marker=0x%08x ", *marker);
    uint32_t *j = (uint32_t *)job;
    printf("ex=0x%02x ", j[0]);
    drain_events();
    printf("OK\n");
    fflush(stdout);
    
    return ret;
}

int main(void) {
    printf("=== Job Type Scan ===\n");
    fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Quick scan */
    test("DEP_0", 0, 0x000);
    test("DEP_203", 0, 0x203);
    
    test("FRAG_201", 14, 0x201);
    test("FRAG_203", 14, 0x203);
    test("FRAG_001", 14, 0x001);
    
    test("CS_202", 3, 0x202);
    test("CS_002", 3, 0x002);
    
    test("TIL_204", 13, 0x204);
    test("TIL_004", 13, 0x004);
    
    test("WV_010", 2, 0x010);
    test("WV_210", 2, 0x210);
    test("WV_203", 2, 0x203);
    
    test("VERT_008", 4, 0x008);
    
    if (cpu_mem) munmap(cpu_mem, 16384);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}