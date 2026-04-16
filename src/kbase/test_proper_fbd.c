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
    printf("=== Test 3: Proper FBD/Tiler State ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    /* Color buffer: 16x16 pixels */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x40);
    for (int i = 0; i < 16*16; i++) color[i] = 0xCCCCCCCC;

    /* ===== Job 1: WRITE_VALUE ===== */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x100);
    job0[12] = 1;

    struct kbase_atom atom0 = {0};
    atom0.jc = gva;
    atom0.core_req = 0x203;
    atom0.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom0, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);

    /* ===== Job 2: VERTEX ===== */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job1[4] = (3 << 1) | (1 << 16);
    job1[8] = (uint32_t)(gva + 0x300);
    job1[12] = 0;

    float *vbuf = (float *)((uint8_t*)cpu + 0x300);
    vbuf[0] = 0.0f; vbuf[1] = 0.9f; vbuf[2] = 0.0f; vbuf[3] = 1.0f;
    vbuf[4] = -0.9f; vbuf[5] = -0.9f; vbuf[6] = 0.0f; vbuf[7] = 1.0f;
    vbuf[8] = 0.9f; vbuf[9] = -0.9f; vbuf[10] = 0.0f; vbuf[11] = 1.0f;

    struct kbase_atom atom1 = {0};
    atom1.jc = gva + 0x200;
    atom1.core_req = 0x008;
    atom1.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom1, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);

    /* ===== Job 3: TILER with proper state ===== */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x400);
    job2[4] = (4 << 1) | (1 << 16);
    job2[8] = (uint32_t)(gva + 0x500);
    job2[12] = 0;

    /* Polygon list with more detailed state */
    uint32_t *poly = (uint32_t *)((uint8_t*)cpu + 0x500);
    poly[0] = 0x00000001;
    poly[1] = 0x00000000;
    poly[2] = (uint32_t)(gva + 0x300);
    poly[3] = 3;
    poly[4] = 0x00000000;  /* first vertex index */
    poly[5] = 3;           /* vertex count */
    poly[6] = 0x00000000;
    poly[7] = 0x00000000;

    struct kbase_atom atom2 = {0};
    atom2.jc = gva + 0x400;
    atom2.core_req = 0x004;
    atom2.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom2, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);

    /* ===== Job 4: FRAGMENT with proper FBD ===== */
    uint32_t *job3 = (uint32_t *)((uint8_t*)cpu + 0x600);
    job3[4] = (5 << 1) | (1 << 16);
    job3[8] = (uint32_t)(gva + 0x800);
    job3[12] = 0;

    /* Full FBD at 0x800 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x800);
    /* LOCAL_STORAGE at 0x00 */
    memset(fbd, 0, 0x80);
    /* PARAMETERS at 0x80 */
    fbd[0x80/4 + 0] = 16;    /* width */
    fbd[0x80/4 + 1] = 16;    /* height */
    fbd[0x80/4 + 2] = 0x00000002;  /* format: RGBA8 */
    fbd[0x80/4 + 3] = 0x00000000;  /* swizzle */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x900);  /* RT pointer */

    /* RT at 0x900 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x900);
    rt[0] = (uint32_t)(gva + 0x40);  /* color buffer */
    rt[1] = 0;
    rt[2] = 16 * 4;  /* stride */
    rt[3] = 0;
    rt[4] = 0;

    struct kbase_atom atom3 = {0};
    atom3.jc = gva + 0x600;
    atom3.core_req = 0x003;
    atom3.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom3, .nr = 1, .stride = 72});
    usleep(100000);
    read(fd, (uint8_t[24]){0}, 24);

    printf("\n=== RESULT ===\n");
    int changed = 0;
    for (int i = 0; i < 16*16; i++) {
        if (color[i] != 0xCCCCCCCC) changed++;
    }
    printf("Changed: %d / 256\n", changed);
    for (int y = 0; y < 4; y++) {
        printf("Row %d: ", y);
        for (int x = 0; x < 8; x++) {
            printf("%08x ", color[y*16 + x]);
        }
        printf("\n");
    }

    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}