#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

struct submit_args {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Test 1: next_job_ptr=0 explicit ===\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[6] = 0;
        job[8] = (uint32_t)(mem[1] + 0x100);
        job[9] = 0;
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 8192);
    }
    
    printf("=== Test 2: Job type 3 (CS) ===\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (3 << 1) | (1 << 16);  /* CS not WRITE_VALUE */
        job[8] = 0;
        job[9] = 0;
        job[12] = 1;  /* group count */
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x002, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 8192);
    }
    
    printf("=== Test 3: core_req=0 (DEP) ===\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x100);
        job[9] = 0;
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 8192);
    }
    
    printf("=== Test 4: 8 pages allocation ===\n");
    {
        uint64_t mem[4] = {8, 8, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 32768, 3, 1, fd, mem[1]);
        memset(cpu, 0, 32768);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x200);
        job[9] = 0;
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x200);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 32768);
    }
    
    printf("=== Test 5: Use multiple atoms (stride 72) ===\n");
    {
        uint64_t mem[4] = {4, 4, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 16384, 3, 1, fd, mem[1]);
        memset(cpu, 0, 16384);
        
        /* First job - WRITE_VALUE */
        uint32_t *job1 = (uint32_t *)((char*)cpu + 0x100);
        job1[4] = (2 << 1) | (1 << 16);
        job1[8] = (uint32_t)(mem[1] + 0x300);
        job1[9] = 0;
        job1[10] = 6;
        job1[12] = 0x11111111;
        
        /* Second job - same but chained */
        uint32_t *job2 = (uint32_t *)((char*)cpu + 0x200);
        job2[4] = (2 << 1) | (2 << 16);  /* index=2 */
        job2[8] = (uint32_t)(mem[1] + 0x400);
        job2[9] = 0;
        job2[10] = 6;
        job2[12] = 0x22222222;
        
        /* First job points to second */
        job1[8] = (uint32_t)(mem[1] + 0x200);  /* next job */
        job1[9] = 0;
        
        volatile uint32_t *marker1 = (volatile uint32_t *)((char*)cpu + 0x300);
        volatile uint32_t *marker2 = (volatile uint32_t *)((char*)cpu + 0x400);
        *marker1 = 0xAAAAAAAA;
        *marker2 = 0xAAAAAAAA;
        
        /* Atom points to first job */
        struct kbase_atom atom = {.jc = mem[1] + 0x100, .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker1=0x%08x, marker2=0x%08x\n", *marker1, *marker2);
        munmap(cpu, 16384);
    }
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}