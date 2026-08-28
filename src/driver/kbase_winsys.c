#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kbase_winsys.h"

/* Kernel UAPI ioctl definitions */
#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_MEM_FREE       _IOC(_IOC_WRITE, 0x80, 7, 8)

#define BASE_CONTEXT_CREATE_KERNEL_FLAGS (1u << 0)
#define BASE_MEM_SAME_VA                 (1u << 16)

struct kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};

struct kbase_ioctl_set_flags {
    uint32_t create_flags;
};

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

struct kbase_ioctl_job_submit {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

/* Actual MTK r49 base_jd_event_v2 format:
 *   u32  event_code;   // 4 bytes at offset 0
 *   u8   atom_number;  // 1 byte  at offset 4
 *   u8   prio;         // 1 byte  at offset 5
 *   u8   jobslot;      // 1 byte  at offset 6
 *   u8   unused;       // 1 byte  at offset 7
 *   u64  timer;        // 8 bytes at offset 8
 *   u64  udata;        // 8 bytes at offset 16
 */
struct base_jd_event_v2 {
    uint32_t event_code;
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  jobslot;
    uint8_t  unused;
    uint64_t timer;
    uint64_t udata;
} __attribute__((packed));

struct kbase_dev {
    int fd;
    uint16_t major;
    uint16_t minor;
};

struct kbase_dev *kbase_dev_open(const char *dev_node) {
    const char *path = dev_node ? dev_node : "/dev/mali0";
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("kbase_dev_open: open /dev/mali0");
        return NULL;
    }

    uint16_t ver = 11;
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
        perror("kbase_dev_open: KBASE_IOCTL_VERSION_CHECK");
        close(fd);
        return NULL;
    }

    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) {
        perror("kbase_dev_open: KBASE_IOCTL_SET_FLAGS");
        close(fd);
        return NULL;
    }

    struct kbase_dev *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        close(fd);
        return NULL;
    }

    dev->fd = fd;
    dev->major = ver;
    dev->minor = 0;
    printf("kbase_winsys: initialized %s (kernel driver v%d)\n", path, ver);
    return dev;
}

void kbase_dev_close(struct kbase_dev *dev) {
    if (!dev) return;
    if (dev->fd >= 0) close(dev->fd);
    free(dev);
}

static uint64_t next_hint_va = 0x20000000ULL; /* Start at 512MB in low 32-bit space */

struct kbase_bo *kbase_bo_alloc(struct kbase_dev *dev, size_t size, uint32_t flags) {
    if (!dev || size == 0) return NULL;

    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
    uint64_t nr_pages = aligned_size / page_size;

    uint64_t mem_flags = 0x200F; /* CPU_RD|CPU_WR|GPU_RD|GPU_WR | SAME_VA(0x2000) */
    if (flags & KBASE_BO_PROT_EXEC) {
        mem_flags = 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x2000; /* 0x2017: CPU_RD|CPU_WR|GPU_RD|GPU_EX|SAME_VA */
    }

    uint64_t mem[4] = { nr_pages, nr_pages, 0, mem_flags };
    if (ioctl(dev->fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) {
        return NULL;
    }

    int prot = PROT_READ | PROT_WRITE;
    if (flags & KBASE_BO_PROT_EXEC) prot |= PROT_EXEC;

    void *hint = (void *)(uintptr_t)next_hint_va;
    next_hint_va += aligned_size;
    if (next_hint_va > 0x70000000ULL) {
        next_hint_va = 0x20000000ULL;
    }

    void *cpu_ptr = mmap(hint, aligned_size, prot, MAP_SHARED, dev->fd, (off_t)mem[1]);
    if (cpu_ptr == MAP_FAILED) {
        cpu_ptr = mmap(NULL, aligned_size, prot, MAP_SHARED, dev->fd, (off_t)mem[1]);
    }
    if (cpu_ptr == MAP_FAILED) {
        uint64_t free_va = mem[1];
        ioctl(dev->fd, KBASE_IOCTL_MEM_FREE, &free_va);
        return NULL;
    }

    struct kbase_bo *bo = calloc(1, sizeof(*bo));
    if (!bo) {
        munmap(cpu_ptr, aligned_size);
        uint64_t free_va = mem[1];
        ioctl(dev->fd, KBASE_IOCTL_MEM_FREE, &free_va);
        return NULL;
    }

    bo->dev = dev;
    bo->cpu = cpu_ptr;
    bo->gpu = (uint64_t)(uintptr_t)cpu_ptr;
    bo->size = aligned_size;
    bo->flags = flags;

    return bo;
}

struct kbase_bo *kbase_bo_import_dma_buf(struct kbase_dev *dev, int dma_buf_fd, size_t size) {
    if (!dev || dma_buf_fd < 0 || size == 0) return NULL;

