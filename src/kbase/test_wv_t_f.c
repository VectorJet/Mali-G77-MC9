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
    printf("=== 3-Atom: WV -> T -> F (No VERTEX) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Separate buffers */
    uint64_t mem_wv[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_wv);
    void *buf_wv = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_wv[1]);
    uint64_t gva_wv = mem_wv[1];

    uint64_t mem_t[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_t);
    void *buf_t = mmap(NULL, 2*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_t[1]);
    uint64_t gva_t = mem_t[1];

    uint64_t mem_f[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_f);
    void *buf_f = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem_f[1]);
    uint64_t gva_f = mem_f[1];

    memset(buf_wv, 0, 2*4096);
    memset(buf_t, 0, 2*4096);
    memset(buf_f, 0, 4*4096);

    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)buf_f + 0x30);
    *color = 0xDEADBEEF;
    printf("Color at 0x%lx\n", gva_f+0x30);

    /* WRITE_VALUE in buffer WV */
    uint32_t *job_wv = (uint32_t *)buf_wv;
    job_wv[4] = (2 << 1) | (1 << 16);
    job_wv[8] = (uint32_t)(gva_t + 0x100);
    job_wv[12] = 0x00000001;

    /* TILER in buffer T */
    uint32_t *job_t = (uint32_t *)buf_t;
    job_t[4] = (4 << 1) | (1 << 16);
    job_t[8] = (uint32_t)(gva_t + 0x100);
    job_t[12] = 0;

    volatile uint32_t *poly = (volatile uint32_t *)((uint8_t*)buf_t + 0x100);
    *poly = 0x00000001;

    /* FRAGMENT in buffer F */
    uint32_t *job_f = (uint32_t *)buf_f;
    job_f[4] = (5 << 1) | (1 << 16);
    job_f[8] = (uint32_t)(gva_f + 0x100);
    job_f[12] = 0;

    uint32_t *fbd = (uint32_t *)((uint8_t*)buf_f + 0x100);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(gva_f + 0x200);

    uint32_t *rt = (uint32_t *)((uint8_t*)buf_f + 0x200);
    rt[0] = (uint32_t)(gva_f + 0x30);
    rt[2] = 4;

    printf("WV(0x%lx) -> T(0x%lx) -> F(0x%lx)\n", gva_wv, gva_t, gva_f);

    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = gva_wv;
    atoms[0].core_req = 0x203;
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
    munmap(buf_wv, 2*4096);
    munmap(buf_t, 2*4096);
    munmap(buf_f, 4*4096);
    close(fd);
    return 0;
}