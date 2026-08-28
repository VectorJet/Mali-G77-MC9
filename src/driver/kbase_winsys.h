/*
 * Mali-G77 MC9 (Valhall v9) kbase Winsys Driver
 * Direct userspace winsys implementation over /dev/mali0
 */

#ifndef KBASE_WINSYS_H
#define KBASE_WINSYS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory Protection Flags */
#define KBASE_BO_PROT_READ     (1u << 0)
#define KBASE_BO_PROT_WRITE    (1u << 1)
#define KBASE_BO_PROT_EXEC     (1u << 2)
#define KBASE_BO_COHERENT      (1u << 3)

/* Atom Queue Core Requirements */
#define KBASE_QUEUE_REQ_FRAGMENT (0x041u) /* BASE_JD_REQ_FS | BASE_JD_REQ_COHERENT_GROUP */
#define KBASE_QUEUE_REQ_TILER    (0x04Eu) /* PROTECTED | TILER | CS | COHERENT */
#define KBASE_QUEUE_REQ_FLUSH    (0x002u) /* CS compute slot for cache flush atoms */

struct kbase_dev;

struct kbase_bo {
    struct kbase_dev *dev;
    void *cpu;
    void *gpu_map;
    uint64_t gpu;
    size_t size;
    size_t gpu_map_size;
    uint32_t handle;
    uint32_t flags;
};

#define BASE_MEM_IMPORT_TYPE_UMM 2
#define BASE_MEM_NEED_MMAP       (1ull << 14)

union kbase_ioctl_mem_import {
    struct {
        uint64_t flags;
        uint64_t phandle;
        uint32_t type;
        uint32_t padding;
    } in;
    struct {
        uint64_t flags;
        uint64_t gpu_va;
        uint64_t va_pages;
    } out;
};
#define KBASE_IOCTL_MEM_IMPORT _IOC(_IOC_READ|_IOC_WRITE, 0x80, 22, sizeof(union kbase_ioctl_mem_import))

struct kbase_dev *kbase_dev_open(const char *dev_node);
void kbase_dev_close(struct kbase_dev *dev);

struct kbase_bo *kbase_bo_alloc(struct kbase_dev *dev, size_t size, uint32_t flags);
struct kbase_bo *kbase_bo_import_dma_buf(struct kbase_dev *dev, int dma_buf_fd, size_t size);
void kbase_bo_free(struct kbase_bo *bo);

struct kbase_atom_submit_info {
    uint64_t jc;
    uint32_t core_req;
    uint32_t atom_number;
    uint8_t jobslot;
    uint8_t dep_atom_id[2];
    uint8_t dep_type[2];
    uint32_t frame_nr;
};

int kbase_submit_job(struct kbase_dev *dev, uint64_t jc, uint32_t core_req, uint32_t atom_nr,
                      uint8_t jobslot, uint32_t frame_nr);
int kbase_submit_atoms(struct kbase_dev *dev, const struct kbase_atom_submit_info *atoms, uint32_t nr_atoms);
int kbase_wait_event(struct kbase_dev *dev, uint32_t *atom_nr, uint32_t *event_code);
int kbase_wait_event_timeout(struct kbase_dev *dev, uint32_t *atom_nr,
                             uint32_t *event_code, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* KBASE_WINSYS_H */
