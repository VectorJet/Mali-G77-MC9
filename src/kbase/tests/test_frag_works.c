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
    printf("=== FRAGMENT Working! Color buffer test ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Larger buffer for FBD + RT + color */
    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Color buffer at 0x100 - this is where the GPU will write */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0xDEADBEEF;
    printf("Color buffer at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva+0x100));

    /* FRAGMENT job at offset 0 */
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);  /* FRAGMENT type=5 */
    job[8] = (uint32_t)(gva + 0x300);  /* FBD at 0x300 */
    job[12] = 0;

    /* FBD at 0x300 - Single Target Framebuffer Descriptor */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x300);
    
    /* Parameters at offset 0x80 in FBD */
    fbd[0x80/4 + 0] = 256;    /* width */
    fbd[0x80/4 + 1] = 256;    /* height */
    fbd[0x80/4 + 2] = 0x2;    /* format: R8G8B8A8 */
    fbd[0x80/4 + 3] = 0;      /* swizzle */
    
    /* Render target pointer at offset 0x80 + 0x20 = 0xa0 in FBD = word 40 */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x400);  /* RT at 0x400 */
    fbd[0x80/4 + 9] = 0;

    /* Render Target at 0x400 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x400);
    rt[0] = (uint32_t)(gva + 0x100);  /* color buffer base */
    rt[1] = 0;
    rt[2] = 256 * 4;        /* row stride: 256 pixels * 4 bytes = 1024 */
    rt[3] = 0;

    printf("FBD at 0x%llx, RT at 0x%llx, Color at 0x%llx\n",
           (unsigned long long)(gva+0x300), (unsigned long long)(gva+0x400), (unsigned long long)(gva+0x100));

    /* Key: core_req = 0x003 (FS + CS) - this is the breakthrough! */
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x003;  /* FS + CS - FRAGMENT needs both! */
    atom.atom_number = 1;
    atom.prio = 0;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72});

    printf("Job submitted with core_req=0x003 (FS+CS)\n");

    usleep(500000);

    printf("\n=== RESULT ===\n");
    printf("Color before: 0xDEADBEEF\n");
    printf("Color after:  0x%08x\n", *color);

    if (*color != 0xDEADBEEF) {
        printf("\n*** SUCCESS: GPU EXECUTED FRAGMENT JOB! ***\n\n");
        printf("The Mali-G77 GPU executed the FRAGMENT job and modified\n");
        printf("memory at GPU VA 0x%llx\n", (unsigned long long)(gva+0x100));
    } else {
        printf("\n*** FAIL ***\n");
    }

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}