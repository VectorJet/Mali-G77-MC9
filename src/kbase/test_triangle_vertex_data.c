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
    printf("=== Test: Triangle with Proper Vertex Data ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {8, 8, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 8*4096);

    printf("Buffer GVA: 0x%llx\n", (unsigned long long)gva);

    /* Color buffer at 0x30 */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x30);
    *color = 0xDEADBEEF;

    /* ===== Job 1: WRITE_VALUE - Initialize polygon list ===== */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);
    job0[8] = (uint32_t)(gva + 0x100);
    job0[12] = 0x00000001;

    struct kbase_atom atom0 = {0};
    atom0.jc = gva;
    atom0.core_req = 0x203;
    atom0.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom0, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("WRITE_VALUE done\n");

    /* ===== Job 2: VERTEX - Vertex processing ===== */
    /* Vertex job at 0x200 - points to vertex buffer at 0x300 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job1[4] = (3 << 1) | (1 << 16);  /* VERTEX */
    job1[8] = (uint32_t)(gva + 0x300);  /* attribute buffer */
    job1[12] = 0;

    /* Vertex buffer at 0x300 - 3 vertices for triangle */
    float *vertices = (float *)((uint8_t*)cpu + 0x300);
    /* Vertex 0: x, y, z, w */
    vertices[0] = 0.0f;   /* x */
    vertices[1] = 0.5f;   /* y */
    vertices[2] = 0.0f;   /* z */
    vertices[3] = 1.0f;   /* w */
    /* Vertex 1 */
    vertices[4] = -0.5f;
    vertices[5] = -0.5f;
    vertices[6] = 0.0f;
    vertices[7] = 1.0f;
    /* Vertex 2 */
    vertices[8] = 0.5f;
    vertices[9] = -0.5f;
    vertices[10] = 0.0f;
    vertices[11] = 1.0f;

    struct kbase_atom atom1 = {0};
    atom1.jc = gva + 0x200;
    atom1.core_req = 0x008;
    atom1.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom1, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("VERTEX done\n");

    /* ===== Job 3: TILER - Setup tiling ===== */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x400);
    job2[4] = (4 << 1) | (1 << 16);  /* TILER */
    job2[8] = (uint32_t)(gva + 0x500);  /* polygon list */
    job2[12] = 0;

    /* Polygon list at 0x500 - simplified tile descriptor */
    uint32_t *poly = (uint32_t *)((uint8_t*)cpu + 0x500);
    poly[0] = 0x00000001;  /* enable bit */
    poly[1] = 0x00000000;
    poly[2] = (uint32_t)(gva + 0x300);  /* vertex buffer */
    poly[3] = 3;  /* vertex count */

    struct kbase_atom atom2 = {0};
    atom2.jc = gva + 0x400;
    atom2.core_req = 0x004;
    atom2.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom2, .nr = 1, .stride = 72});
    usleep(50000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("TILER done\n");

    /* ===== Job 4: FRAGMENT - Render ===== */
    uint32_t *job3 = (uint32_t *)((uint8_t*)cpu + 0x600);
    job3[4] = (5 << 1) | (1 << 16);  /* FRAGMENT */
    job3[8] = (uint32_t)(gva + 0x800);  /* FBD */
    job3[12] = 0;

    /* FBD at 0x800 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x800);
    fbd[0x80/4 + 0] = 256;   /* width */
    fbd[0x80/4 + 1] = 256;   /* height */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x900);  /* RT */

    /* RT at 0x900 */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x900);
    rt[0] = (uint32_t)(gva + 0x30);  /* color buffer */
    rt[2] = 4;  /* stride */

    struct kbase_atom atom3 = {0};
    atom3.jc = gva + 0x600;
    atom3.core_req = 0x003;  /* FS + CS */
    atom3.atom_number = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atom3, .nr = 1, .stride = 72});
    usleep(100000);
    read(fd, (uint8_t[24]){0}, 24);
    printf("FRAGMENT done\n");

    printf("\n=== RESULT ===\n");
    printf("Color: 0x%08x (was 0xDEADBEEF)\n", *color);

    if (*color == 0xDEADBEEF) {
        printf("No change - need more vertex data\n");
    } else if (*color == 0x00000000) {
        printf("Cleared\n");
    } else {
        printf("*** CHANGE! R=%d G=%d B=%d A=%d\n",
               (*color >> 0) & 0xFF,
               (*color >> 8) & 0xFF,
               (*color >> 16) & 0xFF,
               (*color >> 24) & 0xFF);
    }

    munmap(cpu, 8*4096);
    close(fd);
    return 0;
}