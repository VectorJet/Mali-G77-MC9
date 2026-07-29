/*
 * Valhall v9 Descriptor Builder & Command Buffer Encoder
 * Encodes MFBD, DCD, Tiler Context, Tiler Job, and Fragment JC
 */

#ifndef V9_BUILDER_H
#define V9_BUILDER_H

#include "kbase_winsys.h"

#ifdef __cplusplus
extern "C" {
#endif

struct v9_framebuffer {
    uint32_t width;
    uint32_t height;
    struct kbase_bo *mem_bo;
    struct kbase_bo *exec_bo;
    struct kbase_bo *color_bo;

    /* Offsets within mem_bo */
    uint64_t mfbd_gva;
    uint64_t mfbd_gpu;
    uint64_t dcd_gpu;
    uint64_t rt0_gpu;
    uint64_t tiler_ctx_gpu;
    uint64_t tiler_heap_desc_gpu;
    uint64_t tiler_heap_backing_gpu;
    uint64_t polylist_gpu;
    uint64_t blend_gpu;
    uint64_t depth_gpu;
    uint64_t tls_gpu;
    uint64_t sp_gpu;
    uint64_t res_gpu;
    uint64_t pos_gpu;
    uint64_t idx_gpu;
    uint64_t tiler_job_gpu;
    uint64_t frag_jc_gpu;
    uint64_t flush_jc_gpu;
    uint64_t isa_gpu;
};

struct v9_framebuffer *v9_framebuffer_create(struct kbase_dev *dev, uint32_t width, uint32_t height);
void v9_framebuffer_free(struct v9_framebuffer *fb);

int v9_render_triangle(struct v9_framebuffer *fb);
uint32_t v9_read_pixel(struct v9_framebuffer *fb, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif

#endif /* V9_BUILDER_H */
