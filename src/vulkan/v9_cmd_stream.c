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
    unsigned refcount;
    struct pan_kmod_dev *dev;
    struct v9_render_target_config config;
    struct pan_kmod_bo *mem_bo;
    struct pan_kmod_bo *exec_bo;
    struct pan_kmod_bo *exec_vs_bo;
    struct pan_kmod_bo *color_bo;

    uint64_t mfbd_gva;
    uint64_t rt0_gpu;
    uint64_t polylist_gpu;
    uint64_t sampleloc_gpu;
    uint64_t dcd_gpu;
    uint64_t sp_gpu;
    uint64_t sp_vertex_gpu;
    uint64_t isa_gpu;
    uint64_t isa_vertex_gpu;
    bool has_vertex_shader;
    bool has_varying_shader;
    bool has_draw_command;
    bool use_malloc_vertex;
    uint64_t res_gpu;
    uint64_t ubo_gpu;
    uint64_t attr_buf_gpu;
    uint64_t attr_gpu;
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
    uint64_t frag_jc2_gpu;
    uint64_t mfbd2_gpu;
    uint64_t dcd2_gpu;
    uint64_t tiler_heap_backing_gpu;
    uint64_t color_gpu;
};

struct v9_cmd_buffer *v9_cmd_buffer_create(struct pan_kmod_dev *dev,
                                           const struct v9_render_target_config *config) {
    if (!dev || !config || config->width == 0 || config->height == 0) return NULL;

    struct v9_cmd_buffer *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) return NULL;

    cmd->refcount = 1;
    cmd->dev = dev;
    cmd->config = *config;

    uint32_t aligned_w = (config->width + 15) & ~15;
    uint32_t aligned_h = (config->height + 15) & ~15;
    size_t color_bytes = aligned_w * aligned_h * 4;
    cmd->color_bo = pan_kmod_bo_alloc(dev, color_bytes, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    if (!cmd->color_bo) {
        free(cmd);
        return NULL;
    }
    memset(cmd->color_bo->cpu, 0, color_bytes);

    size_t mem_size = 0x100000; /* 1 MiB for descriptors and tiler heap */
    cmd->mem_bo = pan_kmod_bo_alloc(dev, mem_size, PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE);
    if (!cmd->mem_bo) {
        pan_kmod_bo_free(cmd->color_bo);
        free(cmd);
        return NULL;
    }
    memset(cmd->mem_bo->cpu, 0, mem_size);

    cmd->exec_bo = pan_kmod_bo_alloc(dev, 4096,
                                     PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE | PAN_KMOD_BO_FLAG_EXEC);
    if (!cmd->exec_bo) {
        pan_kmod_bo_free(cmd->mem_bo);
        pan_kmod_bo_free(cmd->color_bo);
        free(cmd);
        return NULL;
    }

    uint64_t base_gva = cmd->mem_bo->gpu;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    cmd->color_gpu               = cmd->color_bo->gpu;
    printf("v9_cmd_buffer_create: color_gpu=0x%llx (size=%zu)\n",
           (unsigned long long)cmd->color_gpu, color_bytes);
    cmd->mfbd_gva                 = base_gva + 0x6000;
    cmd->rt0_gpu                  = base_gva + 0x6080;
    cmd->polylist_gpu             = base_gva + 0x7000;
    cmd->sampleloc_gpu            = base_gva + 0xB100;
    cmd->dcd_gpu                  = base_gva + 0xC100;
    cmd->sp_gpu                   = base_gva + 0xCC00;
    cmd->sp_vertex_gpu            = base_gva + 0xCD00;
    cmd->isa_gpu                  = cmd->exec_bo->gpu;
    cmd->res_gpu                  = base_gva + 0xD200;
    cmd->ubo_gpu                  = base_gva + 0xD300;
    cmd->attr_buf_gpu             = base_gva + 0xD700;
    cmd->attr_gpu                 = base_gva + 0xD900;
    cmd->flush_jc_gpu             = base_gva + 0xD400;
    cmd->tiler_heap_desc_gpu      = base_gva + 0xD500;
    cmd->tiler_ctx_gpu            = base_gva + 0xD600;
    cmd->pos_gpu                  = base_gva + 0xE000;
    cmd->blend_gpu                = base_gva + 0xE040;
    cmd->depth_gpu                = base_gva + 0xE060;
    cmd->tls_gpu                  = base_gva + 0xE100;
    cmd->idx_gpu                  = base_gva + 0xE0C0;
    cmd->tiler_job_gpu            = base_gva + 0xE200;
    cmd->frag_jc_gpu              = base_gva + 0xE380;
    cmd->frag_jc2_gpu             = base_gva + 0xE400;
    cmd->mfbd2_gpu                = base_gva + 0xE480;
    cmd->dcd2_gpu                 = base_gva + 0xE500;
    cmd->tiler_heap_backing_gpu   = base_gva + 0x40000;

    memcpy(cmd->exec_bo->cpu, k_valhall_green_fs, sizeof(k_valhall_green_fs));

    /* Initialize Blend, TLS, Depth */
    v9_pack_blend((uint32_t *)(base_cpu + (cmd->blend_gpu - base_gva)));
    v9_pack_tls((uint32_t *)(base_cpu + (cmd->tls_gpu - base_gva)), base_gva + 0x10000);
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
    v9_pack_shader_program((uint32_t *)(base_cpu + (cmd->sp_gpu - base_gva)),
                           cmd->isa_gpu, 2, 32, 0, true, true, false, false);
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

    /* MFBD & DCD 1 */
    v9_pack_mfbd((uint32_t *)(base_cpu + 0x6000), config->width, config->height,
                 cmd->dcd_gpu, cmd->tiler_ctx_gpu, cmd->sampleloc_gpu);
    v9_pack_dcd((uint32_t *)(base_cpu + (cmd->dcd_gpu - base_gva)),
                cmd->depth_gpu, cmd->blend_gpu, cmd->res_gpu, cmd->sp_gpu, cmd->tls_gpu);

    /* MFBD & DCD 2 (for Fragment completion pass) */
    v9_pack_dcd2((uint32_t *)(base_cpu + (cmd->dcd2_gpu - base_gva)), cmd->tls_gpu);
    v9_pack_mfbd2((uint32_t *)(base_cpu + (cmd->mfbd2_gpu - base_gva)),
                  config->width, config->height, cmd->dcd2_gpu, cmd->tiler_ctx_gpu, cmd->sampleloc_gpu);

    /* Cache Flush Job */
    v9_pack_flush_job((uint32_t *)(base_cpu + (cmd->flush_jc_gpu - base_gva)));

    return cmd;
}

