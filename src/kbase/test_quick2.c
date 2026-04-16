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

volatile int alarm_triggered = 0;

void alarm_handler(int sig) {
    alarm_triggered = 1;
}

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)
#define BASE_MEM_PROT_GPU_RDWR (BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int do_test(int fd, const char *name, uint32_t job_type, uint32_t core_req) {
    printf("%s: ", name); fflush(stdout);
    alarm(2);
    
    uint64_t mem[4] = {2, 2, 0, BASE_MEM_PROT_GPU_RDWR};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    uint32_t *job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (job_type << 1) | (1 << 16);
    
    /* Add payload for WRITE_VALUE */
    if (job_type == 2) {
        job[8] = (uint32_t)(mem[1] + 0x200);
        job[9] = 0;
        job[10] = 6;
        job[12] = 0xCAFEBABE;
        volatile uint32_t *m = (volatile uint32_t *)((uint8_t*)cpu + 0x200);
        *m = 0xDEADBEEF;
    }
    
    struct kbase_atom a = {0};
    a.jc = mem[1] + 0x100;
    a.core_req = core_req;
    a.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms, stride; } s = {.addr=(uint64_t)&a, .nr_atoms=1, .stride=72};
    int r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("submit=%d ", r);
    
    int result = 0;
    if (alarm_triggered) {
        printf("HANG!\n");
        alarm_triggered = 0;
        result = -1;
    } else {
        if (job_type == 2) {
            volatile uint32_t *m = (volatile uint32_t *)((uint8_t*)cpu + 0x200);
            printf("marker=0x%08x\n", *m);
        } else {
            printf("OK\n");
        }
    }
    alarm(0);
    munmap(cpu, 8192);
    return result;
}

int main(void) {
    signal(SIGALRM, alarm_handler);
    
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Quick Scan (no FRAGMENT) ===\n");
    
    /* DEP */
    do_test(fd, "DEP_0", 0, 0x000);
    do_test(fd, "DEP_203", 0, 0x203);
    
    /* CS - compute shader */
    do_test(fd, "CS_202", 3, 0x202);
    do_test(fd, "CS_002", 3, 0x002);
    do_test(fd, "CS_200", 3, 0x200);
    
    /* TILER */
    do_test(fd, "TIL_204", 13, 0x204);
    do_test(fd, "TIL_004", 13, 0x004);
    
    /* VERTEX */
    do_test(fd, "VERT_008", 4, 0x008);
    
    /* WRITE_VALUE */
    do_test(fd, "WV_010", 2, 0x010);
    do_test(fd, "WV_210", 2, 0x210);
    do_test(fd, "WV_203", 2, 0x203);
    do_test(fd, "WV_200", 2, 0x200);
    
    printf("\n=== Now testing FRAGMENT with different core_req ===\n");
    
    /* FRAGMENT - these hang! */
    do_test(fd, "FRAG_201", 14, 0x201);
    do_test(fd, "FRAG_001", 14, 0x001);
    do_test(fd, "FRAG_200", 14, 0x200);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}