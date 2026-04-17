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
    printf("=== 3-Atom: V->T->F (Separate Buffers) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate SEPARATE buffers for each job - like Chrome */
    uint64_t mem_v[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_v);
    void *buf_v = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_v[1]);
    uint64_t gva_v = mem_v[1];

    uint64_t mem_t[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_t);
    void *buf_t = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_t[1]);
    uint64_t gva_t = mem_t[1];

    uint64_t mem_f[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_f);
    void *buf_f = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_f[1]);
    uint64_t gva_f = mem_f[1];

    memset(buf_v, 0, 2*4096);
    memset(buf_t, 0, 2*4096);
    memset(buf_f, 0, 4*4096);

    /* Color buffer in F buffer at 0x30 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)buf_f + 0x30);
    *color = 0xDEADBEEF;
    printf("Color at 0x%llx = 0xDEADBEEF\n", (unsigned long long)(gva_f+0x30));

    /* VERTEX job in buffer V at offset 0 */
    uint32_t *job_v = (uint32_t *)buf_v;
    job_v[4] = (3 << 1) | (1 << 16);
    job_v[8] = (uint32_t)(gva_v + 0x100);
    job_v[12] = 0;

    /* TILER job in buffer T at offset 0 */
    uint32_t *job_t = (uint32_t *)buf_t;
    job_t[4] = (4 << 1) | (1 << 16);
    job_t[8] = (uint32_t)(gva_t + 0x100);  /* poly list */
    job_t[12] = 0;

    /* Polygon list at 0x100 in buffer T */
    volatile uint32_t *poly = (volatile uint32_t *)((uint8_t*)buf_t + 0x100);
    *poly = 0x00000001;

    /* FRAGMENT job in buffer F at offset 0 */
    uint32_t *job_f = (uint32_t *)buf_f;
    job_f[4] = (5 << 1) | (1 << 16);
    job_f[8] = (uint32_t)(gva_f + 0x100);  /* FBD */
    job_f[12] = 0;

    /* FBD at 0x100 in buffer F */
    uint32_t *fbd = (uint32_t *)((uint8_t*)buf_f + 0x100);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva_f + 0x200);  /* RT */

    /* RT at 0x200 in buffer F */
    uint32_t *rt = (uint32_t *)((uint8_t*)buf_f + 0x200);
    rt[0] = (uint32_t)(gva_f + 0x30);  /* color buffer */
    rt[2] = 4;

    printf("V(0x%llx) -> T(0x%llx) -> F(0x%llx)\n",
           gva_v, gva_t, gva_f);

    /* 3 atoms with dependencies */
    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = gva_v;
    atoms[0].core_req = 0x008;
    atoms[0].atom_number = 1;

    atoms[1].jc = gva_t;
    atoms[1].core_req = 0x004;
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = gva_f;
    atoms[2].core_req = 0x003;
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});

    printf("Submitted...\n");

    usleep(300000);

    printf("Color: 0x%08x\n", *color);
    if (*color != 0xDEADBEEF) printf("*** SUCCESS! ***\n");
    else printf("No change\n");

    read(fd, (uint8_t[24]){0}, 24);
    munmap(buf_v, 2*4096);
    munmap(buf_t, 2*4096);
    munmap(buf_f, 4*4096);
    close(fd);
    return 0;
}