struct v9_cmd_buffer *v9_cmd_buffer_ref(struct v9_cmd_buffer *cmd) {
    if (cmd) cmd->refcount++;
    return cmd;
}

void v9_cmd_buffer_destroy(struct v9_cmd_buffer *cmd) {
    if (!cmd) return;
    if (--cmd->refcount != 0) return;
    if (cmd->exec_bo)    pan_kmod_bo_free(cmd->exec_bo);
    if (cmd->exec_vs_bo) pan_kmod_bo_free(cmd->exec_vs_bo);
    if (cmd->color_bo)   pan_kmod_bo_free(cmd->color_bo);
    if (cmd->mem_bo)     pan_kmod_bo_free(cmd->mem_bo);
    free(cmd);
}

int v9_cmd_buffer_begin(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    /* Re-init TILER_JOB exception header words 0-7 */
    uint32_t *vt = (uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu));
    memset(vt, 0, 32);
    vt[4] = (1u << 0) | (7u << 1);

    /* Zero polygon list header table before binning for all tiles */
    size_t poly_bytes = ((cmd->config.width + 15) / 16) * ((cmd->config.height + 15) / 16) * 8;
    if (poly_bytes < 4096) poly_bytes = 4096;
    memset(base_cpu + (cmd->polylist_gpu - cmd->mem_bo->gpu), 0, poly_bytes);

    /* Re-init Fragment JC 1 & 2 headers */
    uint32_t *fj1 = (uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu));
    memset(fj1, 0, 32);
    uint32_t *fj2 = (uint32_t *)(base_cpu + (cmd->frag_jc2_gpu - cmd->mem_bo->gpu));
    memset(fj2, 0, 32);

    return 0;
}

