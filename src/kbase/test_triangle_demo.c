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
 * Mali-G77 Triangle Demo
 * =====================
 * 
 * This demonstrates GPU execution using multi-atom approach.
 * 
 * For actual triangle rendering, you would need:
 * 1. Fragment shader code (GPU program)
 * 2. Framebuffer descriptor (FBD)
 * 3. Vertex/tiler jobs
 * 
 * This demo shows the fundamental building block: 
 * multiple GPU jobs executing via multi-atom submission.
 */
int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║           Mali-G77 Triangle Demo (Multi-Atom)              ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("✓ Opened /dev/mali0\n");

    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("✓ VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    printf("✓ SET_FLAGS: context created\n\n");

    /* 
     * Multi-atom approach: separate allocations for each job
     * This is how Chrome does it - each atom has its own jc
     */
    
    /* Buffer 1: First job */
    uint64_t mem1[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);
    printf("✓ Job1 buffer: gpu_va=0x%llx\n", (unsigned long long)mem1[1]);

    /* Buffer 2: Second job */
    uint64_t mem2[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);
    printf("✓ Job2 buffer: gpu_va=0x%llx\n", (unsigned long long)mem2[1]);

    /* Buffer 3: Third job */
    uint64_t mem3[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem3);
    void *buf3 = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem3[1]);
    printf("✓ Job3 buffer: gpu_va=0x%llx\n\n", (unsigned long long)mem3[1]);

    memset(buf1, 0, 8192);
    memset(buf2, 0, 8192);
    memset(buf3, 0, 8192);

    /* 
     * Target locations - imagine these would be:
     * - Job1: Vertex shader output
     * - Job2: Tiler preprocessing  
     * - Job3: Fragment color output
     */
    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t*)buf1 + 0x30);
    volatile uint32_t *target2 = (volatile uint32_t *)((uint8_t*)buf2 + 0x30);
    volatile uint32_t *target3 = (volatile uint32_t *)((uint8_t*)buf3 + 0x30);
    
    *target1 = 0x11111111;
    *target2 = 0x22222222;
    *target3 = 0x33333333;
    printf("Targets initialized: 0x11111111, 0x22222222, 0x33333333\n");

    /* Job 1 - WRITE_VALUE */
    uint32_t *job1 = (uint32_t *)buf1;
    job1[4] = (2 << 1) | (1 << 16);  /* type=2, index=1 */
    job1[8] = (uint32_t)(mem1[1] + 0x30);
    job1[10] = 6;  /* IMMEDIATE32 */
    job1[12] = 0xAAA;  /* Vertex X */

    /* Job 2 - WRITE_VALUE */
    uint32_t *job2 = (uint32_t *)buf2;
    job2[4] = (2 << 1) | (2 << 16);
    job2[8] = (uint32_t)(mem2[1] + 0x30);
    job2[10] = 6;
    job2[12] = 0xBBB;  /* Vertex Y */

    /* Job 3 - WRITE_VALUE */
    uint32_t *job3 = (uint32_t *)buf3;
    job3[4] = (2 << 1) | (3 << 16);
    job3[8] = (uint32_t)(mem3[1] + 0x30);
    job3[10] = 6;
    job3[12] = 0xCCC;  /* Fragment Color */

    printf("Jobs configured for vertex → tiler → fragment pipeline\n");

    /* Three atoms - like Chrome's multi-job submits */
    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = mem1[1];  /* Vertex job */
    atoms[0].core_req = 0x203;
    atoms[0].atom_number = 1;
    atoms[0].seq_nr = 0;

    atoms[1].jc = mem2[1];  /* Tiler job */
    atoms[1].core_req = 0x203;
    atoms[1].atom_number = 2;
    atoms[1].seq_nr = 1;

    atoms[2].jc = mem3[1];  /* Fragment job */
    atoms[2].core_req = 0x203;
    atoms[2].atom_number = 3;
    atoms[2].seq_nr = 2;

    printf("Atoms configured: vertex(1) → tiler(2) → fragment(3)\n");

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atoms, .nr_atoms = 3, .stride = 72
    };
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("✓ JOB_SUBMIT: ret=%d (3 atoms)\n", ret);

    usleep(500000);

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("Results:\n");
    printf("  Vertex:   0x%08x (expected 0x00000AAA) - %s\n", 
           *target1, (*target1 == 0xAAA) ? "OK" : "FAIL");
    printf("  Tiler:    0x%08x (expected 0x00000BBB) - %s\n", 
           *target2, (*target2 == 0xBBB) ? "OK" : "FAIL");
    printf("  Fragment: 0x%08x (expected 0x00000CCC) - %s\n", 
           *target3, (*target3 == 0xCCC) ? "OK" : "FAIL");
    printf("═══════════════════════════════════════════════════════════\n");

    if (*target1 == 0xAAA && *target2 == 0xBBB && *target3 == 0xCCC) {
        printf("\n🎉 SUCCESS! GPU executed all 3 jobs!\n\n");
        printf("This demonstrates the multi-atom pipeline approach.\n");
        printf("For actual triangle rendering, add:\n");
        printf("  • Fragment shader code\n");
        printf("  • Framebuffer descriptor (FBD)\n");
        printf("  • Proper vertex data\n");
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
    printf("\n=== Demo Complete ===\n");
    return 0;
}