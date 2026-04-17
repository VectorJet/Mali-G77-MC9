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
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t  jit_id[2];
    struct base_dependency pre_dep[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;
    uint32_t pad2;
} __attribute__((packed));

#define NUM_ATOMS 5

int main(void) {
    printf("=== Test: %d Atoms Single Submission (Real HW) ===\n", NUM_ATOMS);
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* SAME_VA (0x2000) + CPU/GPU RW (0xF) */
    uint64_t mem[4] = {1, 1, 0, 0x200F};
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) != 0) {
        perror("MEM_ALLOC");
        return 1;
    }
    
    void *cpu = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    memset(cpu, 0, 4096);
    uint64_t gva = (uint64_t)cpu; // CPU pointer IS GPU pointer

    /* Set up 5 targets and 5 job descriptors */
    volatile uint32_t *targets[NUM_ATOMS];
    uint32_t expected_values[NUM_ATOMS] = {
        0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555
    };

    struct kbase_atom_mtk atoms[NUM_ATOMS];
    memset(atoms, 0, sizeof(atoms));

    for (int i = 0; i < NUM_ATOMS; i++) {
        targets[i] = (volatile uint32_t *)((uint8_t*)cpu + 0x800 + (i * 0x10));
        *targets[i] = 0xDEADBEEF; // initialize

        uint32_t *job = (uint32_t *)((uint8_t*)cpu + (i * 128)); // 128-byte aligned descriptors
        job[4]  = (2 << 1) | (1 << 16); // Type 2 = WRITE_VALUE
        uint64_t target_addr = gva + 0x800 + (i * 0x10);
        job[8]  = (uint32_t)(target_addr & 0xFFFFFFFF);
        job[9]  = (uint32_t)(target_addr >> 32);
        job[10] = 6; // Immediate32
        job[12] = expected_values[i];

        atoms[i].jc = gva + (i * 128);
        atoms[i].core_req = 0x4a; // CS + CF + COH
        atoms[i].atom_number = i + 1;
        atoms[i].jobslot = 1;
        atoms[i].frame_nr = 1;

        if (i > 0) {
            /* Create a dependency chain: atom i depends on atom i-1 */
            atoms[i].pre_dep[0].atom_id = i; 
            atoms[i].pre_dep[0].dep_type = 1; // DATA dependency
        }
    }

    msync(cpu, 4096, MS_SYNC); // Flush CPU cache (paranoid)

    struct { uint64_t addr; uint32_t nr, stride; } submit = { 
        (uint64_t)atoms, NUM_ATOMS, sizeof(struct kbase_atom_mtk) 
    };

    printf("Submitting %d atoms with stride %zu...\n", NUM_ATOMS, sizeof(struct kbase_atom_mtk));
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret != 0) {
        printf("JOB_SUBMIT failed: %d (%s)\n", errno, strerror(errno));
        return 1;
    }

    /* Wait and drain events */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    uint8_t ev[24] = {0};
    int attempts = 40, count = 0;
    while (attempts--) {
        if (read(fd, ev, 24) == 24) {
            uint32_t code = *(uint32_t*)ev;
            uint8_t atom_num = ev[4];
            printf("Got event: code=0x%02x atom=%d\n", code, atom_num);
            if (code != 0x01) {
                printf("  -> FAULT OR ERROR!\n");
            }
            if (++count == NUM_ATOMS) break;
        }
        usleep(50000);
    }
    
    printf("\nResults:\n");
    int success = 1;
    for (int i = 0; i < NUM_ATOMS; i++) {
        printf("Target %d: 0x%08x (expected 0x%08x)\n", i+1, *targets[i], expected_values[i]);
        if (*targets[i] != expected_values[i]) success = 0;
    }

    if (success) {
        printf("\n*** EXCELLENT! %d-atom batch strictly executed on hardware! ***\n", NUM_ATOMS);
    } else {
        printf("\n*** FAILED. Some targets were not written. ***\n");
    }

    munmap(cpu, 4096);
    close(fd);
    return 0;
}
