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

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test Multi-Atom: WRITE_VALUE + FRAGMENT ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate two separate buffers like Chrome does */
    /* Buffer 1: For WRITE_VALUE job */
    uint64_t mem1[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem1);
    void *buf1 = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem1[1]);
    uint64_t gva1 = mem1[1];

    /* Buffer 2: For FRAGMENT job + FBD */
    uint64_t mem2[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem2);
    void *buf2 = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem2[1]);
    uint64_t gva2 = mem2[1];

    memset(buf1, 0, 2*4096);
    memset(buf2, 0, 8*4096);

    /* Color buffer at start of buf2 */
    volatile uint32_t *color = (volatile uint32_t *)buf2;
    *color = 0xDEADBEEF;
    printf("Color at 0x%llx = 0xDEADBEEF\n", (unsigned long long)gva2);

    /* Setup WRITE_VALUE job in buf1 at offset 0 */
    uint32_t *job1 = (uint32_t *)buf1;
    job1[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE */
    job1[8] = (uint32_t)(gva2);      /* write to color buffer */
    job1[9] = 0;
    job1[10] = 6;                     /* IMMEDIATE32 */
    job1[12] = 0xCAFEBABE;           /* value to write */

    printf("WRITE_VALUE job at 0x%llx -> write 0xCAFEBABE to 0x%llx\n",
           (unsigned long long)gva1, (unsigned long long)gva2);

    /* Setup FRAGMENT job in buf2 at offset 0 */
    /* Simple SFBD at offset 0x100 */
    uint32_t *sfbd = (uint32_t *)((uint8_t*)buf2 + 0x100);
    
    /* Parameters at 0x80 */
    sfbd[0x80/4 + 0] = 256;   /* width */
    sfbd[0x80/4 + 1] = 256;   /* height */
    sfbd[0x80/4 + 2] = 0x2;   /* format */
    sfbd[0x80/4 + 3] = 0;     /* swizzle */
    
    /* RT at 0x100 */
    uint32_t *rt = (uint32_t *)((uint8_t*)buf2 + 0x200);
    rt[0] = (uint32_t)gva2;   /* color address */
    rt[1] = 0;
    rt[2] = 256 * 4;          /* stride */
    rt[3] = 0;
    
    /* Point RT from SFBD */
    sfbd[0x80/4 + 8] = (uint32_t)(gva2 + 0x200);

    uint32_t *job2 = (uint32_t *)buf2;
    job2[4] = (5 << 1) | (1 << 16);  /* FRAGMENT */
    job2[8] = (uint32_t)(gva2 + 0x100);  /* SFBD address */
    job2[9] = 0;
    job2[10] = 0;
    job2[11] = 0;
    job2[12] = 0;

    printf("FRAGMENT job at 0x%llx, FBD at 0x%llx\n",
           (unsigned long long)gva2, (unsigned long long)(gva2+0x100));

    /* Two atoms: WRITE_VALUE then FRAGMENT with dependency */
    struct kbase_atom atoms[2] = {0};

    /* Atom 0: WRITE_VALUE */
    atoms[0].jc = gva1;
    atoms[0].core_req = 0x203;  /* CS + CF */
    atoms[0].atom_number = 1;
    atoms[0].prio = 0;

    /* Atom 1: FRAGMENT - depends on atom 0 */
    atoms[1].jc = gva2;
    atoms[1].core_req = 0x001;  /* FS */
    atoms[1].atom_number = 2;
    atoms[1].prio = 0;
    atoms[1].pre_dep_atom[0] = 0;   /* depends on atom 0 */
    atoms[1].pre_dep_type[0] = 1;  /* type 1 = job complete */

    printf("\nSubmitting 2 atoms:\n");
    printf("Atom0: jc=0x%llx core_req=0x%x\n", (unsigned long long)atoms[0].jc, atoms[0].core_req);
    printf("Atom1: jc=0x%llx core_req=0x%x dep=(%d,%d)\n", 
           (unsigned long long)atoms[1].jc, atoms[1].core_req,
           atoms[1].pre_dep_atom[0], atoms[1].pre_dep_type[0]);

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 2, .stride = 72});

    usleep(500000);

    printf("\nResults:\n");
    printf("Color: 0x%08x\n", *color);
    
    if (*color == 0xCAFEBABE) {
        printf("*** WRITE_VALUE worked! ***\n");
    } else if (*color == 0xDEADBEEF) {
        printf("Color unchanged\n");
    } else {
        printf("*** GPU modified color! ***\n");
    }

    read(fd, (uint8_t[24]){0}, 24);
    munmap(buf1, 2*4096);
    munmap(buf2, 8*4096);
    close(fd);
    return 0;
}