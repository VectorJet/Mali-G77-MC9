/*
 * Valhall v9 Command Stream Recorder Engine Implementation
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "v9_cmd_stream.h"
#include "v9_pack.h"
#include "pan_kmod_kbase.h"
#include "kbase_winsys.h"

/* Pre-compiled Valhall fragment shader producing solid green (0xFF00FF00) (40 bytes) */
static const uint8_t k_valhall_green_fs[] = {
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x10, 0x01, /* IADD_IMM.i32 r0, 0x0, #0x0 */
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc1, 0xa4, 0x00, /* FADD.f32 r1, r0, 0x3F800000 */
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x10, 0x01, /* IADD_IMM.i32 r2, 0x0, #0x0 */
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc3, 0xa4, 0x00, /* FADD.f32 r3, r0, 0x3F800000 */
    0xf0, 0x00, 0x3c, 0x32, 0x08, 0x40, 0x7f, 0x78  /* BLEND.slot0.v4.f32.end */
};

struct v9_cmd_buffer {
    struct pan_kmod_dev *dev;
    struct v9_render_target_config config;
    struct pan_kmod_bo *mem_bo;
    struct pan_kmod_bo *exec_bo;

    uint64_t mfbd_gva;
    uint64_t rt0_gpu;
    uint64_t polylist_gpu;
    uint64_t sampleloc_gpu;
    uint64_t dcd_gpu;
    uint64_t sp_gpu;
    uint64_t isa_gpu;
    uint64_t res_gpu;
    uint64_t flush_jc_gpu;
    uint64_t tiler_heap_desc_gpu;
    uint64_t tiler_ctx_gpu;
    uint64_t pos_gpu;
    uint64_t blend_gpu;
    uint64_t depth_gpu;
    uint64_t tls_gpu;
    uint64_t idx_gpu;
    uint64_t tiler_job_gpu;
    uint64_t frag_jc_gpu;
    uint64_t tiler_heap_backing_gpu;
    uint64_t color_gpu;
};

struct v9_cmd_buffer *v9_cmd_buffer_create(struct pan_kmod_dev *dev,
                                           const struct v9_render_target_config *config) {
    if (!dev || !config || config->width == 0 || config->height == 0) return NULL;

    struct v9_cmd_buffer *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) return NULL;

    cmd->dev = dev;
    cmd->config = *config;

    size_t mem_size = 1024 * 1024;
    cmd->mem_bo = pan_kmod_bo_alloc(dev, mem_size, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    if (!cmd->mem_bo) {
        free(cmd);
        return NULL;
    }
    memset(cmd->mem_bo->cpu, 0, mem_size);

    cmd->exec_bo = pan_kmod_bo_alloc(dev, 4096,
                                     PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE | PAN_KMOD_BO_FLAG_EXEC);
    if (!cmd->exec_bo) {
        pan_kmod_bo_free(cmd->mem_bo);
        free(cmd);
        return NULL;
    }

    uint64_t base_gva = cmd->mem_bo->gpu;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    uint64_t color_off = (config->width * config->height * 4 > 0x1000) ? 0x40000 : 0xA000;
    cmd->color_gpu               = base_gva + color_off;
    cmd->mfbd_gva                 = base_gva + 0x6000;
    cmd->rt0_gpu                  = base_gva + 0x6080;
    cmd->polylist_gpu             = base_gva + 0x7000;
    cmd->sampleloc_gpu            = base_gva + 0xB100;
    cmd->dcd_gpu                  = base_gva + 0xC100;
    cmd->sp_gpu                   = base_gva + 0xCC00;
    cmd->isa_gpu                  = cmd->exec_bo->gpu;
    cmd->res_gpu                  = base_gva + 0xD200;
    cmd->flush_jc_gpu             = base_gva + 0xD400;
    cmd->tiler_heap_desc_gpu      = base_gva + 0xD500;
    cmd->tiler_ctx_gpu            = base_gva + 0xD600;
    cmd->pos_gpu                  = base_gva + 0xE000;
    cmd->blend_gpu                = base_gva + 0xE040;
    cmd->depth_gpu                = base_gva + 0xE060;
    cmd->tls_gpu                  = base_gva + 0xE0A0;
    cmd->idx_gpu                  = base_gva + 0xE0C0;
    cmd->tiler_job_gpu            = base_gva + 0xE200;
    cmd->frag_jc_gpu              = base_gva + 0xE380;
    cmd->tiler_heap_backing_gpu   = base_gva + 0x80000;

    memcpy(cmd->exec_bo->cpu, k_valhall_green_fs, sizeof(k_valhall_green_fs));

    /* Initialize Blend, TLS, Depth */
    v9_pack_blend((uint32_t *)(base_cpu + (cmd->blend_gpu - base_gva)));
    v9_pack_tls((uint32_t *)(base_cpu + (cmd->tls_gpu - base_gva)), base_gva + 0x8000);
    v9_pack_depth((uint32_t *)(base_cpu + (cmd->depth_gpu - base_gva)));

    /* Position buffer */
    float *pos = (float *)(base_cpu + (cmd->pos_gpu - base_gva));
    pos[0] = -1.0f; pos[1] = -1.0f; pos[2] = 0.5f; pos[3] = 1.0f;
    pos[4] =  3.0f; pos[5] = -1.0f; pos[6] = 0.5f; pos[7] = 1.0f;
    pos[8] = -1.0f; pos[9] =  3.0f; pos[10] = 0.5f; pos[11] = 1.0f;

    /* Index buffer */
    uint16_t *idx = (uint16_t *)(base_cpu + (cmd->idx_gpu - base_gva));
    idx[0] = 0; idx[1] = 1; idx[2] = 2;

    /* Shader program & tiler heap & context */
    v9_pack_shader_program((uint32_t *)(base_cpu + (cmd->sp_gpu - base_gva)), cmd->isa_gpu);
    v9_pack_tiler_heap((uint32_t *)(base_cpu + (cmd->tiler_heap_desc_gpu - base_gva)),
                       cmd->tiler_heap_backing_gpu, 0x40000);
    v9_pack_tiler_ctx((uint32_t *)(base_cpu + (cmd->tiler_ctx_gpu - base_gva)),
                      cmd->polylist_gpu, config->width, config->height, cmd->tiler_heap_desc_gpu);

    /* Render target & sample location */
    v9_pack_rt0((uint32_t *)(base_cpu + (cmd->rt0_gpu - base_gva)),
                cmd->color_gpu, config->width, config->clear_color);

    uint16_t *sl = (uint16_t *)(base_cpu + (cmd->sampleloc_gpu - base_gva));
    memset(sl, 0, 192);
    sl[0] = 128; sl[1] = 128;
    for (int i = 1; i < 32; i++) { sl[i*2] = 0; sl[i*2+1] = 256; }
    sl[64] = 128; sl[65] = 128;

    /* MFBD & DCD */
    v9_pack_mfbd((uint32_t *)(base_cpu + 0x6000), config->width, config->height,
                 cmd->dcd_gpu, cmd->tiler_ctx_gpu, cmd->sampleloc_gpu);
    v9_pack_dcd((uint32_t *)(base_cpu + (cmd->dcd_gpu - base_gva)),
                cmd->depth_gpu, cmd->blend_gpu, cmd->res_gpu, cmd->sp_gpu, cmd->tls_gpu);

    /* Cache Flush Job */
    v9_pack_flush_job((uint32_t *)(base_cpu + (cmd->flush_jc_gpu - base_gva)));

    return cmd;
}

