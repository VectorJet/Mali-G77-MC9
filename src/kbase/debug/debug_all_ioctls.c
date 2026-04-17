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
#define KBASE_IOCTL_GET_GPUPROPS   _IOC(_IOC_WRITE, 0x80, 3, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_MEM_QUERY      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 6, 32)
#define KBASE_IOCTL_MEM_FREE       _IOC(_IOC_WRITE, 0x80, 7, 8)
#define KBASE_IOCTL_HWCNT_SETUP    _IOC(_IOC_WRITE, 0x80, 8, 16)
#define KBASE_IOCTL_MEM_JIT_INIT   _IOC(_IOC_WRITE, 0x80, 14, 16)
#define KBASE_IOCTL_MEM_SYNC       _IOC(_IOC_WRITE, 0x80, 15, 16)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int test_ioctl(int fd, int nr, void *arg, const char *name) {
    int ret = ioctl(fd, nr, arg);
    printf("  ioctl 0x%02x (%s): ret=%d\n", nr & 0xFF, name, ret);
    return ret;
}

int main(void) {
    printf("=== Debug: Try ALL r49 ioctls before 3-atom submit ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Try ioctl 0x19 (MEM_JIT_INIT) */
    printf("Testing ioctl 0x19 (MEM_JIT_INIT)...\n");
    struct { uint32_t id, val; } jit = {0, 0};
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x19, 8), &jit, "MEM_JIT_INIT");

    /* Try ioctl 0x1A */
    printf("Testing ioctl 0x1A...\n");
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1A, 4), &(uint32_t){0}, "unknown");

    /* Try ioctl 0x1C */
    printf("Testing ioctl 0x1C...\n");
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1C, 4), &(uint32_t){0}, "unknown");

    /* Try ioctl 0x1D */
    printf("Testing ioctl 0x1D...\n");
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1D, 4), &(uint32_t){0}, "unknown");

    /* Try ioctl 0x1E */
    printf("Testing ioctl 0x1E...\n");
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1E, 4), &(uint32_t){0}, "unknown");

    /* Try ioctl 0x1F */
    printf("Testing ioctl 0x1F...\n");
    test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x1F, 4), &(uint32_t){0}, "unknown");

    /* Try multiple 0x19 values like Chrome */
    printf("\nTrying multiple 0x19 values...\n");
    uint32_t vals[] = {0x100, 0x108, 0x110, 0x120, 0x180, 0x200};
    for (int i = 0; i < 6; i++) {
        test_ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x19, 4), &vals[i], "0x19");
    }

    /* Now try 3 atoms */
    printf("\n=== Now trying 3 atoms ===\n");
    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;
    *(uint32_t*)((uint8_t*)cpu + 0x600) = 1;

    struct kbase_atom atoms[3] = {0};
    atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1;
    atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 1;
    atoms[2].jc = gva + 0x400; atoms[2].core_req = 0x004; atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1; atoms[2].pre_dep_type[0] = 1;

    printf("Submitting 3 atoms...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    sleep(2);
    printf("poly0=0x%08x poly1=0x%08x poly2=0x%08x\n",
           *(uint32_t*)((uint8_t*)cpu+0x200),
           *(uint32_t*)((uint8_t*)cpu+0x400),
           *(uint32_t*)((uint8_t*)cpu+0x600));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}