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
    
    printf("=== Test with sleep between submit and read ===\n");
    
    /* Test: submit job then wait before reading event */
    printf("Test 1: immediate read\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x100);
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
    
    printf("Test 2: 1 second sleep before read\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x100);
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        sleep(1);  /* Wait for GPU to potentially process */
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 8192);
    }
    
    printf("Test 3: 2 second sleep\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x100);
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        sleep(2);
        read(fd, (uint8_t[24]){0}, 24);
        printf("  marker=0x%08x\n", *marker);
        munmap(cpu, 8192);
    }
    
    printf("Test 4: Non-blocking read\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x100);
        job[10] = 6;
        job[12] = 0x11111111;
        
        volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
        *marker = 0xDEADBEEF;
        
        /* Set non-blocking */
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        
        struct kbase_atom atom = {.jc = mem[1], .core_req = 0x010, .atom_number = 1};
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        
        /* Try non-blocking read */
        uint8_t ev[24];
        int n = read(fd, ev, sizeof(ev));
        printf("  read returned %d, event[0]=0x%02x\n", n, n>0?ev[0]:0);
        printf("  marker=0x%08x\n", *marker);
        
        /* Restore blocking */
        fcntl(fd, F_SETFL, flags);
        munmap(cpu, 8192);
    }
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}