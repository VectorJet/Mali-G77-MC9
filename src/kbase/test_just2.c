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
    printf("=== Test: How many atoms can we submit? ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Try 2 atoms - working case */
    printf("\n--- 2 atoms (should work) ---\n");
    {
        uint64_t mem[4] = {4, 4, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
        uint64_t gva = mem[1];
        memset(cpu, 0, 4*4096);

        *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
        *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;

        struct kbase_atom atoms[2] = {0};
        atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1;
        atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2;

        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});
        usleep(100000);
        printf("poly0=0x%08x poly1=0x%08x\n",
               *(uint32_t*)((uint8_t*)cpu+0x200), *(uint32_t*)((uint8_t*)cpu+0x400));
        read(fd, (uint8_t[24]){0}, 24);
        munmap(cpu, 4*4096);
    }

    printf("=== DONE - 2 atoms works ===\n");
    close(fd);
    return 0;
}