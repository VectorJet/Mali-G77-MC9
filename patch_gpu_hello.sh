cat << 'INNER' > src/kbase/gpu_hello.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    struct { uint16_t major, minor; } ver = {11, 46}; ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4), &ver);
    uint32_t flags = 0; ioctl(fd, _IOC(_IOC_WRITE, 0x80, 1, 4), &flags);
    uint8_t ioctl_37_arg = 0x08; ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x37, 1), &ioctl_37_arg);
    uint64_t req[4] = {2, 2, 0, 0xF}; ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32), req);
    uint64_t gpu_va = req[1];
    void *cpu_ptr = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);

    // TLSTREAM ACQUIRE
    char tlstream[32] = "malitl_1234_0xdeadbeef";
    int r1 = ioctl(fd, _IOC(_IOC_WRITE, 0x80, 0x18, 32), tlstream);
    printf("tlstream ret=%d\n", r1);

    uint32_t *job = (uint32_t *)cpu_ptr;
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_ptr + 4096);
    *target = 0xAAAAAAAA;

    memset(job, 0, 128);
    job[4] = (2 << 1) | (1 << 16); // WRITE_VALUE
    job[8] = (uint32_t)((gpu_va + 4096) & 0xFFFFFFFF);
    job[9] = (uint32_t)((gpu_va + 4096) >> 32);
    job[10] = 6; job[12] = 0xDEADBEEF;

    uint8_t atom[72] = {0};
    uint64_t *a64 = (uint64_t*)atom;
    a64[0] = 0; // seq_nr
    a64[1] = gpu_va; // jc
    a64[2] = 0x1234; // udata[0]
    atom[0x30] = 1; // atom_number
    *(uint32_t*)(&atom[0x34]) = 0x10; // V

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = { (uint64_t)(uintptr_t)atom, 1, 72 };
    ioctl(fd, _IOC(_IOC_WRITE, 0x80, 2, 16), &submit);
    
    // Wait slightly
    usleep(100000);
    
    uint8_t ev[24] = {0};
    read(fd, ev, 24);
    printf("Event code: %x\n", ev[0]);
    printf("Target: %x\n", *target);

    close(fd); return 0;
}
INNER
