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

int test_n_atoms(int n) {
    printf("\n=== Testing %d atoms (no deps) ===\n", n);

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    /* Create n TILER jobs */
    struct kbase_atom atoms[4] = {0};
    for (int i = 0; i < n && i < 4; i++) {
        *(uint32_t*)((uint8_t*)cpu + 0x200 + i*0x200) = 1;
        atoms[i].jc = gva + i*0x200;
        atoms[i].core_req = 0x004;
        atoms[i].atom_number = i + 1;
    }

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = n, .stride = 72});
    printf("Submit %d atoms, ret=%d\n", n, ret);

    /* Wait and check */
    usleep(200000);
    printf("Done waiting\n");

    /* Check if any executed */
    int executed = 0;
    for (int i = 0; i < n && i < 4; i++) {
        uint32_t val = *(uint32_t*)((uint8_t*)cpu + 0x200 + i*0x200);
        printf("  atom %d: 0x%08x\n", i, val);
        if (val == 0) executed++;
    }

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 4*4096);
    close(fd);

    return executed;
}

int main(void) {
    printf("=== Atom Count Boundary Test ===\n");

    test_n_atoms(1);  /* should work */
    test_n_atoms(2);  /* should work */
    test_n_atoms(3);  /* ? */
    test_n_atoms(4);  /* ? */

    printf("\n=== Done ===\n");
    return 0;
}