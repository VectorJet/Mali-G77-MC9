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

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test: Different core_req values ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 4*4096);

    /* Test different core_req with TILER jobs */
    uint32_t test_cores[] = {0x004, 0x014, 0x024, 0x044, 0x084};
    const char *labels[] = {"0x004 (T)", "0x014 (T+?)", "0x024 (T+?)", "0x044 (T+?)", "0x084 (T+FC)"};

    for (int i = 0; i < 5; i++) {
        /* Reset memory */
        memset(cpu, 0, 4*4096);

        /* Job at 0 */
        uint32_t *job = (uint32_t *)cpu;
        job[4] = (4 << 1) | (1 << 16);
        job[8] = (uint32_t)(gva + 0x200);
        job[12] = 0;

        struct kbase_atom atom = {0};
        atom.jc = gva;
        atom.core_req = test_cores[i];
        atom.atom_number = 1;

        printf("Testing core_req=%s...\n", labels[i]);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atom, .nr = 1, .stride = 72});
        usleep(50000);
        read(fd, (uint8_t[24]){0}, 24);

        uint32_t result = *(uint32_t*)((uint8_t*)cpu + 0x200);
        printf("  poly at 0x200 = 0x%08x (%s)\n", result, result == 0 ? "MODIFIED" : "unchanged");
    }

    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}