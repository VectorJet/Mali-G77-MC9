#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

struct base_dependency {
    uint8_t atom_id;
    uint8_t dep_type;
} __attribute__((packed));

struct kbase_atom_mtk {
    uint64_t seq_nr;          /* +0 */
    uint64_t jc;              /* +8 */
    uint64_t udata[2];        /* +16 */
    uint64_t extres_list;     /* +32 */
    uint16_t nr_extres;       /* +40 */
    uint8_t  jit_id[2];       /* +42 */
    struct base_dependency pre_dep[2]; /* +44 */
    uint8_t  atom_number;     /* +48 */
    uint8_t  prio;            /* +49 */
    uint8_t  device_nr;       /* +50 */
    uint8_t  jobslot;         /* +51 */
    uint32_t core_req;        /* +52 */
    uint8_t  renderpass_id;   /* +56 */
    uint8_t  padding[7];      /* +57 */
    uint32_t frame_nr;        /* +64 */
    uint32_t pad2;            /* +68 */
} __attribute__((packed));

int main(void) {
    printf("=== Test: 3 Atoms Single Submission with Correct Struct ===\n\n");
    printf("sizeof(struct kbase_atom_mtk) = %zu (expected 72)\n", sizeof(struct kbase_atom_mtk));

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    volatile uint32_t *target2 = (volatile uint32_t *)((uint8_t*)cpu + 0x300);
    volatile uint32_t *target3 = (volatile uint32_t *)((uint8_t*)cpu + 0x500);
    *target1 = 0xAAAAAAAA;
    *target2 = 0xBBBBBBBB;
    *target3 = 0xCCCCCCCC;

    /* Create 3 WRITE_VALUE job descriptors */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x000);
    job1[4] = (2 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x100);
    job1[12] = 0x11111111;

    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job2[4] = (2 << 1) | (1 << 16);
    job2[8] = (uint32_t)(gva + 0x300);
    job2[12] = 0x22222222;

    uint32_t *job3 = (uint32_t *)((uint8_t*)cpu + 0x400);
    job3[4] = (2 << 1) | (1 << 16);
    job3[8] = (uint32_t)(gva + 0x500);
    job3[12] = 0x33333333;

    /* Build 3 atoms sequentially in array */
    struct kbase_atom_mtk atoms[3] = {0};
    
    atoms[0].jc = gva + 0x000;
    atoms[0].core_req = 0x002; /* WRITE_VALUE */
    atoms[0].atom_number = 1;
    atoms[0].jobslot = 0;
    atoms[0].frame_nr = 1;

    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x002;
    atoms[1].atom_number = 2;
    atoms[1].jobslot = 0;
    atoms[1].pre_dep[0].atom_id = 1;  /* Depend on atom 1 */
    atoms[1].pre_dep[0].dep_type = 1; /* DATA dependency */
    atoms[1].frame_nr = 1;

    atoms[2].jc = gva + 0x400;
    atoms[2].core_req = 0x002;
    atoms[2].atom_number = 3;
    atoms[2].jobslot = 0;
    atoms[2].pre_dep[0].atom_id = 2;  /* Depend on atom 2 */
    atoms[2].pre_dep[0].dep_type = 1; /* DATA dependency */
    atoms[2].frame_nr = 1;

    struct { uint64_t addr; uint32_t nr, stride; } submit;
    submit.addr = (uint64_t)atoms;
    submit.nr = 3;
    submit.stride = sizeof(struct kbase_atom_mtk); /* 72 */

    printf("Submitting 3 atoms in one ioctl...\n");
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret != 0) {
        printf("Submit failed: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("Submit accepted.\n");
    }

    /* Wait and drain events */
    usleep(100000);
    
    uint8_t ev[24] = {0};
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    while (read(fd, ev, 24) == 24) {
        printf("Got event: event_code=0x%02x atom=%d\n", *(uint32_t*)ev, ev[4]);
    }

    printf("\nResults:\n");
    printf("target1: 0x%08x\n", *target1);
    printf("target2: 0x%08x\n", *target2);
    printf("target3: 0x%08x\n", *target3);

    if (*target3 == 0x33333333) {
        printf("\n*** SUCCESS! Multi-atom in single submit WORKS! ***\n");
    } else {
        printf("\n*** FAILED! GPU did not execute all atoms! ***\n");
    }

    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}
