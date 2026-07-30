/*
 * Mali-G77 MC9 (Valhall v9) pan_kmod kbase backend
 * Mesa-compatible kmod abstraction over /dev/mali0
 */

#ifndef PAN_KMOD_KBASE_H
#define PAN_KMOD_KBASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory Flags */
#define PAN_KMOD_BO_FLAG_READ         (1u << 0)
#define PAN_KMOD_BO_FLAG_WRITE        (1u << 1)
#define PAN_KMOD_BO_FLAG_EXEC         (1u << 2)
#define PAN_KMOD_BO_FLAG_COHERENT     (1u << 3)

struct pan_kmod_dev;

struct pan_kmod_bo {
    struct pan_kmod_dev *dev;
    void *cpu;
    uint64_t gpu;
    size_t size;
    uint32_t handle;
    uint32_t flags;
};

struct pan_kmod_dev_props {
    uint32_t gpu_id;
    uint32_t gpu_revision;
    uint32_t core_count;
    char ddk_version[64];
};

/* Panfrost kmod API for kbase */
struct pan_kmod_dev *pan_kmod_dev_create(const char *dev_node);
void pan_kmod_dev_destroy(struct pan_kmod_dev *dev);
int pan_kmod_dev_query_props(struct pan_kmod_dev *dev, struct pan_kmod_dev_props *props);

struct pan_kmod_bo *pan_kmod_bo_alloc(struct pan_kmod_dev *dev, size_t size, uint32_t flags);
void pan_kmod_bo_free(struct pan_kmod_bo *bo);

int pan_kmod_submit_atom(struct pan_kmod_dev *dev, uint64_t jc_gpu, uint32_t core_req,
                         uint32_t atom_id, uint32_t *event_code);
int pan_kmod_submit_atom_timeout(struct pan_kmod_dev *dev, uint64_t jc_gpu, uint32_t core_req,
                                 uint32_t atom_id, uint32_t *event_code, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* PAN_KMOD_KBASE_H */
