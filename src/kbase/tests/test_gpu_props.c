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
#define KBASE_IOCTL_GET_GPU_props _IOC(_IOC_READ, 0x80, 0x12, 749)

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
    
    /* Try getting GPU props to see if there are any useful hints */
    printf("=== Try GET_GPU_PROPS ===\n");
    char props[1024];
    int ret = ioctl(fd, KBASE_IOCTL_GET_GPU_props, props);
    printf("GET_GPU_PROPS ret=%d\n", ret);
    if (ret > 0) {
        /* Print first few bytes as hex */
        printf("First 64 bytes: ");
        for (int i = 0; i < 64 && i < ret; i++) {
            printf("%02x ", (unsigned char)props[i]);
        }
        printf("\n");
        
        /* GPU ID is at offset 0 */
        printf("GPU ID: 0x%02x%02x%02x%02x\n", 
               (unsigned char)props[3], (unsigned char)props[2],
               (unsigned char)props[1], (unsigned char)props[0]);
    }
    
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Try using SAME_VA mode by mmap with offset 0 */
    printf("=== Try mmap with offset 0 (SAME_VA) ===\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        printf("gpu_va=0x%llx\n", (unsigned long long)mem[1]);
        
        /* Try mmap with offset 0 (SAME_VA mode) */
        void *cpu = mmap(NULL, 8192, 3, 1, fd, 0);  /* offset 0 */
        if (cpu == MAP_FAILED) {
            printf("mmap offset 0 failed\n");
        } else {
            printf("mmap offset 0 = %p\n", cpu);
            
            memset(cpu, 0, 8192);
            uint32_t *job = (uint32_t *)cpu;
            job[4] = (2 << 1) | (1 << 16);
            job[8] = 0x100;  /* target at offset 0x100 */
            job[10] = 6;
            job[12] = 0x11111111;
            
            volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x100);
            *marker = 0xDEADBEEF;
            
            struct kbase_atom atom = {.jc = 0, .core_req = 0x010, .atom_number = 1};  /* jc=0 means use current context */
            struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
            ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
            read(fd, (uint8_t[24]){0}, 24);
            printf("marker=0x%08x\n", *marker);
            munmap(cpu, 8192);
        }
    }
    
    /* Try jc=0 (no job chain, just a barrier or no-op) */
    printf("=== Try jc=0 ===\n");
    {
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
        memset(cpu, 0, 8192);
        
        struct kbase_atom atom = {.jc = 0, .core_req = 0, .atom_number = 1};  /* jc=0, core_req=0 = no-op */
        struct submit_args s = {.addr=(uint64_t)&atom, .nr_atoms=1, .stride=72};
        ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
        printf("jc=0 submit ret=%d\n", ret);
        read(fd, (uint8_t[24]){0}, 24);
        munmap(cpu, 8192);
    }
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}