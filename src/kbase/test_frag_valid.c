#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

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

int main(void) {
    printf("=== Test: Valid Fragment Job ===\n");
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0x200F}; /* SAME_VA + CPU/GPU RW */
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) != 0) return 1;
    
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    memset(cpu, 0, 4*4096);
    uint64_t gva = (uint64_t)cpu;

    /* Build Framebuffer Descriptor at 0x800 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x800);
    /* FBD is 128 bytes total (64 params + 64 padding). Then Render Targets follow. */
    fbd[8] = 255 | (255 << 16);  /* Width/Height - 1 */
    fbd[9] = 0;                  /* Bound Min X/Y */
    fbd[10] = 255 | (255 << 16); /* Bound Max X/Y */
    /* Sample count=1 (0), Pattern=0, Tie-Break=0, Eff Tile Size=0, Render Target Count-1 = 0 */
    /* We just set Render Target Count to 1 (which means bits 19-22 = 0) */
    fbd[11] = 0; 
    
    /* RT0 is at 0x800 + 128 = 0x880 */
    uint32_t *rt0 = (uint32_t *)((uint8_t*)cpu + 0x880);
    rt0[0] = 0; /* Internal Format, Mime, etc. Let's just leave it 0 (R8G8B8A8 usually) */
    rt0[2] = 0; /* Write Enable / Clear */
    rt0[4] = (uint32_t)(gva + 0x1000); /* Buffer Base Low */
    rt0[5] = (uint32_t)((gva + 0x1000) >> 32); /* Buffer Base High */
    rt0[8] = 256 * 4; /* Row Stride */

    /* Pointer to FBD: Shift right by 6 and set Type=1 (bit 0) */
    uint64_t fbd_ptr = (gva + 0x800);
    uint64_t fbd_desc = (fbd_ptr >> 6) << 6; /* Bits [63:6] are Pointer >> 6 */
    fbd_desc |= 1; /* Type = 1 */
    fbd_desc |= (0 << 2); /* Render target count - 1 = 0 */

    /* Build Fragment Job at 0x200 */
    uint32_t *job_frag = (uint32_t *)((uint8_t*)cpu + 0x200);
    job_frag[4] = (9 << 1) | (3 << 16); // Type 9 = Fragment
    
    /* Fragment Payload */
    job_frag[8]  = 0; /* Bound Min X, Y */
    job_frag[9]  = 255 | (255 << 16); /* Bound Max X, Y */
    job_frag[10] = (uint32_t)fbd_desc; /* Framebuffer desc */
    job_frag[11] = (uint32_t)(fbd_desc >> 32);
    /* Tile Enable Map = 0 */

    struct kbase_atom_mtk atoms[1];
    memset(atoms, 0, sizeof(atoms));
    atoms[0].jc = gva + 0x200;
    atoms[0].core_req = 0x49; /* FS + CF + COH */
    atoms[0].atom_number = 1;
    atoms[0].jobslot = 0;     /* Fragment goes to slot 0 */
    atoms[0].frame_nr = 1;

    msync(cpu, 4*4096, MS_SYNC);

    struct { uint64_t addr; uint32_t nr, stride; } submit = { 
        (uint64_t)atoms, 1, sizeof(struct kbase_atom_mtk) 
    };

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret != 0) return 1;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    uint8_t ev[24] = {0};
    int attempts = 40;
    while (attempts--) {
        if (read(fd, ev, 24) == 24) {
            uint32_t code = *(uint32_t*)ev;
            printf("Got event: code=0x%02x atom=%d\n", code, ev[4]);
            break;
        }
        usleep(50000);
    }
    
    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}
