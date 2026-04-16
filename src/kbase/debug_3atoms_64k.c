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
    printf("=== Debug: 3 atoms with PAGE-ALIGNED 64KB buffers ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate 3x 64KB (16 page) buffers - page aligned */
    uint64_t mem1[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);
    uint64_t gva1 = mem1[1];

    uint64_t mem2[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);
    uint64_t gva2 = mem2[1];

    uint64_t mem3[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem3);
    void *buf3 = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem3[1]);
    uint64_t gva3 = mem3[1];

    printf("Buffers: 0x%llx, 0x%llx, 0x%llx\n", 
           (unsigned long long)gva1, (unsigned long long)gva2, (unsigned long long)gva3);

    memset(buf1, 0, 16*4096);
    memset(buf2, 0, 16*4096);
    memset(buf3, 0, 16*4096);

    /* Each job at offset 0 in its own buffer */
    *(uint32_t*)((uint8_t*)buf1 + 0x100) = 1;
    *(uint32_t*)((uint8_t*)buf2 + 0x100) = 1;
    *(uint32_t*)((uint8_t*)buf3 + 0x100) = 1;

    /* 3 atoms, each using DIFFERENT buffer */
    struct kbase_atom atoms[3] = {0};
    atoms[0].jc = gva1;  /* Buffer 1, offset 0 */
    atoms[0].core_req = 0x004;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva2;  /* Buffer 2, offset 0 */
    atoms[1].core_req = 0x004;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = gva3;  /* Buffer 3, offset 0 */
    atoms[2].core_req = 0x004;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    printf("Submitting 3 atoms (3 separate 64KB buffers)...\n");
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    sleep(2);
    printf("buf1=0x%08x buf2=0x%08x buf3=0x%08x\n",
           *(uint32_t*)((uint8_t*)buf1+0x100),
           *(uint32_t*)((uint8_t*)buf2+0x100),
           *(uint32_t*)((uint8_t*)buf3+0x100));

    read(fd, (uint8_t[24]){0}, 24);
    munmap(buf1, 16*4096);
    munmap(buf2, 16*4096);
    munmap(buf3, 16*4096);
    close(fd);
    return 0;
}