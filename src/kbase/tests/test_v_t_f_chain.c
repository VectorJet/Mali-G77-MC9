#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
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
    printf("=== 3-Atom: VERTEX -> TILER -> FRAGMENT ===\n\n");
    fflush(stdout);

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open mali0"); return 1; }
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    /* 3 separate allocations (same GVA is fine per debug_multi_atom) */
    uint64_t vmem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, vmem);
    void *vbuf = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, vmem[1]);
    memset(vbuf, 0, 8192);

    uint64_t tmem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, tmem);
    void *tbuf = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, tmem[1]);
    memset(tbuf, 0, 8192);

    uint64_t fmem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, fmem);
    void *fbuf = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, fmem[1]);
    memset(fbuf, 0, 4*4096);

    /* Color target in fragment buffer */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)fbuf + 0x30);
    *color = 0xDEADBEEF;
    printf("Color at fbuf+0x30 = 0xDEADBEEF\n");

    /* VERTEX job at vbuf offset 0 */
    uint32_t *vjob = (uint32_t *)vbuf;
    vjob[4] = (3 << 1) | (1 << 16);  /* type=3 VERTEX */

    /* TILER job at tbuf offset 0 */
    uint32_t *tjob = (uint32_t *)tbuf;
    tjob[4] = (4 << 1) | (1 << 16);  /* type=4 TILER */

    /* FRAGMENT job at fbuf offset 0 */
    uint32_t *fjob = (uint32_t *)fbuf;
    fjob[4] = (5 << 1) | (1 << 16);     /* type=5 FRAGMENT */
    fjob[8] = (uint32_t)(fmem[1] + 0x100);  /* FBD pointer */

    /* FBD at fbuf+0x100 */
    uint32_t *fbd = (uint32_t *)((uint8_t*)fbuf + 0x100);
    fbd[0x80/4 + 0] = 256;
    fbd[0x80/4 + 1] = 256;
    fbd[0x80/4 + 8] = (uint32_t)(fmem[1] + 0x200);  /* RT */

    /* RT at fbuf+0x200 */
    uint32_t *rt = (uint32_t *)((uint8_t*)fbuf + 0x200);
    rt[0] = (uint32_t)(fmem[1] + 0x30);  /* color addr */
    rt[2] = 4;

    printf("V(gva=0x%llx) -> T(gva=0x%llx) -> F(gva=0x%llx)\n",
           (unsigned long long)vmem[1], (unsigned long long)tmem[1],
           (unsigned long long)fmem[1]);
    fflush(stdout);

    /* 3 atoms, stride=72 (proven working pattern) */
    struct kbase_atom atoms[3] = {0};

    atoms[0].jc = vmem[1];
    atoms[0].core_req = 0x008;  /* V */
    atoms[0].atom_number = 1;

    atoms[1].jc = tmem[1];
    atoms[1].core_req = 0x004;  /* T */
    atoms[1].atom_number = 2;
    atoms[1].pre_dep_atom[0] = 1;
    atoms[1].pre_dep_type[0] = 1;

    atoms[2].jc = fmem[1];
    atoms[2].core_req = 0x003;  /* FS+CS */
    atoms[2].atom_number = 3;
    atoms[2].pre_dep_atom[0] = 2;
    atoms[2].pre_dep_type[0] = 1;

    errno = 0;
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT,
        &(struct { uint64_t addr; uint32_t nr, stride; }){
            .addr = (uint64_t)&atoms, .nr = 3, .stride = 72});
    printf("JOB_SUBMIT ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
    fflush(stdout);

    /* Poll with timeout - don't hang */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int evcount = 0;
    while (evcount < 3) {
        int pr = poll(&pfd, 1, 2000);
        if (pr <= 0) {
            printf("TIMEOUT waiting for event %d\n", evcount);
            break;
        }
        uint8_t ev[24];
        int n = read(fd, ev, sizeof(ev));
        printf("Event[%d]: code=0x%08x atom=%u (%d bytes)\n",
               evcount, *(uint32_t*)ev, ev[4], n);
        evcount++;
    }

    printf("\nColor: 0x%08x (was 0xDEADBEEF)\n", *color);
    if (*color != 0xDEADBEEF)
        printf("*** SUCCESS! GPU modified color buffer ***\n");
    else
        printf("No change\n");

    munmap(vbuf, 8192);
    munmap(tbuf, 8192);
    munmap(fbuf, 4*4096);
    close(fd);
    return 0;
}
