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
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

struct base_dependency { uint8_t atom_id; uint8_t dep_type; } __attribute__((packed));
struct kbase_atom_mtk {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t  jit_id[2];
    struct base_dependency pre_dep[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;
    uint32_t pad2;
} __attribute__((packed));
struct kbase_ioctl_job_submit { uint64_t addr; uint32_t nr_atoms; uint32_t stride; };

#define OFF_JOB     0x0000
#define OFF_TLS_D   0x0100
#define OFF_TLS_S   0x0140
#define OFF_DCD     0x1200
#define OFF_FBD     0x1400
#define OFF_COLOR   0x2000
#define TOTAL_PAGES 8
#define FB_WIDTH 64
#define FB_HEIGHT 64

static void write64(void *base, size_t off, uint64_t val) {
    *(uint64_t *)((uint8_t *)base + off) = val;
}

static const uint8_t clear_dcd[128] = {
    [0x70] = 0x40, [0x71] = 0xb1, [0x72] = 0xf1, [0x73] = 0xff, [0x74] = 0x5e
};

static const uint8_t clear_fbd[256] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x40,0xfc,0xff,0x5e,0x00,0x00,0x00,0x00,0xb2,0xf1,0xff,0x5e,0x00,0x00,0x00,
    0x3f,0x00,0x3f,0x00,0x00,0x00,0x00,0x00,0x3f,0x00,0x3f,0x00,0x00,0x10,0x03,0x01,
    [0x80]=0x00,[0x81]=0x00,[0x82]=0x00,[0x83]=0x04,[0x84]=0x99,[0x85]=0x4c,[0x86]=0x0a,[0x87]=0x86,
    [0x88]=0x01,[0x89]=0x28
};

int main(void) {
    printf("=== Mali-G77 Clear-style Fragment Replay ===\n");
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    uint16_t ver = 11;
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) { perror("VERSION_CHECK"); return 1; }
    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) { perror("SET_FLAGS"); return 1; }

    uint64_t mem[4] = { TOTAL_PAGES, TOTAL_PAGES, 0, 0x200F };
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) { perror("MEM_ALLOC"); return 1; }
    void *cpu = mmap(NULL, TOTAL_PAGES * 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) { perror("mmap"); return 1; }
    uint64_t gva = (uint64_t)cpu;
    memset(cpu, 0, TOTAL_PAGES * 4096);

    memcpy((uint8_t*)cpu + OFF_DCD, clear_dcd, sizeof(clear_dcd));
    memcpy((uint8_t*)cpu + OFF_FBD, clear_fbd, sizeof(clear_fbd));

    /* Try to make the clear path target our memory. */
    write64(cpu, OFF_DCD + 0x28, gva + OFF_COLOR);
    write64(cpu, OFF_DCD + 0x70, gva + OFF_TLS_D);
    write64(cpu, OFF_FBD + 0x10, gva + OFF_TLS_S);
    write64(cpu, OFF_FBD + 0x18, gva + OFF_DCD);

    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + OFF_COLOR);
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) color[i] = 0xDEADBEEF;

    uint32_t *job = (uint32_t *)((uint8_t*)cpu + OFF_JOB);
    job[4] = (9 << 1) | (1 << 16); /* type 9 fragment */
    job[8] = 0;
    job[9] = (FB_WIDTH - 1) | ((FB_HEIGHT - 1) << 16);
    write64(job, 10*4, (gva + OFF_FBD) | 1);

    for (int pass = 0; pass < 2; pass++) {
        uint32_t core_req = pass == 0 ? 0x003 : 0x049;
        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) color[i] = 0xDEADBEEF;
        struct kbase_atom_mtk atom = {0};
        atom.jc = gva + OFF_JOB;
        atom.core_req = core_req;
        atom.atom_number = 1;
        atom.jobslot = 0;
        atom.frame_nr = 1;
        struct kbase_ioctl_job_submit submit = { .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72 };
        int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
        printf("submit core_req=0x%x ret=%d\n", core_req, ret);
        usleep(300000);
        uint8_t ev[24] = {0};
        read(fd, ev, 24);
        printf("  event code=0x%x atom=%u exception=0x%x\n",
               *(uint32_t*)ev, *(uint32_t*)(ev+4), job[0]);
        int changed = 0;
        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) if (color[i] != 0xDEADBEEF) changed++;
        printf("  changed pixels=%d first=%08x\n", changed, color[0]);
    }

    munmap(cpu, TOTAL_PAGES * 4096);
    close(fd);
    return 0;
}
