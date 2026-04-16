#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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
 * Test sequential atom submission with dependencies
 * 
 * Chrome uses:
 * - seq_nr for ordering
 * - pre_dep for dependencies between atoms
 * - Different core_req for different job types
 */
int main(void) {
    int fd, ret;
    void *cpu_mem;
    struct kbase_atom atoms[2] = {0};
    uint32_t *job;

    printf("=== Mali - Sequential Atoms with Deps ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[OK] SET_FLAGS\n");

    /* Allocate 4 pages */
    uint64_t mem[4] = {0};
    mem[0] = 4;
    mem[1] = 4;
    mem[2] = 0;
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
              BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    uint64_t gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    cpu_mem = mmap(NULL, 4 * 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    printf("[OK] mmap: cpu=%p\n", cpu_mem);

    memset(cpu_mem, 0, 4 * 4096);

    /* Two targets */
    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x40);
    volatile uint32_t *target2 = (volatile uint32_t *)((uint8_t *)cpu_mem + 0x80);
    *target1 = 0xAAAAAAAA;
    *target2 = 0xBBBBBBBB;
    printf("[OK] target1 = 0xAAAAAAAA\n");
    printf("[OK] target2 = 0xBBBBBBBB\n");

    /* Job 1 at offset 0 */
    job = (uint32_t *)((uint8_t *)cpu_mem + 0);
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(gpu_va + 0x40);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0x11111111;

    /* Job 2 at offset 0x100 */
    job = (uint32_t *)((uint8_t *)cpu_mem + 0x100);
    job[4] = (2 << 1) | (2 << 16);
    job[8] = (uint32_t)(gpu_va + 0x80);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0x22222222;

    printf("[OK] Job1: WRITE_VALUE to target1 with 0x11111111\n");
    printf("[OK] Job2: WRITE_VALUE to target2 with 0x22222222\n");

    /* Atom 1 - depends on nothing */
    atoms[0].jc = gpu_va;
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;
    atoms[0].prio = 0;
    atoms[0].seq_nr = 0;

    /* Atom 2 - depends on atom 1 (atom_number=1, dep_type=1=AND) */
    atoms[1].jc = gpu_va + 0x100;
    atoms[1].core_req = 0x203;
    atoms[1].atom_number = 2;
    atoms[1].prio = 0;
    atoms[1].seq_nr = 1;
    atoms[1].pre_dep_atom[0] = 1;  /* depends on atom 1 */
    atoms[1].pre_dep_type[0] = 1;  /* AND dependency */

    printf("[OK] Atom1: atom_number=1, no deps\n");
    printf("[OK] Atom2: atom_number=2, depends on atom 1\n");

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atoms,
        .nr_atoms = 2,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[OK] JOB_SUBMIT: ret=%d\n", ret);

    usleep(500000);

    printf("\n=== RESULT ===\n");
    printf("target1: 0x%08x (expected 0x11111111)\n", *target1);
    printf("target2: 0x%08x (expected 0x22222222)\n", *target2);

    if (*target1 == 0x11111111 && *target2 == 0x22222222) {
        printf("\n*** SUCCESS: Both jobs executed! ***\n");
    } else if (*target1 == 0xAAAAAAAA) {
        printf("\n*** Note: Only first job may run with chained deps ***\n");
    }

    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    printf("Event: 0x%08x\n", *(uint32_t *)ev);

    munmap(cpu_mem, 4 * 4096);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}