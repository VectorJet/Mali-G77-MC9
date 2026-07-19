#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS     _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_CPU_RD (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR (1ULL << 3)
#define BASE_MEM_SAME_VA     (1ULL << 13)

/*
 * Bifrost job descriptor header layout (each word = 4 bytes):
 *   word 0  [0x00]: reserved
 *   word 1  [0x04]: job_type (bits 7:1) | flags
 *                   job_type=7 for TILER, 9 for FRAGMENT, 4 for COMPUTE
 *   word 2  [0x08]: job descriptor size (bytes, including header)
 *   word 3  [0x0C]: next job lo (0 = no chain)
 *   word 4  [0x10]: next job hi
 *   word 5  [0x14]: job-specific payload starts here
 *
 * For TILER job payload (starting at word 5 / byte 0x14):
 *   +0x00: polygon_list ptr lo
 *   +0x04: polygon_list ptr hi
 *   +0x08: tiler_heap_free lo
 *   +0x0C: tiler_heap_free hi
 *   +0x10: width  (pixels)
 *   +0x14: height (pixels)
 *   ... (remaining fields can be zero for minimal test)
 */

#pragma pack(push,1)
struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres; uint8_t jit_id[2], pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
    uint32_t frame_nr;
};
#pragma pack(pop)

static void *alloc_gpu(int fd, size_t pages, uint64_t *gva_out) {
    uint64_t mem[4] = {
        pages, pages, 0,
        BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
        BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
        BASE_MEM_SAME_VA
    };
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) return NULL;
    *gva_out = mem[1];
    void *cpu = mmap(NULL, pages * 4096, PROT_READ|PROT_WRITE,
                     MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) return NULL;
    memset(cpu, 0, pages * 4096);
    return cpu;
}

static uint32_t read_event(int fd) {
    uint8_t ev[24] = {0};
    ssize_t n = read(fd, ev, sizeof(ev));
    if (n < 4) return 0xFFFFFFFF;
    return *(uint32_t *)ev;
}

int main(void) {
    printf("=== Bifrost TILER job v2 (correct header layout) ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    uint32_t flags = 0;
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags);
    printf("[OK] init fd=%d\n", fd);

    /* Allocate 16 pages: job descriptors + polygon list + scratch */
    uint64_t gva;
    uint8_t *cpu = alloc_gpu(fd, 16, &gva);
    if (!cpu) { perror("alloc"); return 1; }
    printf("[OK] GPU buf: cpu=%p gva=0x%llx\n", cpu, (unsigned long long)gva);

    /*
     * Build TILER job descriptor at offset 0x000
     * Polygon list scratch at offset 0x1000
     * Tiler heap scratch  at offset 0x2000
     */
    uint64_t jc_tiler  = gva + 0x000;
    uint64_t poly_list = gva + 0x1000;
    uint64_t heap_free = gva + 0x2000;

    uint32_t *jd = (uint32_t *)cpu;  /* tiler job descriptor */

    /* word 0: reserved */
    jd[0] = 0;
    /* word 1 [0x04]: job_type=7 (TILER) in bits 7:1 */
    jd[1] = (7 << 1);
    /* word 2 [0x08]: descriptor size in bytes (header=0x14 + payload) */
    jd[2] = 0x60;  /* 96 bytes total, covers tiler payload */
    /* word 3+4 [0x0C]: next job ptr = 0 (no chain) */
    jd[3] = 0;
    jd[4] = 0;

    /* Tiler payload at 0x14 */
    uint32_t *pl = jd + 5;  /* byte 0x14 */
    /* polygon_list ptr */
    pl[0] = (uint32_t)(poly_list & 0xFFFFFFFF);
    pl[1] = (uint32_t)(poly_list >> 32);
    /* tiler_heap_free ptr */
    pl[2] = (uint32_t)(heap_free & 0xFFFFFFFF);
    pl[3] = (uint32_t)(heap_free >> 32);
    /* width, height (minimal non-zero) */
    pl[4] = 1;  /* width  */
    pl[5] = 1;  /* height */

    /* Polygon list: write a minimal header (end-of-list marker) */
    uint32_t *poly = (uint32_t *)(cpu + 0x1000);
    poly[0] = 0x00000000;  /* empty polygon list */

    printf("[OK] TILER jd at 0x%llx, poly_list=0x%llx, heap=0x%llx\n",
           (unsigned long long)jc_tiler,
           (unsigned long long)poly_list,
           (unsigned long long)heap_free);
    printf("     jd[1](type)=0x%08x jd[2](size)=0x%08x\n", jd[1], jd[2]);

    /* Submit atom */
    struct kbase_atom atom = {0};
    atom.jc          = jc_tiler;
    atom.core_req    = 0x004;   /* BASE_JD_REQ_T */
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr, stride; } sub = {
        .addr   = (uint64_t)&atom,
        .nr     = 1,
        .stride = 72
    };

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
    printf("[%s] JOB_SUBMIT ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);

    /* Wait for completion event */
    usleep(200000);
    uint32_t ev = read_event(fd);
    printf("Event code: 0x%08x\n", ev);

    switch (ev) {
    case 0x01: printf("=> BASE_JD_EVENT_DONE - SUCCESS!\n"); break;
    case 0x04: printf("=> BASE_JD_EVENT_TERMINATED - soft kill (descriptor still wrong?)\n"); break;
    case 0x40: printf("=> JOB_CONFIG_FAULT\n"); break;
    case 0x41: printf("=> JOB_POWER_FAULT\n"); break;
    case 0x42: printf("=> JOB_READ_FAULT - GPU couldn't read descriptor\n"); break;
    case 0x43: printf("=> JOB_WRITE_FAULT\n"); break;
    case 0x44: printf("=> JOB_AFFINITY_FAULT\n"); break;
    case 0x48: printf("=> JOB_BUS_FAULT\n"); break;
    default:   printf("=> unknown\n"); break;
    }

    munmap(cpu, 16 * 4096);
    close(fd);
    return 0;
}
