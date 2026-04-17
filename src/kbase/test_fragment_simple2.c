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
    printf("=== Valhall Simple Fragment Test ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);
    printf("Buffer GVA: 0x%llx\n", (unsigned long long)gva);

    /* Color buffer at 0x30 - will hold the final color */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *color = 0xDEADBEEF;

    /* FRAGMENT job alone with full FBD */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (9 << 1) | (1 << 16);  /* Type 9 = Fragment */
    job[8] = (uint32_t)(gva + 0x100);  /* FBD pointer */
    job[12] = 0;

    /* FBD at 0x100 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x100);
    
    /* LOCAL_STORAGE at 0x00 */
    memset(fbd, 0, 128);
    
    /* Parameters at 0x80 */
    fbd[0x80/4 + 0] = 256;   /* width */
    fbd[0x80/4 + 1] = 256;   /* height */
    fbd[0x80/4 + 2] = 2;     /* format = RGBA8 */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x200);  /* render target pointer */

    /* Render Target at 0x200 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x200);
    rt[0] = (uint32_t)(gva + 0x30);  /* color buffer pointer */
    rt[2] = 256;  /* row stride */

    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x003;  /* FS + CS */
    atom.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});
    usleep(100000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("FRAGMENT done\n");

    printf("\n=== RESULT ===\n");
    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);

    if (*color == 0xDEADBEEF) printf("No change - no fragment shader output\n");
    else if (*color == 0x00000000) printf("Cleared - fragment ran but no shader color\n");
    else printf("*** CHANGED! ***\n");

    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}