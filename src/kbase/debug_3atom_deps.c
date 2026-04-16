#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <signal.h>

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

volatile int alarm_triggered = 0;

void alarm_handler(int sig) {
    alarm_triggered = 1;
}

int main(void) {
    printf("=== Debug: 3 Atoms with Deps - Various Tests ===\n\n");

    signal(SIGALRM, alarm_handler);

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Test 1: Try WITHOUT dependencies first */
    printf("=== Test A: 3 atoms NO dependencies ===\n");
    {
        uint64_t mem[4] = {4, 4, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
        uint64_t gva = mem[1];
        memset(cpu, 0, 4*4096);

        volatile uint32_t *t = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
        *t = 0xDEADBEEF;

        /* 3 TILER jobs */
        *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;
        *(uint32_t*)((uint8_t*)cpu + 0x400) = 1;
        *(uint32_t*)((uint8_t*)cpu + 0x600) = 1;

        struct kbase_atom atoms[3] = {0};
        atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1;
        atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2;
        atoms[2].jc = gva + 0x400; atoms[2].core_req = 0x004; atoms[2].atom_number = 3;

        alarm(3);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});
        if (alarm_triggered) { printf("TIMEOUT!\n"); alarm_triggered = 0; }
        else { printf("OK\n"); usleep(100000); read(fd, (uint8_t[24]){0}, 24); }
        munmap(cpu, 4*4096);
    }

    /* Test 2: 3 atoms with only first dep */
    printf("\n=== Test B: 3 atoms, only first has dep ===\n");
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
        atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 1;

        alarm(3);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});
        if (alarm_triggered) { printf("TIMEOUT!\n"); alarm_triggered = 0; }
        else { printf("OK\n"); usleep(100000); read(fd, (uint8_t[24]){0}, 24); }
        munmap(cpu, 4*4096);
    }

    /* Test 3: 3 atoms all chained: 0->1->2 */
    printf("\n=== Test C: 3 atoms chain 0->1->2 ===\n");
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
        atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 1;

        alarm(3);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});
        if (alarm_triggered) { printf("TIMEOUT!\n"); alarm_triggered = 0; }
        else { printf("OK\n"); usleep(100000); read(fd, (uint8_t[24]){0}, 24); }
        munmap(cpu, 4*4096);
    }

    /* Test 4: Try pre_dep_type = 0 instead of 1 */
    printf("\n=== Test D: 2 atoms with dep_type=0 ===\n");
    {
        uint64_t mem[4] = {4, 4, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
        uint64_t gva = mem[1];
        memset(cpu, 0, 4*4096);

        *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;

        struct kbase_atom atoms[2] = {0};
        atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1;
        atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2;
        atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 0;  /* type 0 */

        alarm(3);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});
        if (alarm_triggered) { printf("TIMEOUT!\n"); alarm_triggered = 0; }
        else { printf("OK\n"); usleep(100000); read(fd, (uint8_t[24]){0}, 24); }
        munmap(cpu, 4*4096);
    }

    /* Test 5: Try different jobslot */
    printf("\n=== Test E: 2 atoms different jobslot ===\n");
    {
        uint64_t mem[4] = {4, 4, 0, 0xF};
        ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
        void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
        uint64_t gva = mem[1];
        memset(cpu, 0, 4*4096);

        *(uint32_t*)((uint8_t*)cpu + 0x200) = 1;

        struct kbase_atom atoms[2] = {0};
        atoms[0].jc = gva; atoms[0].core_req = 0x004; atoms[0].atom_number = 1; atoms[0].jobslot = 0;
        atoms[1].jc = gva + 0x200; atoms[1].core_req = 0x004; atoms[1].atom_number = 2; atoms[1].jobslot = 1;
        atoms[1].pre_dep_atom[0] = 0; atoms[1].pre_dep_type[0] = 1;

        alarm(3);
        ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});
        if (alarm_triggered) { printf("TIMEOUT!\n"); alarm_triggered = 0; }
        else { printf("OK\n"); usleep(100000); read(fd, (uint8_t[24]){0}, 24); }
        munmap(cpu, 4*4096);
    }

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}