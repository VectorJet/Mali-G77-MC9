#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS    _IOC(_IOC_WRITE, 0x80, 1, 4)

int try_flag(uint32_t flag) {
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) return -1;
    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    int ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flag);
    int e = errno;
    close(fd);
    return ret == 0 ? 0 : -e;
}

/* Test tiler job submission (core_req=0x44) with a given SET_FLAGS value.
 * Returns 0 if JOB_SUBMIT succeeds (doesn't fault), -errno otherwise. */
#define KBASE_IOCTL_JOB_SUBMIT _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define BASE_MEM_PROT_CPU_RD (1ULL<<0)
#define BASE_MEM_PROT_CPU_WR (1ULL<<1)
#define BASE_MEM_PROT_GPU_RD (1ULL<<2)
#define BASE_MEM_PROT_GPU_WR (1ULL<<3)
#define BASE_MEM_SAME_VA     (1ULL<<13)

#pragma pack(push,1)
struct kbase_atom {
    uint64_t seq_nr, jc, udata[2], extres_list;
    uint16_t nr_extres; uint8_t jit_id[2], pre_dep_atom[2], pre_dep_type[2];
    uint8_t atom_number, prio, device_nr, jobslot;
    uint32_t core_req; uint8_t renderpass_id, padding[7]; uint32_t frame_nr;
};
#pragma pack(pop)

int try_tiler_with_flag(uint32_t flag) {
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) return -errno;
    struct { uint16_t major, minor; } ver = {11, 13};
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flag) < 0) { close(fd); return -EINVAL; }

    uint64_t mem[4] = {1, 1, 0,
        BASE_MEM_PROT_CPU_RD|BASE_MEM_PROT_CPU_WR|
        BASE_MEM_PROT_GPU_RD|BASE_MEM_PROT_GPU_WR|BASE_MEM_SAME_VA};
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) { close(fd); return -ENOMEM; }
    uint64_t gpu_va = mem[1];

    struct kbase_atom atom = {0};
    atom.jc = gpu_va;
    atom.core_req = (1<<2)|(1<<6); /* BASE_JD_REQ_T | BASE_JD_REQ_COHERENT_GROUP */
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr, stride; } sub = {(uint64_t)&atom, 1, 72};
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
    int e = errno;
    usleep(80000);
    /* read event to get completion status */
    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    uint32_t ev_code = *(uint32_t*)ev;
    close(fd);
    printf("  flags=0x%02x: submit=%s ev_code=0x%08x\n",
        flag, ret==0?"OK":strerror(e), ev_code);
    return ret == 0 ? 0 : -e;
}

int main(void) {
    uint32_t valid[] = {0x0, 0x2, 0x8, 0x10, 0x20, 0x40,
                        0x2|0x8, 0x2|0x40, 0x8|0x40, 0x2|0x8|0x40};
    printf("=== Tiler job (core_req=0x44) vs SET_FLAGS ===\n");
    for (int i = 0; i < (int)(sizeof(valid)/sizeof(valid[0])); i++)
        try_tiler_with_flag(valid[i]);
    return 0;
}
