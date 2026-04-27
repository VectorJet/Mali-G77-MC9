#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <signal.h>

volatile int alarm_triggered = 0;

void alarm_handler(int sig) {
    alarm_triggered = 1;
}

void print_dmesg(void) {
    FILE *f = popen("dmesg | tail -30", "r");
    if (f) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), f)) {
            fputs(buf, stdout);
        }
        pclose(f);
    }
}

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define BASE_MEM_PROT_GPU_RDWR 0xF

struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres, jit_id[2];
    uint8_t pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req;
    uint8_t renderpass_id, padding[7];
} __attribute__((packed));

int main(void) {
    printf("=== Test Fragment Pixel Output (2x2 clear, 4x4 framebuffer) ===\n\n");
    fflush(stdout);

    signal(SIGALRM, alarm_handler);

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mali0");
        return 1;
    }

    printf("Init: VERSION_CHECK...\n");
    int r = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    printf("  result=%d\n", r);

    printf("Init: SET_FLAGS...\n");
    r = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    printf("  result=%d\n", r);

    printf("Allocating memory...\n");
    uint64_t mem[4] = {4, 4, 0, BASE_MEM_PROT_GPU_RDWR};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
    uint64_t gva = mem[1];

    memset(cpu, 0, 4*4096);

    printf("Setting up color buffer at offset 0x40 (4x4 = 16 pixels)...\n");
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t*)cpu + 0x40);
    for (int i = 0; i < 4*4; i++) {
        color[i] = 0xAABBCCDD;
    }

    printf("Setting up polygon list at offset 0x100...\n");
    uint32_t *poly = (uint32_t *)((uint8_t*)cpu + 0x100);
    poly[0] = 1;

    printf("Setting up FBD at offset 0x200...\n");
    uint32_t *fbd = (uint32_t *)((uint8_t*)cpu + 0x200);
    fbd[0x80/4 + 0] = 4;
    fbd[0x80/4 + 1] = 4;
    fbd[0x80/4 + 8] = (uint32_t)(gva + 0x300);

    printf("Setting up render target at offset 0x300...\n");
    uint32_t *rt = (uint32_t *)((uint8_t*)cpu + 0x300);
    rt[0] = (uint32_t)(gva + 0x40);
    rt[2] = 4 * 4;

    printf("Setting up FRAGMENT job at offset 0x000...\n");
    uint32_t *job = (uint32_t *)cpu;
    job[4] = (5 << 1) | (1 << 16);
    job[8] = (uint32_t)(gva + 0x200);
    job[12] = 0;

    printf("Submitting FRAGMENT job...\n");
    struct kbase_atom atom = {0};
    atom.jc = gva;
    atom.core_req = 0x003;
    atom.atom_number = 1;

    alarm(5);
    struct { uint64_t addr; uint32_t nr, stride; } submit = {
        .addr = (uint64_t)&atom, .nr = 1, .stride = 72
    };
    r = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("  ioctl result=%d\n", r);

    if (alarm_triggered) {
        printf("\n*** TIMEOUT - GPU appears hung! ***\n");
        printf("dmesg output:\n");
        print_dmesg();
        alarm_triggered = 0;
        munmap(cpu, 4*4096);
        close(fd);
        return 1;
    }

    usleep(100000);

    printf("Reading event...\n");
    uint8_t event[24];
    int er = read(fd, event, 24);
    printf("  read result=%d\n", er);

    printf("\nColor buffer after render:\n");
    for (int y = 0; y < 4; y++) {
        printf("Row %d: ", y);
        for (int x = 0; x < 4; x++) {
            printf("%08x ", color[y*4 + x]);
        }
        printf("\n");
    }

    int modified = 0;
    int zero_count = 0;
    for (int i = 0; i < 4*4; i++) {
        if (color[i] != 0xAABBCCDD) modified++;
        if (color[i] == 0x00000000) zero_count++;
    }
    printf("\nModified pixels: %d / 16\n", modified);
    printf("Zero pixels: %d / 16\n", zero_count);

    if (modified == 0) {
        printf("\n*** WARNING: No pixels were modified! ***\n");
        printf("dmesg output:\n");
        print_dmesg();
    }

    munmap(cpu, 4*4096);
    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}