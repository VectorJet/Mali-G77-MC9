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

int test_core_req(int fd, uint32_t core_req, const char *name) {
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    memset(cpu, 0, 8192);

    volatile uint32_t *marker = (volatile uint32_t *)((uint8_t *)cpu + 0x30);
    *marker = 0xDEADBEEF;

    uint32_t *job = (uint32_t *)cpu;
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x30);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;

    struct kbase_atom atom = {0};
    atom.jc = mem[1];
    atom.core_req = core_req;
    atom.atom_number = 1;
    atom.prio = 0;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    read(fd, (uint8_t[24]){0}, 24);

    usleep(100000);

    int success = (*marker == 0xCAFEBABE);
    printf("%-20s (0x%03x): %s\n", name, core_req, success ? "OK" : "FAIL");

    munmap(cpu, 8192);
    return success;
}

int main(void) {
    printf("=== Core Req Scan ===\n\n");
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    printf("Testing core_req values:\n");
    test_core_req(fd, 0x001, "FS");
    test_core_req(fd, 0x002, "CS");
    test_core_req(fd, 0x004, "T");
    test_core_req(fd, 0x008, "V");
    test_core_req(fd, 0x010, "CF");
    test_core_req(fd, 0x003, "FS+CS");
    test_core_req(fd, 0x005, "FS+T");
    test_core_req(fd, 0x009, "FS+V");
    test_core_req(fd, 0x011, "FS+CF");
    test_core_req(fd, 0x006, "CS+T");
    test_core_req(fd, 0x00A, "CS+V");
    test_core_req(fd, 0x012, "CS+CF");
    test_core_req(fd, 0x203, "CS+CF (Chrome)");
    test_core_req(fd, 0x007, "FS+CS+T");
    test_core_req(fd, 0x003, "FS+CS");
    test_core_req(fd, 0x004, "T");
    test_core_req(fd, 0x100, "JOB_CHAIN");
    test_core_req(fd, 0x200, "PREVENT_RETRY");
    test_core_req(fd, 0x400, "NEVER_RETRY");

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}