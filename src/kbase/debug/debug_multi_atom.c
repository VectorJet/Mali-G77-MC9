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

void dump_atom(struct kbase_atom *a, const char *name) {
    printf("  %s: jc=0x%llx core_req=0x%x atom_number=%d seq_nr=%llu\n",
           name, (unsigned long long)a->jc, a->core_req, 
           a->atom_number, (unsigned long long)a->seq_nr);
    printf("       pre_dep[0]=atom %d type %d, pre_dep[1]=atom %d type %d\n",
           a->pre_dep_atom[0], a->pre_dep_type[0],
           a->pre_dep_atom[1], a->pre_dep_type[1]);
}

/*
 * Debug: Try Chrome-like multi-atom submit
 */
int main(void) {
    printf("=== Debug Multi-Atom Submit ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[OK] Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    printf("[OK] SET_FLAGS\n");

    /* Allocate TWO separate buffers like Chrome does */
    uint64_t mem1[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);
    printf("[OK] Buffer1: gpu_va=0x%llx\n", (unsigned long long)mem1[1]);

    uint64_t mem2[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);
    printf("[OK] Buffer2: gpu_va=0x%llx\n", (unsigned long long)mem2[1]);

    /* Set up targets */
    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t *)buf1 + 0x30);
    volatile uint32_t *target2 = (volatile uint32_t *)((uint8_t *)buf2 + 0x30);
    *target1 = 0xAAAAAAAA;
    *target2 = 0xBBBBBBBB;
    printf("[OK] Target1 = 0xAAAAAAAA, Target2 = 0xBBBBBBBB\n\n");

    /* Build job1 in buffer1 */
    uint32_t *job1 = (uint32_t *)buf1;
    job1[4] = (2 << 1) | (1 << 16);
    job1[8] = (uint32_t)(mem1[1] + 0x30);
    job1[9] = 0;
    job1[10] = 6;
    job1[12] = 0x11111111;

    /* Build job2 in buffer2 */
    uint32_t *job2 = (uint32_t *)buf2;
    job2[4] = (2 << 1) | (2 << 16);
    job2[8] = (uint32_t)(mem2[1] + 0x30);
    job2[9] = 0;
    job2[10] = 6;
    job2[12] = 0x22222222;

    /* Chrome-like atoms: different jc, different core_req */
    struct kbase_atom atoms[2] = {0};

    atoms[0].jc = mem1[1];  /* buffer1 */
    atoms[0].core_req = 0x209;  /* CS + V like Chrome atom 0 */
    atoms[0].atom_number = 1;
    atoms[0].prio = 0;
    atoms[0].seq_nr = 0;

    atoms[1].jc = mem2[1];  /* buffer2 */
    atoms[1].core_req = 0x20a;  /* V + CF like Chrome atom 3 */
    atoms[1].atom_number = 2;
    atoms[1].prio = 0;
    atoms[1].seq_nr = 0;

    printf("Submitting 2 atoms:\n");
    dump_atom(&atoms[0], "Atom0");
    dump_atom(&atoms[1], "Atom1");

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atoms,
        .nr_atoms = 2,
        .stride = 72
    };
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("\n[OK] JOB_SUBMIT: ret=%d\n", ret);

    usleep(500000);

    printf("\n=== RESULT ===\n");
    printf("Target1: 0x%08x (expected 0x11111111)\n", *target1);
    printf("Target2: 0x%08x (expected 0x22222222)\n", *target2);

    if (*target1 == 0x11111111 && *target2 == 0x22222222) {
        printf("\n*** SUCCESS: Both atoms executed! ***\n");
    } else if (*target1 == 0x11111111) {
        printf("\n*** Only first atom ran ***\n");
    } else {
        printf("\n*** Neither ran ***\n");
    }

    /* Drain events */
    uint8_t ev[24] = {0};
    int count = 0;
    while (read(fd, ev, sizeof(ev)) > 0) {
        printf("Event[%d]: 0x%08x\n", count++, *(uint32_t *)ev);
    }

    munmap(buf1, 8192);
    munmap(buf2, 8192);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}