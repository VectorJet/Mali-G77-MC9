#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

/*
 * Test: Job at different offsets within first page
 */
int main(void) {
    printf("=== Test: Job at various offsets ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Test each offset */
    int offsets[] = {0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0xC0, 0x100};
    int n_offsets = sizeof(offsets)/sizeof(offsets[0]);

    for (int i = 0; i < n_offsets; i++) {
        int offset = offsets[i];
        
        uint64_t mem[4] = {2, 2, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
        uint64_t gpu_va = mem[1];

        memset(cpu, 0, 8192);

        volatile uint32_t *t1 = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
        *t1 = 0xAAAAAAAA;

        uint32_t *j1 = (uint32_t *)((uint8_t*)cpu + offset);
        j1[4] = (2 << 1) | (1 << 16);
        j1[8] = (uint32_t)(gpu_va + 0x30);
        j1[10] = 6;
        j1[12] = 0x11111111;

        struct kbase_atom atom = {0};
        atom.jc = gpu_va + offset;
        atom.core_req = 0x203;
        atom.atom_number = 1;

        struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
            .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
        };
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);

        usleep(200000);

        printf("Offset 0x%02x: jc=0x%llx -> %s\n", 
               offset, (unsigned long long)(gpu_va + offset),
               (*t1 == 0x11111111) ? "OK" : "FAIL");

        read(fd, (uint8_t[24]){0}, 24);
        munmap(cpu, 8192);
    }

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}