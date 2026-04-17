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

struct base_dependency {
    uint8_t atom_id;
    uint8_t dep_type;
} __attribute__((packed));

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
    printf("=== Test: 1 Atom with Correct Struct ===\n\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});

    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];
    memset(cpu, 0, 4*4096);

    /* Real target address (not colliding with job descriptor) */
    volatile uint32_t *target1 = (volatile uint32_t *)((uint8_t*)cpu + 0x100);
    *target1 = 0xAAAAAAAA;

    /* Build Valhall WRITE_VALUE descriptor at offset 0x000 */
    uint32_t *job1 = (uint32_t *)((uint8_t*)cpu + 0x000);
    job1[4] = (2 << 1) | (1 << 16);      /* Control word: Type=2(WRITE_VALUE), Index=1 */
    job1[8] = (uint32_t)(gva + 0x100);   /* Target VA low */
    job1[9] = 0;                         /* Target VA high */
    job1[10] = 6;                        /* Type: 6 = Immediate32 */
    job1[12] = 0x11111111;               /* Immediate value */

    /* Build atom */
    struct kbase_atom_mtk atoms[1] = {0};
    atoms[0].jc = gva + 0x000;
    atoms[0].core_req = 0x002; /* BASE_JD_REQ_CS */
    atoms[0].atom_number = 1;
    atoms[0].jobslot = 1;      /* Valhall Job Manager uses jobslot 1 or 2 typically */
    atoms[0].frame_nr = 1;

    struct { uint64_t addr; uint32_t nr, stride; } submit;
    submit.addr = (uint64_t)atoms;
    submit.nr = 1;
    submit.stride = sizeof(struct kbase_atom_mtk);

    printf("Submitting 1 atom...\n");
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret != 0) {
        printf("Submit failed: %d (%s)\n", errno, strerror(errno));
    } else {
        printf("Submit accepted.\n");
    }

    /* Wait and drain events non-blocking */
    usleep(100000);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    uint8_t ev[24] = {0};
    while (read(fd, ev, 24) == 24) {
        printf("Got event: event_code=0x%02x atom=%d\n", *(uint32_t*)ev, ev[4]);
    }

    printf("\nResults:\n");
    printf("target1: 0x%08x\n", *target1);

    if (*target1 == 0x11111111) {
        printf("\n*** SUCCESS! GPU executed! ***\n");
    } else {
        printf("\n*** FAILED! GPU did not execute! ***\n");
    }

    munmap(cpu, 4*4096);
    close(fd);
    return 0;
}