void v9_cmd_buffer_destroy(struct v9_cmd_buffer *cmd) {
    if (!cmd) return;
    if (cmd->exec_bo) pan_kmod_bo_free(cmd->exec_bo);
    if (cmd->mem_bo)  pan_kmod_bo_free(cmd->mem_bo);
    free(cmd);
}

int v9_cmd_buffer_begin(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    /* Re-init TILER_JOB exception header words 0-3 */
    uint32_t *vt = (uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu));
    memset(vt, 0, 32);
    vt[4] = (1u << 0) | (7u << 1);

    return 0;
}

int v9_cmd_draw_indexed_triangle(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    v9_pack_tiler_job((uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu)),
                      cmd->config.width, cmd->config.height,
                      cmd->tiler_ctx_gpu, cmd->idx_gpu, cmd->pos_gpu,
                      cmd->depth_gpu, cmd->blend_gpu, cmd->res_gpu,
                      cmd->sp_gpu, cmd->tls_gpu);
    return 0;
}

int v9_cmd_buffer_end(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    v9_pack_frag_job((uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu)), cmd->mfbd_gva);
    return 0;
}

int v9_cmd_buffer_submit(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->dev) return -EINVAL;

    uint32_t event_code = 0;

    /* 1. Atom 0: TILER_JOB */
    int ret = pan_kmod_submit_atom(cmd->dev, cmd->tiler_job_gpu, KBASE_QUEUE_REQ_TILER, 0, &event_code);
    if (ret != 0 || event_code != 0x1) {
        fprintf(stderr, "v9_cmd_buffer_submit: TILER_JOB failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        return -EIO;
    }

    /* 2. Atom 1: Pre-Flush */
    ret = pan_kmod_submit_atom(cmd->dev, cmd->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1, &event_code);
    if (ret != 0 || event_code != 0x1) {
        fprintf(stderr, "v9_cmd_buffer_submit: Pre-Flush failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        return -EIO;
    }

    /* Re-init Tiler Heap Desc for Fragment HW */
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;
    uint32_t *th = (uint32_t *)(base_cpu + (cmd->tiler_heap_desc_gpu - cmd->mem_bo->gpu));
    *(uint64_t *)(th + 4) = cmd->tiler_heap_backing_gpu;

    /* 3. Atom 2: Fragment JC */
    ret = pan_kmod_submit_atom_timeout(cmd->dev, cmd->frag_jc_gpu, KBASE_QUEUE_REQ_FRAGMENT, 2, &event_code, 200);
    if (ret < 0) {
        fprintf(stderr, "v9_cmd_buffer_submit: Fragment JC submission failed (ret=%d)\n", ret);
        return ret;
    }

    /* 4. Atom 3: Post-Flush */
    ret = pan_kmod_submit_atom_timeout(cmd->dev, cmd->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1, &event_code, 200);
    if (ret < 0) {
        fprintf(stderr, "v9_cmd_buffer_submit: Post-Flush submission failed (ret=%d)\n", ret);
        return ret;
    }

    return 0;
}

uint32_t v9_cmd_buffer_read_pixel(struct v9_cmd_buffer *cmd, uint32_t x, uint32_t y) {
    if (!cmd || !cmd->mem_bo || x >= cmd->config.width || y >= cmd->config.height) return 0;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;
    uint32_t *color = (uint32_t *)(base_cpu + (cmd->color_gpu - cmd->mem_bo->gpu));
    return color[y * cmd->config.width + x];
}