int v9_cmd_buffer_set_vertex_shader(struct v9_cmd_buffer *cmd,
                                     const struct panvk_v9_compiled_shader *shader) {
    if (!cmd || !cmd->mem_bo || !shader || !shader->binary ||
        !shader->binary_size || (shader->binary_size & 7)) {
        return -EINVAL;
    }

    if (!cmd->exec_vs_bo || shader->binary_size > cmd->exec_vs_bo->size) {
        if (cmd->exec_vs_bo) pan_kmod_bo_free(cmd->exec_vs_bo);
        struct pan_kmod_bo *new_bo = pan_kmod_bo_alloc(
            cmd->dev, shader->binary_size,
            PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE | PAN_KMOD_BO_FLAG_EXEC);
        if (!new_bo) return -ENOMEM;
        cmd->exec_vs_bo = new_bo;
        cmd->isa_vertex_gpu = new_bo->gpu;
    }

    memcpy(cmd->exec_vs_bo->cpu, shader->binary, shader->binary_size);
    uint8_t *base_cpu = cmd->mem_bo->cpu;
    v9_pack_shader_program((uint32_t *)(base_cpu + (cmd->sp_vertex_gpu - cmd->mem_bo->gpu)),
                           cmd->isa_vertex_gpu + shader->no_psiz_offset, 3,
                           shader->work_reg_count, shader->preload,
                           false, shader->contains_barrier,
                           shader->ftz_fp16, shader->ftz_fp32);
    if (shader->secondary_enable) {
        v9_pack_shader_program(
            (uint32_t *)(base_cpu + (cmd->sp_vertex_gpu + 32 - cmd->mem_bo->gpu)),
            cmd->isa_vertex_gpu + shader->secondary_offset, 3,
            shader->secondary_work_reg_count, shader->secondary_preload,
            false, shader->contains_barrier,
            shader->ftz_fp16, shader->ftz_fp32);
    }
    cmd->has_vertex_shader = true;
    cmd->has_varying_shader = shader->secondary_enable &&
                              getenv("PANVK_EXPERIMENT_MV11_VARYING");
    cmd->use_malloc_vertex = getenv("PANVK_EXPERIMENT_MV11_POSITION") && shader->idvs;
    return 0;
}

int v9_cmd_buffer_set_fragment_shader(struct v9_cmd_buffer *cmd,
                                      const struct panvk_v9_compiled_shader *shader) {
    if (!cmd || !cmd->mem_bo || !shader || !shader->binary ||
        !shader->binary_size || (shader->binary_size & 7)) {
        return -EINVAL;
    }

    if (shader->binary_size > cmd->exec_bo->size) {
        struct pan_kmod_bo *new_bo = pan_kmod_bo_alloc(
            cmd->dev, shader->binary_size,
            PAN_KMOD_BO_FLAG_READ | PAN_KMOD_BO_FLAG_WRITE | PAN_KMOD_BO_FLAG_EXEC);
        if (!new_bo) return -ENOMEM;
        pan_kmod_bo_free(cmd->exec_bo);
        cmd->exec_bo = new_bo;
        cmd->isa_gpu = new_bo->gpu;
    }

    memcpy(cmd->exec_bo->cpu, shader->binary, shader->binary_size);
    uint8_t *base_cpu = cmd->mem_bo->cpu;
    v9_pack_shader_program((uint32_t *)(base_cpu + (cmd->sp_gpu - cmd->mem_bo->gpu)),
                           cmd->isa_gpu, 2, shader->work_reg_count, shader->preload,
                           true, shader->contains_barrier,
                           shader->ftz_fp16, shader->ftz_fp32);
    return 0;
}

