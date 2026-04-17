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
    printf("=== RENDER TRIANGLE: Full Pipeline Test ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* Allocate buffer for all stages */
    uint64_t mem[4] = {16, 16, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 16*4096);

    /* Color buffer at 0x100 - we'll fill this with a color */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *color = 0x00000000;  /* Start black */
    printf("Color buffer at 0x%llx\n", (unsigned long long)(gva+0x100));

    /* 
     * STAGE 1: WRITE_VALUE - Zero out memory / initialize
     */
    uint32_t *job0 = (uint32_t *)cpu;
    job0[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE */
    job0[8] = (uint32_t)(gva + 0x200);  /* polygon list area */
    job0[12] = 0x00000000;  /* zero */

    /* 
     * STAGE 2: VERTEX - Simple pass-through
     * Just copies data to show it's working
     */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x200);
    job1[4] = (3 << 1) | (1 << 16);  /* VERTEX */
    job1[8] = (uint32_t)(gva + 0x400);  /* attribute buffer */
    job1[12] = 0;

    /* Attribute buffer at 0x400 - vertex data */
    /* Simple vertex format: x, y, z, w for each vertex */
    float *attr = (float *)((uint8_t*)cpu + 0x400);
    /* Vertex 0: position (0.5, 0.5, 0, 1) */
    attr[0] = 0.5f; attr[1] = 0.5f; attr[2] = 0.0f; attr[3] = 1.0f;
    /* Vertex 1: position (-0.5, -0.5, 0, 1) */
    attr[4] = -0.5f; attr[5] = -0.5f; attr[6] = 0.0f; attr[7] = 1.0f;
    /* Vertex 2: position (0.5, -0.5, 0, 1) */
    attr[8] = 0.5f; attr[9] = -0.5f; attr[10] = 0.0f; attr[11] = 1.0f;

    /* 
     * STAGE 3: TILER - Mark tiles as valid
     */
    uint32_t *job2 = (uint32_t *)((uint8_t*)cpu + 0x600);
    job2[4] = (4 << 1) | (1 << 16);  /* TILER */
    job2[8] = (uint32_t)(gva + 0x800);  /* polygon list */
    job2[12] = 0;

    /* Polygon list at 0x800 - minimal tile data */
    /* For now just mark tiles as processed */
    volatile uint32_t *poly = (volatile uint32_t *)((uint8_t*)cpu + 0x800);
    *poly = 0x00000001;  /* one tile processed */

    /* 
     * STAGE 4: FRAGMENT - Fill the framebuffer with color
     * This is the key stage that writes to color buffer
     */
    uint32_t *job3 = (uint32_t *)((uint8_t*)cpu + 0xA00);
    job3[4] = (5 << 1) | (1 << 16);  /* FRAGMENT */
    job3[8] = (uint32_t)(gva + 0xC00);  /* FBD */
    job3[12] = 0;

    /* FBD at 0xC00 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0xC00);
    fbd[0x80/4 + 0] = 256;    /* width */
    fbd[0x80/4 + 1] = 256;    /* height */
    fbd[0x80/4 + 2] = 0x2;    /* format: RGBA8 */
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0xD00);  /* RT */

    /* RT at 0xD00 - points to color buffer */
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0xD00);
    rt[0] = (uint32_t)(gva + 0x100);  /* color buffer */
    rt[2] = 256 * 4;  /* row stride */

    printf("Pipeline: WV(0x%llx) -> V(0x%llx) -> T(0x%llx) -> F(0x%llx)\n",
           (unsigned long long)gva, 
           (unsigned long long)(gva+0x200),
           (unsigned long long)(gva+0x600),
           (unsigned long long)(gva+0xA00));

    /* Submit 4-stage pipeline with dependencies */
    struct kbase_atom atoms[4] = {0};

    /* Stage 0: WRITE_VALUE */
    atoms[0].jc = gva;
    atoms[0].core_req = 0x203;  /* CS + CF */
    atoms[0].atom_number = 1;

    /* Stage 1: VERTEX - depends on WRITE_VALUE */
    atoms[1].jc = gva + 0x200;
    atoms[1].core_req = 0x008;  /* Vertex */
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 0;
    atoms[1].pre_dep_type[0] = 1;  /* job complete */

    /* Stage 2: TILER - depends on VERTEX */
    atoms[2].jc = gva + 0x600;
    atoms[2].core_req = 0x004;  /* Tiler */
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 1;
    atoms[2].pre_dep_type[0] = 1;

    /* Stage 3: FRAGMENT - depends on TILER */
    atoms[3].jc = gva + 0xA00;
    atoms[3].core_req = 0x003;  /* FS + CS - THE KEY! */
    atoms[3].atom_number = 4;
    atoms[3].pre_dep_atom[0] = 2;
    atoms[3].pre_dep_type[0] = 1;

    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &(struct { uint64_t addr; uint32_t nr, stride; }){
        .addr = (uint64_t)&atoms, .nr = 4, .stride = 72});

    printf("Submitted 4 atoms with dependencies...\n");

    usleep(500000);

    printf("\n=== RESULT ===\n");
    printf("Color buffer: 0x%08x\n", *color);

    if (*color != 0x00000000) {
        printf("\n*** GPU RENDERED! ***\n");
        printf("Color value: R=%d G=%d B=%d A=%d\n",
               (*color >> 0) & 0xFF,
               (*color >> 8) & 0xFF,
               (*color >> 16) & 0xFF,
               (*color >> 24) & 0xFF);
    } else {
        printf("\nColor still black - may need proper vertex/tiler setup\n");
    }

    read(fd, (uint8_t[24]){0}, 24);
    munmap(cpu, 16*4096);
    close(fd);
    return 0;
}