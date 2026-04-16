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
void alarm_handler(int sig) { alarm_triggered = 1; }

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_GPU_RDWR 0xF

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int test(int fd, const char *name, uint32_t core_req) {
    printf("%s (0x%03x): ", name, core_req); fflush(stdout);
    alarm(2);
    
    uint64_t mem[4] = {2, 2, 0, BASE_MEM_PROT_GPU_RDWR};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 8192);
    
    /* Use DEP job (type=0) - it's a no-op but tests core_req handling */
    uint32_t *job = (uint32_t *)((uint8_t*)cpu + 0x100);
    job[4] = (0 << 1) | (1 << 16);  /* type=0, index=1 */
    
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
        printf("OK\n");
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
    
    printf("=== core_req Scan with DEP job ===\n");
    
    test(fd, "0x000", 0x000);
    test(fd, "0x001", 0x001);
    test(fd, "0x002", 0x002);
    test(fd, "0x003", 0x003);
    test(fd, "0x004", 0x004);
    test(fd, "0x008", 0x008);
    test(fd, "0x010", 0x010);
    test(fd, "0x020", 0x020);
    test(fd, "0x040", 0x040);
    test(fd, "0x080", 0x080);
    test(fd, "0x100", 0x100);
    test(fd, "0x200", 0x200);
    test(fd, "0x201", 0x201);
    test(fd, "0x202", 0x202);
    test(fd, "0x203", 0x203);
    test(fd, "0x204", 0x204);
    test(fd, "0x208", 0x208);
    test(fd, "0x209", 0x209);
    test(fd, "0x20A", 0x20A);
    test(fd, "0x210", 0x210);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}