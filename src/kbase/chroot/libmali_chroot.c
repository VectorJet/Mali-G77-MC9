#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT    _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC     _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_MEM_FREE      _IOC(_IOC_WRITE, 0x80, 7, 8)
#define KBASE_IOCTL_GET_GPUPROPS _IOC(_IOC_WRITE, 0x80, 3, 16)

#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)
#define BASE_MEM_SAME_VA       (1ULL << 13)

#define BASE_JD_REQ_FS    (1ULL << 0)
#define BASE_JD_REQ_CS   (1ULL << 1)
#define BASE_JD_REQ_T    (1ULL << 2)
#define BASE_JD_REQ_CF   (1ULL << 3)
#define BASE_JD_REQ_COHERENT_GROUP (1ULL << 6)

#define KBASE_VERSION_MAJOR 11
#define KBASE_VERSION_MINOR 13

#pragma pack(push, 1)
struct kbase_atom {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t  jit_id[2];
    uint8_t  pre_dep_atom[2];
    uint8_t  pre_dep_type[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;
};

struct kbase_version {
    uint16_t major;
    uint16_t minor;
};

struct kbase_mem_alloc {
    uint64_t va_pages;
    uint64_t commit_pages;
    uint64_t extension;
    uint64_t flags;
};

struct kbase_mem_free {
    uint64_t gpu_addr;
};

struct kbase_job_submit {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};
#pragma pack(pop)

static int mali_fd = -1;
static bool mali_initialized = false;
static pthread_mutex_t mali_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    void *cpu_ptr;
    uint64_t gpu_va;
    size_t size;
} gpu_buffer_t;

#define MAX_BUFFERS 64
static gpu_buffer_t buffers[MAX_BUFFERS];
static int buffer_count = 0;

int mali_init(void) {
    int ret;

    if (mali_initialized) return 0;

    pthread_mutex_lock(&mali_lock);

    if (mali_initialized) {
        pthread_mutex_unlock(&mali_lock);
        return 0;
    }

    mali_fd = open("/dev/mali0", O_RDWR);
    if (mali_fd < 0) {
        fprintf(stderr, "[mali] open /dev/mali0 failed: %s\n", strerror(errno));
        pthread_mutex_unlock(&mali_lock);
        return -1;
    }

    struct kbase_version ver = {KBASE_VERSION_MAJOR, KBASE_VERSION_MINOR};
    ret = ioctl(mali_fd, KBASE_IOCTL_VERSION_CHECK, &ver);
    if (ret < 0) {
        fprintf(stderr, "[mali] VERSION_CHECK failed: %s\n", strerror(errno));
        close(mali_fd);
        mali_fd = -1;
        pthread_mutex_unlock(&mali_lock);
        return -1;
    }

    uint32_t create_flags = 0;
    ret = ioctl(mali_fd, KBASE_IOCTL_SET_FLAGS, &create_flags);
    if (ret < 0) {
        fprintf(stderr, "[mali] SET_FLAGS failed: %s\n", strerror(errno));
        close(mali_fd);
        mali_fd = -1;
        pthread_mutex_unlock(&mali_lock);
        return -1;
    }

    fprintf(stderr, "[mali] Initialized: fd=%d, version %u.%u\n", mali_fd, ver.major, ver.minor);
    mali_initialized = true;
    pthread_mutex_unlock(&mali_lock);

    return 0;
}

