#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_0x19           _IOC(_IOC_WRITE, 0x80, 0x19, 4)
#define KBASE_IOCTL_0x1B           _IOC(_IOC_WRITE, 0x80, 0x1B, 16)

/*
 * Approach 2: Replay Chrome's exact ioctl sequence.
 * 
 * Chrome's spy capture showed:
 *   - ioctl 0x19 with values like 0x131, 0x152, 0x180 BEFORE submit
 *   - ioctl 0x1b with {pointer, int, 0} AFTER submit
 *   - stride = 72
 *   - core_req = 0x203 (CS | CF | ???)
 *
 * We test multiple 0x19 values that Chrome actually used.
 */

struct kbase_atom {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint16_t jit_id[2];
    uint8_t pre_dep_atom[2];
    uint8_t pre_dep_type[2];
    uint8_t atom_number;
    uint8_t prio;
    uint8_t device_nr;
    uint8_t jobslot;
    uint32_t core_req;
    uint8_t renderpass_id;
    uint8_t padding[7];
} __attribute__((packed));

static int try_submit(int fd, uint64_t gpu_va, void *cpu_mem, 
                      uint32_t ioctl19_val, uint32_t core_req,
                      const char *label)
{
    int ret;
    volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 4096);

    /* Reset target */
    *target = 0xDEADBEEF;

    /* Build WRITE_VALUE job descriptor at start of buffer */
    uint32_t *job = (uint32_t *)cpu_mem;
    memset(job, 0, 128);
    /* Control: type=WRITE_VALUE(2), index=1 */
    job[4] = (2 << 1) | (1 << 16);
    /* Target address: second page of our allocation */
    uint64_t target_gpu = gpu_va + 4096;
    job[8] = (uint32_t)(target_gpu & 0xFFFFFFFF);
    job[9] = (uint32_t)(target_gpu >> 32);
    /* Write type: Immediate32 = 6 */
    job[10] = 6;
    /* Value to write */
    job[12] = 0xCAFEBABE;

    /* Call 0x19 BEFORE submit, like Chrome does */
    ret = ioctl(fd, KBASE_IOCTL_0x19, &ioctl19_val);
    if (ret < 0) {
        printf("  [%s] 0x19(0x%x): FAIL errno=%d\n", label, ioctl19_val, errno);
    }

    /* Build atom */
    struct kbase_atom atom = {0};
    atom.jc = gpu_va;
    atom.core_req = core_req;
    atom.atom_number = 1;
    atom.prio = 0;

    /* Submit */
    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)(uintptr_t)&atom,
        .nr_atoms = 1,
        .stride = 72
    };
    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret < 0) {
        printf("  [%s] JOB_SUBMIT: FAIL errno=%d\n", label, errno);
        return -1;
    }

    /* Call 0x1b AFTER submit, like Chrome does */
    struct { uint64_t ptr; uint32_t arg0; uint32_t arg1; } ctl_1b = {0};
    ctl_1b.ptr = (uint64_t)(uintptr_t)cpu_mem; /* use our buffer as pointer */
    ctl_1b.arg0 = 0x148f; /* Chrome observed value */
    ctl_1b.arg1 = 0;
    ret = ioctl(fd, KBASE_IOCTL_0x1B, &ctl_1b);
    if (ret < 0) {
        /* Don't abort — this might not be required */
    }

    /* Wait for GPU */
    usleep(200000);

    /* Read event */
    uint8_t ev[24] = {0};
    ssize_t ev_bytes = read(fd, ev, sizeof(ev));
    uint32_t event_code = 0;
    if (ev_bytes >= 4) {
        memcpy(&event_code, ev, 4);
    }

    uint32_t result = *target;
    printf("  [%s] 0x19=0x%03x core_req=0x%x => target=0x%08x event=0x%x %s\n",
           label, ioctl19_val, core_req, result, event_code,
           result == 0xCAFEBABE ? "*** WRITE DETECTED ***" :
           result != 0xDEADBEEF ? "*** CHANGED ***" : "(unchanged)");

    return (result != 0xDEADBEEF) ? 1 : 0;
}

