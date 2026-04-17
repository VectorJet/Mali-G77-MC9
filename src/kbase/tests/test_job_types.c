#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
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

int test_job(int fd, const char *name, uint32_t job_type, uint32_t core_req, int value) {
    uint64_t mem[4] = {4, 4, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 16384, 3, 1, fd, mem[1]);
    memset(cpu, 0, 16384);
    
    /* Job at offset 0x100 */
    uint32_t *job = (uint32_t *)((char*)cpu + 0x100);
    
    if (job_type == 2) {
        /* WRITE_VALUE */
        job[4] = (2 << 1) | (1 << 16);
        job[8] = (uint32_t)(mem[1] + 0x300);
        job[9] = 0;
        job[10] = 6;
        job[12] = value;
    } else if (job_type == 3) {
        /* COMPUTE SHADER */
        job[4] = (3 << 1) | (1 << 16);
        /* Compute payload at +0x20 */
        job[8] = mem[1] + 0x400;  /* uniform buffers */
        job[9] = 0;
        job[10] = mem[1] + 0x500;  /* textures */
        job[11] = 0;
        job[12] = mem[1] + 0x600;  /* shared memory */
        job[13] = 0;
        job[14] = 0;  /* group count X */
        job[15] = 0;
        job[16] = 0;  /* group count Y */
        job[17] = 0;
    } else if (job_type == 14) {
        /* FRAGMENT */
        job[4] = (14 << 1) | (1 << 16);
        /* Fragment payload at +0x20 */
        job[8] = 0;  /* draw descriptor */
        job[9] = 0;
        job[10] = 0;
        job[11] = 0;
        job[12] = mem[1] + 0x400;  /* position */
        job[13] = 0;
        job[14] = mem[1] + 0x500;  /* uniform buffers */
        job[15] = 0;
        job[16] = mem[1] + 0x600;  /* textures */
        job[17] = 0;
    }
    
    /* Marker at offset 0x300 */
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x300);
    *marker = value;
    
    struct kbase_atom atom = {0};
    atom.jc = mem[1] + 0x100;
    atom.core_req = core_req;
    atom.atom_number = 1;
    
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)&atom, .nr_atoms = 1, .stride = 72
    };
    
    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    
    /* Read event */
    uint8_t ev[24];
    read(fd, ev, sizeof(ev));
    
    printf("%s (type=%d, core_req=0x%03x): ret=%d, marker=0x%08x\n", 
           name, job_type, core_req, ret, *marker);
    
    munmap(cpu, 16384);
    return ret;
}

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    printf("=== Testing Different Job Types ===\n");
    
    /* WRITE_VALUE - baseline */
    test_job(fd, "WRITE_VALUE", 2, 0x010, 0x11111111);
    
    /* Compute shader */
    test_job(fd, "CS_002", 3, 0x002, 0x22222222);
    test_job(fd, "CS_202", 3, 0x202, 0x33333333);
    
    /* Fragment shader */
    test_job(fd, "FRAG_001", 14, 0x001, 0x44444444);
    test_job(fd, "FRAG_201", 14, 0x201, 0x55555555);
    
    /* Tiler */
    test_job(fd, "TILER_004", 13, 0x004, 0x66666666);
    test_job(fd, "TILER_204", 13, 0x204, 0x77777777);
    
    /* Vertex */
    test_job(fd, "VERT_008", 4, 0x008, 0x88888888);
    
    close(fd);
    printf("=== Done ===\n");
    return 0;
}