    int import_fd = dma_buf_fd;
    union kbase_ioctl_mem_import param = {
        .in = {
            .flags = 0xf, /* CPU/GPU read and write */
            .phandle = (uint64_t)(uintptr_t)&import_fd,
            .type = BASE_MEM_IMPORT_TYPE_UMM,
            .padding = 0,
        },
    };

    if (ioctl(dev->fd, KBASE_IOCTL_MEM_IMPORT, &param) < 0) {
        perror("kbase_bo_import_dma_buf: KBASE_IOCTL_MEM_IMPORT failed");
        return NULL;
    }

    void *gpu_map = NULL;
    uint64_t gpu_va = param.out.gpu_va;
    size_t gpu_map_size = 0;
    if (param.out.flags & BASE_MEM_NEED_MMAP) {
        gpu_map_size = param.out.va_pages * (size_t)sysconf(_SC_PAGESIZE);
        /* This UMM mapping establishes the GPU VA. MTK's kbase rejects CPU
         * faults on it, so CPU access must use a mapping of the dma-buf fd. */
        gpu_map = mmap(NULL, gpu_map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       dev->fd, param.out.gpu_va);
        if (gpu_map == MAP_FAILED) {
            perror("kbase_bo_import_dma_buf: mmap");
            return NULL;
        }
        gpu_va = (uint64_t)(uintptr_t)gpu_map;
    }

    void *cpu_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         dma_buf_fd, 0);
    if (cpu_ptr == MAP_FAILED) {
        perror("kbase_bo_import_dma_buf: dma-buf mmap");
        if (gpu_map) munmap(gpu_map, gpu_map_size);
        return NULL;
    }

    struct kbase_bo *bo = calloc(1, sizeof(*bo));
    if (!bo) {
        munmap(cpu_ptr, size);
        if (gpu_map) munmap(gpu_map, gpu_map_size);
        return NULL;
    }

    bo->dev = dev;
    bo->cpu = cpu_ptr;
    bo->gpu_map = gpu_map;
    bo->gpu = gpu_va;
    bo->size = size;
    bo->gpu_map_size = gpu_map_size;
    bo->flags = KBASE_BO_PROT_READ | KBASE_BO_PROT_WRITE;
    return bo;
}

void kbase_bo_free(struct kbase_bo *bo) {
    if (!bo) return;
    if (bo->dev && bo->dev->fd >= 0 && bo->gpu > 0) {
        uint64_t gpu_addr = bo->gpu;
        ioctl(bo->dev->fd, KBASE_IOCTL_MEM_FREE, &gpu_addr);
    }
    if (bo->cpu && bo->size > 0 && bo->cpu != MAP_FAILED) munmap(bo->cpu, bo->size);
    if (bo->gpu_map && bo->gpu_map_size > 0 && bo->gpu_map != MAP_FAILED)
        munmap(bo->gpu_map, bo->gpu_map_size);
    free(bo);
}