void *gpu_alloc(size_t size, uint64_t *out_gpu_va) {
    if (!mali_initialized) {
        if (mali_init() < 0) return NULL;
    }

    size_t pages = (size + 4095) / 4096;
    if (pages < 1) pages = 1;

    struct kbase_mem_alloc req = {
        .va_pages = pages,
        .commit_pages = pages,
        .extension = 0,
        .flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                 BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
                 BASE_MEM_SAME_VA
    };

    uint64_t in_flags = req.flags;
    int ret = ioctl(mali_fd, KBASE_IOCTL_MEM_ALLOC, &req);
    if (ret < 0) {
        fprintf(stderr, "[mali] MEM_ALLOC failed: %s\n", strerror(errno));
        return NULL;
    }

    uint64_t gpu_va = req.flags;
    fprintf(stderr, "[mali] MEM_ALLOC: in=0x%llx, out: [0]=0x%llx, [1]=0x%llx, [2]=0x%llx, [3]=0x%llx\n",
            (unsigned long long)in_flags,
            (unsigned long long)((uint64_t *)&req)[0],
            (unsigned long long)((uint64_t *)&req)[1],
            (unsigned long long)((uint64_t *)&req)[2],
            (unsigned long long)((uint64_t *)&req)[3]);

    gpu_va = ((uint64_t *)&req)[1];

    void *cpu_ptr = mmap(NULL, pages * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mali_fd, gpu_va);
    if (cpu_ptr == MAP_FAILED) {
        fprintf(stderr, "[mali] mmap failed: %s\n", strerror(errno));
        return NULL;
    }

    memset(cpu_ptr, 0, pages * 4096);

    if (buffer_count < MAX_BUFFERS) {
        buffers[buffer_count].cpu_ptr = cpu_ptr;
        buffers[buffer_count].gpu_va = gpu_va;
        buffers[buffer_count].size = pages * 4096;
        buffer_count++;
    }

    *out_gpu_va = gpu_va;
    fprintf(stderr, "[mali] gpu_alloc: cpu=%p, gpu_va=0x%llx, size=%zu\n",
            cpu_ptr, (unsigned long long)gpu_va, size);

    return cpu_ptr;
}

int gpu_free(void *ptr) {
    for (int i = 0; i < buffer_count; i++) {
        if (buffers[i].cpu_ptr == ptr) {
            munmap(ptr, buffers[i].size);
            buffers[i].cpu_ptr = NULL;
            fprintf(stderr, "[mali] gpu_free: cpu=%p\n", ptr);
            return 0;
        }
    }
    return -1;
}

static int submit_atoms(struct kbase_atom *atoms, int count) {
    if (!mali_initialized) {
        if (mali_init() < 0) return -1;
    }

    struct kbase_job_submit submit = {
        .addr = (uint64_t)atoms,
        .nr_atoms = count,
        .stride = 72
    };

    int ret = ioctl(mali_fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    if (ret < 0) {
        fprintf(stderr, "[mali] JOB_SUBMIT failed: %s\n", strerror(errno));
        return ret;
    }

    fprintf(stderr, "[mali] JOB_SUBMIT: %d atoms submitted\n", count);
    return 0;
}

int gpu_wait_event(void) {
    uint8_t ev[24] = {0};
    ssize_t n = read(mali_fd, ev, sizeof(ev));
    if (n > 0) {
        fprintf(stderr, "[mali] Event: code=0x%08x\n", *(uint32_t *)ev);
        return (int)*(uint32_t *)ev;
    }
    return 0;
}

int gpu_flush(void) {
    usleep(50000);
    return gpu_wait_event();
}

int mali_cleanup(void) {
    for (int i = 0; i < buffer_count; i++) {
        if (buffers[i].cpu_ptr) {
            munmap(buffers[i].cpu_ptr, buffers[i].size);
        }
    }
    buffer_count = 0;

    if (mali_fd >= 0) {
        close(mali_fd);
        mali_fd = -1;
    }
    mali_initialized = false;
    return 0;
}

void *gpu_alloc_buffer(size_t size) {
    uint64_t gva;
    return gpu_alloc(size, &gva);
}

__attribute__((visibility("default")))
int mali_driver_open(const char *path) {
    (void)path;
    return mali_init();
}

__attribute__((visibility("default")))
void *mali_create_context(uint32_t flags) {
    (void)flags;
    if (mali_init() < 0) return NULL;
    return (void *)0x1;
}

__attribute__((visibility("default")))
int mali_destroy_context(void *ctx) {
    (void)ctx;
    return 0;
}

__attribute__((visibility("default")))
int mali_submit_job(void *ctx, void *job, uint64_t *deps, int dep_count) {
    (void)ctx;
    (void)job;
    (void)deps;
    (void)dep_count;
    return 0;
}

__attribute__((visibility("default")))
int mali_initialize(void) {
    return mali_init();
}

__attribute__((visibility("default")))
int mali_gpu_init(void) {
    return mali_init();
}

__attribute__((visibility("default")))
void *mali_gpu_alloc(size_t size, uint64_t *gpu_va) {
    return gpu_alloc(size, gpu_va);
}

__attribute__((visibility("default")))
int mali_gpu_free(void *ptr) {
    return gpu_free(ptr);
}

__attribute__((visibility("default")))
int mali_gpu_submit(void *atom, int count) {
    return submit_atoms((struct kbase_atom *)atom, count);
}

__attribute__((visibility("default")))
int mali_gpu_flush(void) {
    return gpu_flush();
}