int main(void) {
    int fd, ret;

    printf("=== Approach 2: Replay Chrome ioctl sequence ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    /* VERSION_CHECK */
    struct { uint16_t major, minor; } ver = {11, 13};
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
        perror("VERSION_CHECK"); close(fd); return 1;
    }
    printf("[OK] VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

    /* SET_FLAGS */
    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) {
        perror("SET_FLAGS"); close(fd); return 1;
    }
    printf("[OK] SET_FLAGS\n");

    /* MEM_ALLOC: 2 pages */
    uint64_t mem[4] = {2, 2, 0, 0xF};
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) {
        perror("MEM_ALLOC"); close(fd); return 1;
    }
    uint64_t gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx flags_out=0x%llx\n",
           (unsigned long long)gpu_va, (unsigned long long)mem[0]);

    /* mmap */
    void *cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    printf("[OK] mmap: %p\n\n", cpu_mem);

    /* Chrome-observed 0x19 values */
    uint32_t chrome_0x19[] = {0x108, 0x10e, 0x111, 0x131, 0x13f, 0x14d, 0x152, 0x153, 0x161, 0x173, 0x180};
    int n19 = sizeof(chrome_0x19) / sizeof(chrome_0x19[0]);

    /* Chrome-observed core_req values */
    uint32_t chrome_core_reqs[] = {0x203, 0x209, 0x001, 0x04e, 0x20a};
    int ncr = sizeof(chrome_core_reqs) / sizeof(chrome_core_reqs[0]);

    printf("--- Test: Chrome 0x19 values with core_req=0x203 ---\n");
    for (int i = 0; i < n19; i++) {
        char label[16];
        snprintf(label, sizeof(label), "T%02d", i);
        if (try_submit(fd, gpu_va, cpu_mem, chrome_0x19[i], 0x203, label) > 0)
            goto success;
    }

    printf("\n--- Test: Chrome core_req values with 0x19=0x180 ---\n");
    for (int i = 0; i < ncr; i++) {
        char label[16];
        snprintf(label, sizeof(label), "CR%d", i);
        if (try_submit(fd, gpu_va, cpu_mem, 0x180, chrome_core_reqs[i], label) > 0)
            goto success;
    }

    printf("\n--- Test: No 0x19, just different core_reqs ---\n");
    for (int i = 0; i < ncr; i++) {
        volatile uint32_t *target = (volatile uint32_t *)((uint8_t *)cpu_mem + 4096);
        *target = 0xDEADBEEF;

        uint32_t *job = (uint32_t *)cpu_mem;
        memset(job, 0, 128);
        job[4] = (2 << 1) | (1 << 16);
        uint64_t tva = gpu_va + 4096;
        job[8] = (uint32_t)(tva & 0xFFFFFFFF);
        job[9] = (uint32_t)(tva >> 32);
        job[10] = 6;
        job[12] = 0xCAFEBABE;

        struct kbase_atom atom = {0};
        atom.jc = gpu_va;
        atom.core_req = chrome_core_reqs[i];
        atom.atom_number = 1;

        struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
            .addr = (uint64_t)(uintptr_t)&atom, .nr_atoms = 1, .stride = 72
        };
        ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
        usleep(200000);
        uint8_t ev[24] = {0};
        read(fd, ev, sizeof(ev));
        uint32_t ec = 0;
        memcpy(&ec, ev, 4);
        uint32_t result = *target;
        printf("  [NC%d] core_req=0x%x => target=0x%08x event=0x%x %s\n",
               i, chrome_core_reqs[i], result, ec,
               result == 0xCAFEBABE ? "*** WRITE ***" :
               result != 0xDEADBEEF ? "*** CHANGED ***" : "(unchanged)");
        if (result != 0xDEADBEEF) goto success;
    }

    printf("\nAll tests: target unchanged. GPU did not execute.\n");
    munmap(cpu_mem, 8192);
    close(fd);
    return 1;

success:
    printf("\n*** GPU WROTE TO MEMORY! ***\n");
    munmap(cpu_mem, 8192);
    close(fd);
    return 0;
}
