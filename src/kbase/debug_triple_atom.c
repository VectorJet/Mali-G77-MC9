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
 * Test: Multi-job using multi-atom (like Chrome)
 * 
 * Chrome uses SEPARATE jc values for each atom - each points to
 * a different GPU VA. This is NOT job chaining via next_job_ptr.
 */
int main(void) {
    printf("=== Test: Multi-Job via Multi-Atom (Chrome Style) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* THREE separate allocations like Chrome */
    uint64_t mem1[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);

    uint64_t mem2[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);

    uint64_t mem3[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem3);
    void *buf3 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem3[1]);

    printf("Buffer1: 0x%llx\n", (unsigned long long)mem1[1]);
    printf("Buffer2: 0x%llx\n", (unsigned long long)mem2[1]);
    printf("Buffer3: 0x%llx\n", (unsigned long long)mem3[1]);

    memset(buf1, 0, 8192);
    memset(buf2, 0, 8192);
    memset(buf3, 0, 8192);

    /* Target in each buffer */
    volatile uint32_t *t1 = (volatile uint32_t *)((uint8_t*)buf1 + 0x30);
    volatile uint32_t *t2 = (volatile uint32_t *)((uint8_t*)buf2 + 0x30);
    volatile uint32_t *t3 = (volatile uint32_t *)((uint8_t*)buf3 + 0x30);
    *t1 = 0xAAAAAAAA;
    *t2 = 0xBBBBBBBB;
    *t3 = 0xCCCCCCCC;

    /* Job1 in buffer1 */
    uint32_t *j1 = (uint32_t *)buf1;
    j1[4] = (2 << 1) | (1 << 16);
    j1[8] = (uint32_t)(mem1[1] + 0x30);
    j1[10] = 6;
    j1[12] = 0x11111111;

    /* Job2 in buffer2 */
    uint32_t *j2 = (uint32_t *)buf2;
    j2[4] = (2 << 1) | (2 << 16);
    j2[8] = (uint32_t)(mem2[1] + 0x30);
    j2[10] = 6;
    j2[12] = 0x22222222;

    /* Job3 in buffer3 */
    uint32_t *j3 = (uint32_t *)buf3;
    j3[4] = (2 << 1) | (3 << 16);
    j3[8] = (uint32_t)(mem3[1] + 0x30);
    j3[10] = 6;
    j3[12] = 0x33333333;

    /* Three atoms - like Chrome */
    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = mem1[1];  /* buffer1 */
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;
    atoms[0].seq_nr = 0;

    atoms[1].jc = mem2[1];  /* buffer2 */
    atoms[1].core_req = 0x203;
    atoms[1].atom_number = 2;
    atoms[1].seq_nr = 0;

    atoms[2].jc = mem3[1];  /* buffer3 */
    atoms[2].core_req = 0x203;
    atoms[2].atom_number = 3;
    atoms[2].seq_nr = 0;

    printf("\n3 atoms, each with separate jc\n");

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atoms, .nr_atoms = 3, .stride = 72
    };
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);

    usleep(500000);

    printf("\nResult:\n");
    printf("  t1: 0x%08x (expected 0x11111111) - %s\n", *t1, (*t1 == 0x11111111) ? "OK" : "FAIL");
    printf("  t2: 0x%08x (expected 0x22222222) - %s\n", *t2, (*t2 == 0x22222222) ? "OK" : "FAIL");
    printf("  t3: 0x%08x (expected 0x33333333) - %s\n", *t3, (*t3 == 0x33333333) ? "OK" : "FAIL");

    if (*t1 == 0x11111111 && *t2 == 0x22222222 && *t3 == 0x33333333) {
        printf("\n*** SUCCESS: All 3 jobs executed! ***\n");
    }

    /* Drain events */
    uint8_t ev[24];
    int count = 0;
    while (read(fd, ev, sizeof(ev)) > 0) {
        printf("Event[%d]: 0x%08x\n", count++, *(uint32_t*)ev);
    }

    munmap(buf1, 8192);
    munmap(buf2, 8192);
    munmap(buf3, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}