int kbase_submit_job(struct kbase_dev *dev, uint64_t jc, uint32_t core_req, uint32_t atom_nr,
                      uint8_t jobslot, uint32_t frame_nr) {
    if (!dev || !jc) return -EINVAL;

    struct kbase_atom_mtk atom;
    memset(&atom, 0, sizeof(atom));
    atom.jc = jc;
    atom.atom_number = (uint8_t)atom_nr;
    atom.core_req = core_req;
    atom.jobslot = jobslot;
    atom.frame_nr = frame_nr;

    struct kbase_ioctl_job_submit submit = {
        .addr = (uint64_t)&atom,
        .nr_atoms = 1,
        .stride = sizeof(atom)
    };

    if (ioctl(dev->fd, KBASE_IOCTL_JOB_SUBMIT, &submit) < 0) {
        perror("kbase_submit_job: KBASE_IOCTL_JOB_SUBMIT");
        return -errno;
    }

    return 0;
}

int kbase_submit_atoms(struct kbase_dev *dev, const struct kbase_atom_submit_info *atoms, uint32_t nr_atoms) {
    if (!dev || !atoms || nr_atoms == 0) return -EINVAL;

    struct kbase_atom_mtk *k_atoms = calloc(nr_atoms, sizeof(struct kbase_atom_mtk));
    if (!k_atoms) return -ENOMEM;

    for (uint32_t i = 0; i < nr_atoms; i++) {
        k_atoms[i].jc = atoms[i].jc;
        k_atoms[i].atom_number = (uint8_t)atoms[i].atom_number;
        k_atoms[i].core_req = atoms[i].core_req;
        k_atoms[i].jobslot = atoms[i].jobslot;
        k_atoms[i].frame_nr = atoms[i].frame_nr;
        if (atoms[i].dep_atom_id[0]) {
            k_atoms[i].pre_dep[0].atom_id = atoms[i].dep_atom_id[0];
            k_atoms[i].pre_dep[0].dep_type = atoms[i].dep_type[0];
        }
        if (atoms[i].dep_atom_id[1]) {
            k_atoms[i].pre_dep[1].atom_id = atoms[i].dep_atom_id[1];
            k_atoms[i].pre_dep[1].dep_type = atoms[i].dep_type[1];
        }
    }

    struct kbase_ioctl_job_submit submit = {
        .addr = (uint64_t)(uintptr_t)k_atoms,
        .nr_atoms = nr_atoms,
        .stride = sizeof(struct kbase_atom_mtk)
    };

    int ret = 0;
    if (ioctl(dev->fd, KBASE_IOCTL_JOB_SUBMIT, &submit) < 0) {
        perror("kbase_submit_atoms: KBASE_IOCTL_JOB_SUBMIT");
        ret = -errno;
    }

    free(k_atoms);
    return ret;
}

int kbase_wait_event(struct kbase_dev *dev, uint32_t *atom_nr, uint32_t *event_code) {
    if (!dev) return -EINVAL;

    struct base_jd_event_v2 ev;
    memset(&ev, 0, sizeof(ev));
    ssize_t ret = read(dev->fd, &ev, sizeof(ev));
    if (ret <= 0) {
        return (ret == 0) ? -EAGAIN : -errno;
    }

    if (atom_nr)   *atom_nr = ev.atom_number;
    if (event_code) *event_code = ev.event_code;

    printf("kbase_wait_event: code=0x%x atom=%u prio=%u jobslot=%u timer=%llu\n",
           ev.event_code, ev.atom_number, ev.prio, ev.jobslot,
           (unsigned long long)ev.timer);

    return 0;
}

int kbase_wait_event_timeout(struct kbase_dev *dev, uint32_t *atom_nr,
                             uint32_t *event_code, int timeout_ms) {
    if (!dev || timeout_ms < 0) return -EINVAL;

    struct pollfd pfd = { .fd = dev->fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) return -errno;
    if (ret == 0) return -EAGAIN;
    if (!(pfd.revents & POLLIN)) return -EIO;

    return kbase_wait_event(dev, atom_nr, event_code);
}
