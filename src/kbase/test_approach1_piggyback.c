#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/*
 * Approach 1: Piggyback on Chrome's active GPU context.
 *
 * Strategy A: Open our own /dev/mali0 while Chrome is actively rendering
 *             (GPU power domain should be on).
 * Strategy B: Find Chrome's mali0 fd via /proc/PID/fd and use it directly
 *             via /proc/PID/fd/N (requires root).
 *
 * The hypothesis is that GPU power domain / GED is not active for our
 * standalone process, but is active when Chrome is rendering.
 */

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

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

static int find_chrome_mali_fd(pid_t *out_pid) {
    /* Search /proc for processes with mali0 open */
    DIR *proc = opendir("/proc");
    if (!proc) return -1;

    struct dirent *de;
    while ((de = readdir(proc)) != NULL) {
        pid_t pid = atoi(de->d_name);
        if (pid <= 0) continue;

        /* Check cmdline for chrome */
        char cmdpath[256];
        snprintf(cmdpath, sizeof(cmdpath), "/proc/%d/cmdline", pid);
        FILE *f = fopen(cmdpath, "r");
        if (!f) continue;
        char cmd[256] = {0};
        fread(cmd, 1, sizeof(cmd)-1, f);
        fclose(f);

        if (strstr(cmd, "chrome") == NULL && strstr(cmd, "Chrome") == NULL)
            continue;

        /* Check fds for mali0 */
        char fddir[256];
        snprintf(fddir, sizeof(fddir), "/proc/%d/fd", pid);
        DIR *fds = opendir(fddir);
        if (!fds) continue;

        struct dirent *fde;
        while ((fde = readdir(fds)) != NULL) {
            char linkpath[512], target[256];
            snprintf(linkpath, sizeof(linkpath), "%s/%s", fddir, fde->d_name);
            ssize_t len = readlink(linkpath, target, sizeof(target)-1);
            if (len <= 0) continue;
            target[len] = 0;
            if (strstr(target, "mali0")) {
                int fdnum = atoi(fde->d_name);
                printf("[FOUND] Chrome PID %d has mali0 at fd %d\n", pid, fdnum);
                *out_pid = pid;
                closedir(fds);
                closedir(proc);
                return fdnum;
            }
        }
        closedir(fds);
    }
    closedir(proc);
    return -1;
}

static int test_own_fd(void) {
    printf("\n=== Strategy A: Own fd while Chrome is active ===\n");

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return -1; }

    struct { uint16_t major, minor; } ver = {11, 13};
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
        perror("VERSION_CHECK"); close(fd); return -1;
    }

    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) {
        perror("SET_FLAGS"); close(fd); return -1;
    }

    uint64_t mem[4] = {2, 2, 0, 0xF};
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) {
        perror("MEM_ALLOC"); close(fd); return -1;
    }
    uint64_t gpu_va = mem[1];
    printf("[OK] gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    void *cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) { perror("mmap"); close(fd); return -1; }

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
    atom.core_req = 0x203;
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)(uintptr_t)&atom, .nr_atoms = 1, .stride = 72
    };

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);

    usleep(500000);

    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    uint32_t ec = 0;
    memcpy(&ec, ev, 4);

    uint32_t result = *target;
    printf("  target=0x%08x event=0x%x %s\n", result, ec,
           result == 0xCAFEBABE ? "*** WRITE DETECTED ***" :
           result != 0xDEADBEEF ? "*** CHANGED ***" : "(unchanged)");

    munmap(cpu_mem, 8192);
    close(fd);
    return (result != 0xDEADBEEF) ? 1 : 0;
}

static int test_chrome_fd(pid_t chrome_pid, int chrome_fdnum) {
    printf("\n=== Strategy B: Use Chrome's mali fd via /proc ===\n");

    /* Open Chrome's mali fd through /proc */
    char fdpath[256];
    snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd/%d", chrome_pid, chrome_fdnum);
    int fd = open(fdpath, O_RDWR);
    if (fd < 0) {
        printf("[SKIP] Cannot open %s: %s\n", fdpath, strerror(errno));
        printf("  This is expected — Chrome's kctx is already set up.\n");
        printf("  Trying with O_RDONLY...\n");
        fd = open(fdpath, O_RDONLY);
        if (fd < 0) {
            printf("[SKIP] O_RDONLY also failed: %s\n", strerror(errno));
            return -1;
        }
    }
    printf("[OK] Opened Chrome's mali fd: %s => fd=%d\n", fdpath, fd);

    /* Try to alloc on Chrome's context */
    uint64_t mem[4] = {2, 2, 0, 0xF};
    int ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    if (ret < 0) {
        printf("[FAIL] MEM_ALLOC on Chrome fd: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    uint64_t gpu_va = mem[1];
    printf("[OK] MEM_ALLOC on Chrome context: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    void *cpu_mem = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_mem == MAP_FAILED) {
        printf("[FAIL] mmap on Chrome fd: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

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
    atom.core_req = 0x203;
    atom.atom_number = 1;

    struct { uint64_t addr; uint32_t nr_atoms; uint32_t stride; } submit = {
        .addr = (uint64_t)(uintptr_t)&atom, .nr_atoms = 1, .stride = 72
    };

    ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("[%s] JOB_SUBMIT on Chrome fd: ret=%d\n", ret >= 0 ? "OK" : "FAIL", ret);

    usleep(500000);

    uint8_t ev[24] = {0};
    read(fd, ev, sizeof(ev));
    uint32_t ec = 0;
    memcpy(&ec, ev, 4);

    uint32_t result = *target;
    printf("  target=0x%08x event=0x%x %s\n", result, ec,
           result == 0xCAFEBABE ? "*** WRITE DETECTED ***" :
           result != 0xDEADBEEF ? "*** CHANGED ***" : "(unchanged)");

    munmap(cpu_mem, 8192);
    close(fd);
    return (result != 0xDEADBEEF) ? 1 : 0;
}

int main(void) {
    printf("=== Approach 1: Piggyback on Chrome's GPU context ===\n");

    /* Check GPU power state */
    FILE *f = fopen("/sys/class/misc/mali0/device/power/runtime_status", "r");
    if (f) {
        char buf[64] = {0};
        fread(buf, 1, sizeof(buf)-1, f);
        fclose(f);
        printf("[INFO] GPU runtime_status: %s", buf);
    }

    f = fopen("/sys/kernel/ged/hal/gpu_power_state", "r");
    if (f) {
        char buf[64] = {0};
        fread(buf, 1, sizeof(buf)-1, f);
        fclose(f);
        printf("[INFO] GED gpu_power_state: %s", buf);
    }

    /* Find Chrome */
    pid_t chrome_pid = 0;
    int chrome_fd = find_chrome_mali_fd(&chrome_pid);

    /* Strategy A: own fd while Chrome is active */
    int result = test_own_fd();
    if (result > 0) {
        printf("\n*** Strategy A SUCCESS ***\n");
        return 0;
    }

    /* Strategy B: use Chrome's fd */
    if (chrome_fd >= 0) {
        result = test_chrome_fd(chrome_pid, chrome_fd);
        if (result > 0) {
            printf("\n*** Strategy B SUCCESS ***\n");
            return 0;
        }
    } else {
        printf("\n[SKIP] Strategy B: No Chrome process with mali0 fd found\n");
        printf("  Make sure Chrome is open and rendering something.\n");
    }

    printf("\n=== Both strategies failed ===\n");
    return 1;
}