int v9_cmd_buffer_set_ubos(struct v9_cmd_buffer *cmd,
                           const struct v9_ubo_binding *bindings,
                           uint32_t binding_count) {
    if (!cmd || !cmd->mem_bo || (binding_count && !bindings)) return -EINVAL;

    uint32_t descriptor_count = 0;
    for (uint32_t i = 0; i < binding_count; i++) {
        if (bindings[i].index >= 8) return -E2BIG;
        if (bindings[i].index + 1 > descriptor_count)
            descriptor_count = bindings[i].index + 1;
    }

    uint8_t *base_cpu = cmd->mem_bo->cpu;
    uint32_t *ubos = (uint32_t *)(base_cpu + (cmd->ubo_gpu - cmd->mem_bo->gpu));
    memset(ubos, 0, 8 * 32);
    for (uint32_t i = 0; i < binding_count; i++) {
        v9_pack_buffer(ubos + bindings[i].index * 8,
                       bindings[i].address, bindings[i].size);
    }

    uint32_t *resources = (uint32_t *)(base_cpu + (cmd->res_gpu - cmd->mem_bo->gpu));
    memset(resources, 0, 12 * 16);
    if (descriptor_count)
        v9_pack_resource(resources, cmd->ubo_gpu, descriptor_count * 32);
    return 0;
}

int v9_cmd_buffer_set_attributes(struct v9_cmd_buffer *cmd,
                                 const struct v9_attribute_binding *bindings,
                                 uint32_t binding_count) {
    if (!cmd || !cmd->mem_bo || (binding_count && !bindings)) return -EINVAL;

    uint8_t *base_cpu = cmd->mem_bo->cpu;
    uint32_t *attr_bufs = (uint32_t *)(base_cpu + (cmd->attr_buf_gpu - cmd->mem_bo->gpu));
    uint32_t *attrs = (uint32_t *)(base_cpu + (cmd->attr_gpu - cmd->mem_bo->gpu));
    memset(attr_bufs, 0, 8 * 32);
    memset(attrs, 0, 8 * 32);

    for (uint32_t i = 0; i < binding_count && i < 8; i++) {
        v9_pack_buffer(attr_bufs + i * 8, bindings[i].buffer_address, bindings[i].buffer_size);
        v9_pack_attribute(attrs + i * 8, bindings[i].format, 1, bindings[i].offset,
                          i, bindings[i].stride, bindings[i].input_rate);
    }

    uint32_t *resources = (uint32_t *)(base_cpu + (cmd->res_gpu - cmd->mem_bo->gpu));
    if (binding_count > 0) {
        v9_pack_resource(resources + 4, cmd->attr_buf_gpu, binding_count * 32);
        v9_pack_resource(resources + 8, cmd->attr_gpu, binding_count * 32);
    }
    return 0;
}

int v9_cmd_draw_indexed(struct v9_cmd_buffer *cmd,
                        uint64_t idx_gpu, uint32_t index_count, uint32_t index_type,
                        uint64_t pos_gpu, uint32_t vertex_count) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    printf("v9_cmd_draw_indexed: idx_gpu=0x%llx, index_count=%u, index_type=%u, pos_gpu=0x%llx, vertex_count=%u, has_vs=%d\n",
           (unsigned long long)idx_gpu, index_count, index_type,
           (unsigned long long)pos_gpu, vertex_count, cmd->has_vertex_shader);

    v9_pack_tiler_job((uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu)),
                      cmd->config.width, cmd->config.height,
                      cmd->tiler_ctx_gpu, idx_gpu, pos_gpu,
                      cmd->depth_gpu, cmd->blend_gpu, cmd->res_gpu,
                      cmd->sp_gpu, (cmd->has_vertex_shader ? cmd->sp_vertex_gpu : 0),
                      (cmd->has_varying_shader ? cmd->sp_vertex_gpu + 32 : 0), cmd->tls_gpu,
                      index_count, index_type, vertex_count, cmd->use_malloc_vertex);
    cmd->has_draw_command = true;
    return 0;
}

