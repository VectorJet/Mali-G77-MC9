/*
 * Mali kbase r49 - Full working test
 *
 * Correct ioctl mapping:
 *   Magic = 0x80
 *   VERSION_CHECK = _IOWR(0x80, 0, 4)    // u16 major, u16 minor
 *   SET_FLAGS     = _IOW(0x80, 1, 4)     // u32 create_flags
 *   JOB_SUBMIT    = _IOW(0x80, 2, 16)    // ptr to atoms, count, stride
 *   GET_GPUPROPS  = _IOW(0x80, 3, 16)    // buffer ptr, size, flags
 *   MEM_ALLOC     = _IOWR(0x80, 5, 32)   // va/commit/ext/flags -> flags/gpu_va
 *   MEM_FREE      = _IOW(0x80, 7, 8)     // gpu_addr
 *
 *   mmap: offset = gpu_va (raw, not page-shifted)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* ioctl command builders */
#define KBASE_IOCTL_VERSION_CHECK    _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS        _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT       _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_GET_GPUPROPS     _IOC(_IOC_WRITE, 0x80, 3, 16)
#define KBASE_IOCTL_MEM_ALLOC        _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_MEM_FREE         _IOC(_IOC_WRITE, 0x80, 7, 8)

/* Memory flags */
#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)

/*
 * base_jd_atom_v2 - job atom for submission
 *
 * JOB_SUBMIT takes a pointer to an array of these via:
 *   struct { u64 addr; u32 nr_atoms; u32 stride; }
 *
 * We need to find the correct atom struct size.
 * Standard kbase r49 atom is 64 bytes.
 */

/* Dependency struct */
struct base_dependency {
    uint8_t atom_id;
    uint8_t dep_type;
};

/* Job atom - 64 byte version (typical for r49) */
struct base_jd_atom_v2 {
    uint64_t jc;                           /* 0: job chain GPU addr */
    uint64_t udata[2];                     /* 8: user data */
    uint64_t extres_list;                   /* 24: external resource list */
    uint16_t nr_extres;                     /* 32: number of external resources */
    uint16_t compat_core_req;               /* 34: legacy core requirements */
    struct base_dependency pre_dep[2];      /* 36: dependencies (2x2 = 4 bytes) */
    uint16_t atom_number;                   /* 40: unique atom ID */
    uint8_t  prio;                          /* 42: priority */
    uint8_t  device_nr;                     /* 43: core group */
    uint32_t core_req;                      /* 44: core requirements */
    /* Total: 48 bytes */
};

int main(void) {
    int fd, ret;

    printf("=== Mali kbase r49 Full Test ===\n\n");

    fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    printf("[OK] Opened /dev/mali0 (fd=%d)\n", fd);

    /* VERSION_CHECK */
    struct { uint16_t major, minor; } ver = {11, 13};
    ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    printf("[%s] VERSION_CHECK: major=%u minor=%u\n",
           ret >= 0 ? "OK" : "FAIL", ver.major, ver.minor);
    if (ret < 0) { close(fd); return 1; }

    /* SET_FLAGS */
    uint32_t create_flags = 0;
    ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    printf("[%s] SET_FLAGS\n", ret >= 0 ? "OK" : "FAIL");
    if (ret < 0) { close(fd); return 1; }

    /* GET_GPUPROPS */
    struct { uint64_t buffer; uint32_t size; uint32_t flags; } props = {0};
    ret = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &props);
    if (ret > 0) {
        int props_size = ret;
        uint8_t *propbuf = calloc(1, props_size);
        struct { uint64_t buffer; uint32_t size; uint32_t flags; } props2 = {
            .buffer = (uint64_t)(uintptr_t)propbuf,
            .size = props_size,
            .flags = 0
        };
        ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &props2);
        printf("[OK] GET_GPUPROPS: %d bytes\n", props_size);

        /* Parse GPU ID from props */
        /* Format: series of (size:u32, key:u32, value:varies) entries */
        int off = 0;
        while (off + 8 <= props_size) {
            uint32_t entry_size = *(uint32_t *)(propbuf + off);
            uint32_t key = *(uint32_t *)(propbuf + off + 4);
            if (entry_size <= 4) break;
            uint32_t val_size = entry_size - 4;
            if (off + 4 + entry_size > props_size) break;
            if (val_size == 4) {
                uint32_t val = *(uint32_t *)(propbuf + off + 8);
                if (key == 1) printf("  PRODUCT_ID: 0x%x\n", val);
                if (key == 55) printf("  GPU_ID: 0x%x\n", val);
            }
            off += 4 + entry_size;
        }
        free(propbuf);
    }

    /* MEM_ALLOC - allocate 1 page for job chain */
    uint64_t mem[4] = {0};
    mem[0] = 1;  /* va_pages */
    mem[1] = 1;  /* commit_pages */
    mem[2] = 0;  /* extension */
    mem[3] = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
             BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
    ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);
    if (ret < 0) {
        printf("[FAIL] MEM_ALLOC: errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    uint64_t gpu_va = mem[1];
    printf("[OK] MEM_ALLOC: gpu_va=0x%llx\n", (unsigned long long)gpu_va);

    /* mmap the allocation */
    void *cpu_map = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
    if (cpu_map == MAP_FAILED) {
        printf("[FAIL] mmap: errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    printf("[OK] mmap: cpu=%p gpu_va=0x%llx\n", cpu_map, (unsigned long long)gpu_va);

    /* Write a null job header (dependency-only job, jc=0 is acceptable for DEP) */
    memset(cpu_map, 0, 4096);

    /* JOB_SUBMIT - try submitting a null/DEP job */
    printf("\n--- JOB_SUBMIT tests ---\n");

    /* First, find correct atom stride by trying different sizes */
    int atom_sizes[] = {40, 48, 56, 64, 72, 80};
    for (int i = 0; i < 6; i++) {
        uint8_t atom_buf[128] = {0};
        struct base_jd_atom_v2 *atom = (struct base_jd_atom_v2 *)atom_buf;
        atom->jc = 0;          /* NULL jc for DEP job */
        atom->core_req = 0;    /* DEP = no hardware job */
        atom->prio = 0;        /* medium priority */
        atom->atom_number = 1; /* atom ID 1 */

        struct {
            uint64_t addr;
            uint32_t nr_atoms;
            uint32_t stride;
        } submit = {
            .addr = (uint64_t)(uintptr_t)atom_buf,
            .nr_atoms = 1,
            .stride = atom_sizes[i]
        };

        errno = 0;
        ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
        int e = errno;
        printf("  stride=%d: ret=%d errno=%d (%s)\n",
               atom_sizes[i], ret, e, strerror(e));

        /* If it worked or got EINVAL (not EFAULT/ENOTTY), the stride is valid */
        if (ret >= 0) {
            printf("  >>> JOB_SUBMIT SUCCEEDED with stride=%d! <<<\n", atom_sizes[i]);
            break;
        }
    }

    /* MEM_FREE */
    uint64_t free_addr = gpu_va;
    munmap(cpu_map, 4096);
    ret = ioctl(fd, KBASE_IOCTL_MEM_FREE, &free_addr);
    printf("\n[%s] MEM_FREE: gpu_va=0x%llx\n",
           ret >= 0 ? "OK" : "FAIL", (unsigned long long)gpu_va);

    close(fd);
    printf("\n=== Done ===\n");
    return 0;
}
