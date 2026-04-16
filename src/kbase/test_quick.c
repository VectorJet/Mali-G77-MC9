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

int main(void) {
    signal(SIGALRM, alarm_handler);
    
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Scan ===\n");
    
    /* Test 1: DEP with core_req=0 */
    printf("Test DEP_0: "); fflush(stdout);
    alarm(2);
    uint64_t mem[4] = {2, 2, 0, BASE_MEM_PROT_GPU_RDWR};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    memset(cpu, 0xFF, 64);  /* marker at start */
    
    struct kbase_atom a = {0};
    a.jc = mem[1] + 0x100;
    a.core_req = 0;
    a.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms, stride; } s = {.addr=(uint64_t)&a, .nr_atoms=1, .stride=72};
    int r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("submit=%d ", r);
    if (alarm_triggered) { printf("HANG\n"); alarm_triggered=0; }
    else { printf("OK\n"); }
    alarm(0);
    munmap(cpu, 8192);
    
    /* Test 2: FRAGMENT with core_req=0x201 */
    printf("Test FRAG_201: "); fflush(stdout);
    alarm(2);
    mem[0]=2; mem[1]=2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    uint32_t *job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (14 << 1) | (1 << 16);  /* type=14, index=1 */
    
    a.jc = mem[1] + 0x100;
    a.core_req = 0x201;
    r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("submit=%d ", r);
    if (alarm_triggered) { printf("HANG\n"); alarm_triggered=0; }
    else { printf("OK\n"); }
    alarm(0);
    munmap(cpu, 8192);
    
    /* Test 3: CS with core_req=0x202 */
    printf("Test CS_202: "); fflush(stdout);
    alarm(2);
    mem[0]=2; mem[1]=2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (3 << 1) | (1 << 16);  /* type=3, index=1 */
    
    a.jc = mem[1] + 0x100;
    a.core_req = 0x202;
    r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("submit=%d ", r);
    if (alarm_triggered) { printf("HANG\n"); alarm_triggered=0; }
    else { printf("OK\n"); }
    alarm(0);
    munmap(cpu, 8192);
    
    /* Test 4: WRITE_VALUE with core_req=0x010 */
    printf("Test WV_010: "); fflush(stdout);
    alarm(2);
    mem[0]=2; mem[1]=2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (2 << 1) | (1 << 16);  /* type=2, index=1 */
    job[8] = (uint32_t)(mem[1] + 0x200);  /* target */
    job[9] = 0;
    job[10] = 6;  /* immediate32 */
    job[12] = 0xCAFEBABE;
    
    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t*)cpu + 0x200);
    *marker = 0xDEADBEEF;
    
    a.jc = mem[1] + 0x100;
    a.core_req = 0x010;
    r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("submit=%d ", r);
    if (alarm_triggered) { printf("HANG\n"); alarm_triggered=0; }
    else { printf("marker=0x%08x\n", *marker); }
    alarm(0);
    munmap(cpu, 8192);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}