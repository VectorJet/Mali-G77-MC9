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
#define KBASE_QUEUE_REQ_FRAGMENT (0x049u) /* BASE_JD_REQ_FS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP */
#define KBASE_QUEUE_REQ_TILER    (0x04Eu) /* PROTECTED | TILER | CS | COHERENT */
#define KBASE_QUEUE_REQ_FLUSH    (0x002u) /* CS compute slot for cache flush atoms */

struct kbase_dev;

struct kbase_bo {
    struct kbase_dev *dev;
    void *cpu;
    uint64_t gpu;
    size_t size;
    uint32_t handle;
    uint32_t flags;
};

struct kbase_dev *kbase_dev_open(const char *dev_node);
void kbase_dev_close(struct kbase_dev *dev);

struct kbase_bo *kbase_bo_alloc(struct kbase_dev *dev, size_t size, uint32_t flags);
void kbase_bo_free(struct kbase_bo *bo);

int kbase_submit_job(struct kbase_dev *dev, uint64_t jc, uint32_t core_req, uint32_t atom_nr);
int kbase_wait_event(struct kbase_dev *dev, uint32_t *atom_nr, uint32_t *event_code);

#ifdef __cplusplus
}
#endif

#endif /* KBASE_WINSYS_H */
