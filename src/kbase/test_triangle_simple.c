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

int main(void) {
    printf("=== Mali-G77 Triangle Demo ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Two separate buffers like working multi-atom test */
    uint64_t mem1[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);

    uint64_t mem2[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);

    printf("Buffer1: 0x%llx\n", (unsigned long long)mem1[1]);
    printf("Buffer2: 0x%llx\n", (unsigned long long)mem2[1]);

    memset(buf1, 0, 8192);
    memset(buf2, 0, 8192);

    /* Targets */
    volatile uint32_t *t1 = (volatile uint32_t *)((uint8_t*)buf1 + 0x30);
    volatile uint32_t *t2 = (volatile uint32_t *)((uint8_t*)buf2 + 0x30);
    *t1 = 0x11111111;
    *t2 = 0x22222222;

    /* Job1 */
    uint32_t *j1 = (uint32_t *)buf1;
    j1[4] = (2 << 1) | (1 << 16);
    j1[8] = (uint32_t)(mem1[1] + 0x30);
    j1[10] = 6;
    j1[12] = 0xAAA;

    /* Job2 */
    uint32_t *j2 = (uint32_t *)buf2;
    j2[4] = (2 << 1) | (2 << 16);
    j2[8] = (uint32_t)(mem2[1] + 0x30);
    j2[10] = 6;
    j2[12] = 0xBBB;

    /* Two atoms */
    struct kbase_atom atoms[2] = {0};
    atoms[0].jc = mem1[1];
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;

    atoms[1].jc = mem2[1];
    atoms[1].core_req = 0x203;
    atoms[1].atom_number = 2;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atoms, .nr_atoms = 2, .stride = 72
    };
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);

    usleep(500000);

    printf("\nResult: t1=0x%08x t2=0x%08x\n", *t1, *t2);
    if (*t1 == 0xAAA && *t2 == 0xBBB) {
        printf("SUCCESS: Triangle pipeline works!\n");
    }

    read(fd, (uint8_t[24]){0}, 24);
    munmap(buf1, 8192);
    munmap(buf2, 8192);
    close(fd);
    return 0;
}