int v9_cmd_draw_indexed_triangle(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    return v9_cmd_draw_indexed(cmd, cmd->idx_gpu, 3, 0, cmd->pos_gpu, 3);
}

int v9_cmd_buffer_end(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    uint32_t *fj1 = (uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu));
    uint32_t *fj2 = (uint32_t *)(base_cpu + (cmd->frag_jc2_gpu - cmd->mem_bo->gpu));

    v9_pack_frag_job_chain(fj1, fj2, cmd->mfbd_gva, cmd->mfbd2_gpu, cmd->frag_jc2_gpu,
                           cmd->config.width, cmd->config.height);
    return 0;
}

int v9_cmd_buffer_submit(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->dev) return -EINVAL;

    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    /* The GPU writes completion state into the job exception header.  A
     * recorded Vulkan command buffer may be submitted more than once, so
     * restore the header without repacking the draw payload at word 8+. */
    uint32_t *vt = (uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu));
    memset(vt, 0, 32);
    vt[4] = cmd->use_malloc_vertex ? (11u << 1) : ((1u << 0) | (7u << 1));

    /* Zero polygon list header table before TILER_JOB */
    size_t poly_bytes = ((cmd->config.width + 15) / 16) * ((cmd->config.height + 15) / 16) * 8;
    if (poly_bytes < 4096) poly_bytes = 4096;
    memset(base_cpu + (cmd->polylist_gpu - cmd->mem_bo->gpu), 0, poly_bytes);

    /* Re-init Fragment JC 1 & 2 headers before submission */
    uint32_t *fj1 = (uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu));
    uint32_t *fj2 = (uint32_t *)(base_cpu + (cmd->frag_jc2_gpu - cmd->mem_bo->gpu));
    v9_pack_frag_job_chain(fj1, fj2, cmd->mfbd_gva, cmd->mfbd2_gpu, cmd->frag_jc2_gpu,
                           cmd->config.width, cmd->config.height);
    if (cmd->use_malloc_vertex)
        pack_u64(fj1 + 6, cmd->frag_jc2_gpu);

    if (!cmd->has_draw_command) {
        v9_cmd_draw_indexed_triangle(cmd);
    }

    uint32_t event_code = 0;

    /* 1. Atom 0: TILER_JOB */
    int ret = pan_kmod_submit_atom(cmd->dev, cmd->tiler_job_gpu, KBASE_QUEUE_REQ_TILER, 0, &event_code);
    if (ret != 0 || event_code != 0x1) {
        fprintf(stderr, "v9_cmd_buffer_submit: TILER_JOB failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        return -EIO;
    }

    if (getenv("PANVK_DUMP_TILER")) {
        uint8_t *cpu = cmd->mem_bo->cpu;
        uint32_t *tj = (uint32_t *)(cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "TJ WORDS:");
        for (unsigned i = 0; i < 64; i++) fprintf(stderr, " %08x", tj[i]);
        fprintf(stderr, "\n");
    }

    if (getenv("PANVK_PATCH_TILER_STATE")) {
        uint32_t *ctx = (uint32_t *)((uint8_t *)cmd->mem_bo->cpu +
                                     (cmd->tiler_ctx_gpu - cmd->mem_bo->gpu));
        ctx[33] = 31;
        ctx[35] = 0x10000000u;
    }

    /* 2. Atom 1: Pre-Flush */
    v9_pack_flush_job((uint32_t *)(base_cpu + (cmd->flush_jc_gpu - cmd->mem_bo->gpu)));
    ret = pan_kmod_submit_atom(cmd->dev, cmd->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1, &event_code);
    if (ret != 0 || event_code != 0x1) {
        fprintf(stderr, "v9_cmd_buffer_submit: Pre-Flush failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        return -EIO;
    }

    /* Reset Tiler Heap Desc bottom pointer back to heap base for Fragment HW */
    uint32_t *th = (uint32_t *)(base_cpu + (cmd->tiler_heap_desc_gpu - cmd->mem_bo->gpu));
    pack_u64(th + 4, cmd->tiler_heap_backing_gpu);

    /* 3. Atom 2: Fragment JC (hardware chain Job 1 -> Job 2) */
    ret = pan_kmod_submit_atom_timeout(cmd->dev, cmd->frag_jc_gpu, KBASE_QUEUE_REQ_FRAGMENT, 2, &event_code, 200);
    if (ret < 0) {
        fprintf(stderr, "v9_cmd_buffer_submit: Fragment JC submission failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        uint32_t *fj1 = (uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu));
        uint32_t *fj2 = (uint32_t *)(base_cpu + (cmd->frag_jc2_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "FJ1 status: 0x%08x 0x%08x 0x%08x 0x%08x fault_ptr=0x%llx\n",
                fj1[0], fj1[1], fj1[2], fj1[3], (unsigned long long)*(uint64_t *)(fj1 + 2));
        fprintf(stderr, "FJ2 status: 0x%08x 0x%08x 0x%08x 0x%08x fault_ptr=0x%llx\n",
                fj2[0], fj2[1], fj2[2], fj2[3], (unsigned long long)*(uint64_t *)(fj2 + 2));
        return ret;
    }

    /* 4. Atom 3: Post-Flush (flushes L2 cache and signals completion) */
    v9_pack_flush_job((uint32_t *)(base_cpu + (cmd->flush_jc_gpu - cmd->mem_bo->gpu)));
    ret = pan_kmod_submit_atom(cmd->dev, cmd->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1, &event_code);
    if (ret != 0 || event_code != 0x1) {
        fprintf(stderr, "v9_cmd_buffer_submit: Post-Flush submission failed (ret=%d, event_code=0x%x)\n", ret, event_code);
        uint32_t *fj1 = (uint32_t *)(base_cpu + (cmd->frag_jc_gpu - cmd->mem_bo->gpu));
        uint32_t *fj2 = (uint32_t *)(base_cpu + (cmd->frag_jc2_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "FJ1 status: 0x%08x 0x%08x 0x%08x 0x%08x fault_ptr=0x%llx\n",
                fj1[0], fj1[1], fj1[2], fj1[3], (unsigned long long)*(uint64_t *)(fj1 + 2));
        fprintf(stderr, "FJ2 status: 0x%08x 0x%08x 0x%08x 0x%08x fault_ptr=0x%llx\n",
                fj2[0], fj2[1], fj2[2], fj2[3], (unsigned long long)*(uint64_t *)(fj2 + 2));
        return -EIO;
    }

    return 0;
}

uint64_t v9_cmd_buffer_get_pos_gpu(struct v9_cmd_buffer *cmd) {
    return cmd ? cmd->pos_gpu : 0;
}

uint64_t v9_cmd_buffer_get_idx_gpu(struct v9_cmd_buffer *cmd) {
    return cmd ? cmd->idx_gpu : 0;
}

uint32_t v9_cmd_buffer_read_pixel(struct v9_cmd_buffer *cmd, uint32_t x, uint32_t y) {
    if (!cmd || !cmd->color_bo || x >= cmd->config.width || y >= cmd->config.height) return 0;
    uint32_t *color = (uint32_t *)cmd->color_bo->cpu;
    return color[y * cmd->config.width + x];
}
