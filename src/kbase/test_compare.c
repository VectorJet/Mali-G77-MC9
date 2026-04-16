#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

int main(void) {
    int fd = open("/dev/mali0", O_RDWR);
    ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &(uint16_t){11});
    ioctl(fd, KBASE_IOCTL_SET_FLAGS, &(uint32_t){0});
    
    /* Test 1: core_req=0 - working in test_verify_exec */
    printf("Test 1: core_req=0\n");
    uint64_t mem[4] = {2, 2, 0, 0xF};
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    void *cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    uint32_t *job = (uint32_t *)((char*)cpu + 0x100);
    job[4] = (2 << 1) | (1 << 16);  /* WRITE_VALUE, index=1 */
    job[8] = (uint32_t)(mem[1] + 0x200);  /* target */
    job[9] = 0;
    job[10] = 6;  /* immediate32 */
    job[12] = 0xCAFEBABE;
    volatile uint32_t *marker = (volatile uint32_t *)((char*)cpu + 0x200);
    *marker = 0xDEADBEEF;
    
    struct { uint64_t jc; uint32_t nr, stride; } s = {.jc = mem[1] + 0x100, .nr = 1, .stride = 72};
    *(uint32_t *)(&s + 8) = 0;  /* core_req at offset 44 */
    *(uint8_t *)(&s + 12) = 1;  /* atom_number at offset 32 */
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("  submit done\n");
    munmap(cpu, 8192);
    
    /* Test 2: core_req=0x203 - also working in test_verify_exec */
    printf("Test 2: core_req=0x203\n");
    mem[0] = 2; mem[1] = 2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    job = (uint32_t *)((char*)cpu + 0x100);
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x200);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    marker = (volatile uint32_t *)((char*)cpu + 0x200);
    *marker = 0xDEADBEEF;
    
    s.jc = mem[1] + 0x100;
    *(uint32_t *)(&s + 8) = 0x203;  /* core_req */
    *(uint8_t *)(&s + 12) = 1;
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("  submit done\n");
    munmap(cpu, 8192);
    
    /* Test 3: core_req=0x001 - this was hanging */
    printf("Test 3: core_req=0x001\n");
    mem[0] = 2; mem[1] = 2;
    ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    cpu = mmap(NULL, 8192, 3, 1, fd, mem[1]);
    job = (uint32_t *)((char*)cpu + 0x100);
    job[4] = (2 << 1) | (1 << 16);
    job[8] = (uint32_t)(mem[1] + 0x200);
    job[9] = 0;
    job[10] = 6;
    job[12] = 0xCAFEBABE;
    marker = (volatile uint32_t *)((char*)cpu + 0x200);
    *marker = 0xDEADBEEF;
    
    s.jc = mem[1] + 0x100;
    *(uint32_t *)(&s + 8) = 0x001;  /* core_req */
    *(uint8_t *)(&s + 12) = 1;
    
    ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &s);
    printf("  submit done\n");
    munmap(cpu, 8192);
    
    close(fd);
    printf("All done\n");
    return 0;
}