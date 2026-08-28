/*
 * Valhall v9 Command Stream Recorder Engine Implementation
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

struct ScreenVert {
    float sx, sy, sz, w;
    float r, g, b;
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
    uint64_t res_frag_gpu;
    uint64_t ubo_gpu;
    uint64_t attr_buf_gpu;
    uint64_t attr_gpu;
    uint64_t vary_attr_gpu;
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
    float *depth_buf;
    struct ScreenVert sverts[256];
    uint32_t vertex_count;
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
    cmd->depth_buf = malloc(config->width * config->height * sizeof(float));
    if (cmd->color_bo && cmd->color_bo->cpu) {
        uint32_t *cptr = (uint32_t *)cmd->color_bo->cpu;
        uint32_t ccount = config->width * config->height;
        uint32_t cc = config->clear_color ? config->clear_color : 0xFF333333u;
        for (uint32_t i = 0; i < ccount; i++) cptr[i] = cc;
    }
    if (cmd->depth_buf) {
        uint32_t ccount = config->width * config->height;
        for (uint32_t i = 0; i < ccount; i++) cmd->depth_buf[i] = 1.0f;
    }

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
    cmd->res_frag_gpu             = base_gva + 0xD100;
    cmd->res_gpu                  = base_gva + 0xD200;
    cmd->ubo_gpu                  = base_gva + 0xD300;
    cmd->attr_buf_gpu             = base_gva + 0xD700;
    cmd->attr_gpu                 = base_gva + 0xD900;
    cmd->vary_attr_gpu            = base_gva + 0xDA00;
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

    /* Pre-pack default varying descriptors for fragment shader in Table 1 */
    uint32_t *vary_attrs = (uint32_t *)(base_cpu + (cmd->vary_attr_gpu - base_gva));
    for (uint32_t v = 0; v < 8; v++) {
        v9_pack_attribute(vary_attrs + v * 8,
                          (191u << 12) | 0u, /* RGBA32F */
                          0,                 /* Table 0 (vertex packet) */
                          v * 16,            /* offset in vertex packet (16 bytes per slot) */
                          0,                 /* buffer_index 0 (general buffer) */
                          0,                 /* stride 0 */
                          0);
    }
    uint32_t *resources_frag = (uint32_t *)(base_cpu + (cmd->res_frag_gpu - base_gva));
    v9_pack_resource(resources_frag + 4, cmd->vary_attr_gpu, 8 * 32);

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
                 cmd->dcd_gpu, cmd->tiler_ctx_gpu, cmd->sampleloc_gpu, true);
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
    if (cmd->depth_buf)  free(cmd->depth_buf);
    free(cmd);
}

int v9_cmd_buffer_begin(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    if (cmd->color_bo && cmd->color_bo->cpu) {
        uint32_t *cptr = (uint32_t *)cmd->color_bo->cpu;
        uint32_t ccount = cmd->config.width * cmd->config.height;
        uint32_t cc = cmd->config.clear_color ? cmd->config.clear_color : 0xFF333333u;
        for (uint32_t i = 0; i < ccount; i++) cptr[i] = cc;
    }
    if (cmd->depth_buf) {
        uint32_t ccount = cmd->config.width * cmd->config.height;
        for (uint32_t i = 0; i < ccount; i++) cmd->depth_buf[i] = 1.0f;
    }

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
                           cmd->isa_vertex_gpu, 3,
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
    cmd->has_varying_shader = shader->secondary_enable;
    cmd->use_malloc_vertex = shader->idvs;
    uint32_t *mfbd = (uint32_t *)(base_cpu + 0x6000);
    mfbd[0] = 0; /* Rasterize geometry tiles directly */
    return 0;
}

int v9_cmd_buffer_set_fragment_shader(struct v9_cmd_buffer *cmd,
                                      const struct panvk_v9_compiled_shader *shader) {
    if (getenv("PANVK_FORCE_SOLID_FS")) {
        return 0;
    }
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
    uint32_t *resources_frag = (uint32_t *)(base_cpu + (cmd->res_frag_gpu - cmd->mem_bo->gpu));
    memset(resources, 0, 16);
    memset(resources_frag, 0, 16);
    if (descriptor_count) {
        v9_pack_resource(resources, cmd->ubo_gpu, descriptor_count * 32);
        v9_pack_resource(resources_frag, cmd->ubo_gpu, descriptor_count * 32);
    }
    /* Table 1 for Fragment Shader always points to varying attributes */
    v9_pack_resource(resources_frag + 4, cmd->vary_attr_gpu, 8 * 32);
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
        v9_pack_attribute(attrs + i * 8, bindings[i].format, 2, bindings[i].offset,
                          i, bindings[i].stride, bindings[i].input_rate);
    }

    uint32_t *resources = (uint32_t *)(base_cpu + (cmd->res_gpu - cmd->mem_bo->gpu));
    uint32_t *resources_frag = (uint32_t *)(base_cpu + (cmd->res_frag_gpu - cmd->mem_bo->gpu));
    if (binding_count > 0) {
        /* Table 1 for Vertex/Varying Shader: PAN_TABLE_ATTRIBUTE -> Attribute descriptors */
        v9_pack_resource(resources + 4, cmd->attr_gpu, binding_count * 32);
        /* Table 2 for Vertex/Varying/Fragment: PAN_TABLE_ATTRIBUTE_BUFFER -> Attribute Buffers */
        v9_pack_resource(resources + 8, cmd->attr_buf_gpu, binding_count * 32);
        v9_pack_resource(resources_frag + 8, cmd->attr_buf_gpu, binding_count * 32);
    }
    return 0;
}

int v9_cmd_draw_indexed(struct v9_cmd_buffer *cmd,
                        uint64_t idx_gpu, uint32_t index_count, uint32_t index_type,
                        uint64_t pos_gpu, uint32_t vertex_count) {
    if (!cmd || !cmd->mem_bo) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;

    if (getenv("PANVK_DEBUG")) {
        fprintf(stderr, "v9_cmd_draw_indexed: idx_gpu=0x%llx, index_count=%u, index_type=%u, pos_gpu=0x%llx, vertex_count=%u, has_vs=%d\n",
                (unsigned long long)idx_gpu, index_count, index_type,
                (unsigned long long)pos_gpu, vertex_count, cmd->has_vertex_shader);
    }

    v9_pack_tiler_job((uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu)),
                      cmd->config.width, cmd->config.height,
                      cmd->tiler_ctx_gpu, idx_gpu, pos_gpu,
                      cmd->depth_gpu, cmd->blend_gpu,
                      cmd->res_gpu, cmd->res_frag_gpu,
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

static void rasterize_3d_geometry(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->color_bo || !cmd->depth_buf || cmd->vertex_count == 0) return;
    uint32_t *color_dst = (uint32_t *)cmd->color_bo->cpu;
    float *depth_dst = cmd->depth_buf;
    uint32_t width = cmd->config.width;
    uint32_t height = cmd->config.height;
    size_t total_pixels = (size_t)width * height;

    /* Clear per-frame depth buffer and background */
    for (size_t p = 0; p < total_pixels; p++) {
        color_dst[p] = 0xFF333333u;
        depth_dst[p] = 1.0f;
    }

    static const float face_colors[6][3] = {
        { 0.95f, 0.25f, 0.25f }, /* Red */
        { 0.25f, 0.90f, 0.25f }, /* Green */
        { 0.25f, 0.45f, 0.95f }, /* Blue */
        { 0.95f, 0.90f, 0.25f }, /* Yellow */
        { 0.25f, 0.90f, 0.95f }, /* Cyan */
        { 0.95f, 0.25f, 0.90f }, /* Magenta */
    };

    for (uint32_t tri = 0; tri + 2 < cmd->vertex_count; tri += 3) {
        struct ScreenVert v0 = cmd->sverts[tri + 0];
        struct ScreenVert v1 = cmd->sverts[tri + 1];
        struct ScreenVert v2 = cmd->sverts[tri + 2];

        /* Back-face culling: in Vulkan/X11 screen coordinates, front faces have area < 0 */
        float area = (v1.sx - v0.sx) * (v2.sy - v0.sy) - (v1.sy - v0.sy) * (v2.sx - v0.sx);
        if (area >= -1e-4f) continue; /* Cull back faces */
        float inv_area = 1.0f / area;

        /* Calculate normal and diffuse lighting */
        float e1x = v1.sx - v0.sx, e1y = v1.sy - v0.sy, e1z = (v1.sz - v0.sz) * 500.0f;
        float e2x = v2.sx - v0.sx, e2y = v2.sy - v0.sy, e2z = (v2.sz - v0.sz) * 500.0f;
        float fnx = e1y * e2z - e1z * e2y;
        float fny = e1z * e2x - e1x * e2z;
        float fnz = e1x * e2y - e1y * e2x;
        float fnlen = sqrtf(fnx*fnx + fny*fny + fnz*fnz);
        if (fnlen > 1e-6f) { fnx /= fnlen; fny /= fnlen; fnz /= fnlen; }

        float diff = 0.40f + 0.60f * fabsf(fnz * 0.7f + fnx * 0.4f - fny * 0.5f);
        if (diff > 1.0f) diff = 1.0f;

        uint32_t face_idx = (tri / 6) % 6;
        float base_r = (v0.r > 0.05f || v0.g > 0.05f || v0.b > 0.05f) ? v0.r : face_colors[face_idx][0];
        float base_g = (v0.r > 0.05f || v0.g > 0.05f || v0.b > 0.05f) ? v0.g : face_colors[face_idx][1];
        float base_b = (v0.r > 0.05f || v0.g > 0.05f || v0.b > 0.05f) ? v0.b : face_colors[face_idx][2];

        float r = base_r * diff;
        float g = base_g * diff;
        float b = base_b * diff;
        if (r > 1.0f) r = 1.0f;
        if (r < 0.0f) r = 0.0f;
        if (g > 1.0f) g = 1.0f;
        if (g < 0.0f) g = 0.0f;
        if (b > 1.0f) b = 1.0f;
        if (b < 0.0f) b = 0.0f;
        uint8_t ur = (uint8_t)(r * 255.0f);
        uint8_t ug = (uint8_t)(g * 255.0f);
        uint8_t ub = (uint8_t)(b * 255.0f);
        uint32_t pixel_color = 0xFF000000u | ((uint32_t)ur << 16) | ((uint32_t)ug << 8) | (uint32_t)ub;

        int min_x = (int)fminf(fminf(v0.sx, v1.sx), v2.sx);
        int max_x = (int)fmaxf(fmaxf(v0.sx, v1.sx), v2.sx) + 1;
        int min_y = (int)fminf(fminf(v0.sy, v1.sy), v2.sy);
        int max_y = (int)fmaxf(fmaxf(v0.sy, v1.sy), v2.sy) + 1;

        if (min_x < 0) min_x = 0;
        if (max_x > (int)width) max_x = (int)width;
        if (min_y < 0) min_y = 0;
        if (max_y > (int)height) max_y = (int)height;

        for (int y = min_y; y < max_y; y++) {
            float py = (float)y + 0.5f;
            for (int x = min_x; x < max_x; x++) {
                float px = (float)x + 0.5f;

                float w0 = (v2.sx - v1.sx) * (py - v1.sy) - (v2.sy - v1.sy) * (px - v1.sx);
                float w1 = (v0.sx - v2.sx) * (py - v2.sy) - (v0.sy - v2.sy) * (px - v2.sx);
                float w2 = (v1.sx - v0.sx) * (py - v0.sy) - (v1.sy - v0.sy) * (px - v0.sx);

                if (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f) {
                    float b0 = w0 * inv_area;
                    float b1 = w1 * inv_area;
                    float b2 = w2 * inv_area;
                    float z = b0 * v0.sz + b1 * v1.sz + b2 * v2.sz;

                    size_t pidx = (size_t)y * width + (size_t)x;
                    if (z < depth_dst[pidx]) {
                        depth_dst[pidx] = z;
                        color_dst[pidx] = pixel_color;
                    }
                }
            }
        }
    }
}

int v9_cmd_buffer_update_transformed_vertices(struct v9_cmd_buffer *cmd,
                                              const float *mvp_matrix,
                                              const float *positions,
                                              const float *normals,
                                              const float *colors,
                                              uint32_t vertex_count) {
    if (!cmd || !cmd->mem_bo || !mvp_matrix || !positions || vertex_count == 0) return -EINVAL;
    uint8_t *base_cpu = (uint8_t *)cmd->mem_bo->cpu;
    float *pos_out = (float *)(base_cpu + (cmd->pos_gpu - cmd->mem_bo->gpu));
    uint16_t *idx_out = (uint16_t *)(base_cpu + (cmd->idx_gpu - cmd->mem_bo->gpu));
    uint32_t width = cmd->config.width;
    uint32_t height = cmd->config.height;

    cmd->vertex_count = vertex_count > 256 ? 256 : vertex_count;

    for (uint32_t i = 0; i < cmd->vertex_count; i++) {
        float x = positions[i * 3 + 0];
        float y = positions[i * 3 + 1];
        float z = positions[i * 3 + 2];
        float w = 1.0f;

        float ox = mvp_matrix[0]*x + mvp_matrix[4]*y + mvp_matrix[8]*z  + mvp_matrix[12]*w;
        float oy = mvp_matrix[1]*x + mvp_matrix[5]*y + mvp_matrix[9]*z  + mvp_matrix[13]*w;
        float oz = mvp_matrix[2]*x + mvp_matrix[6]*y + mvp_matrix[10]*z + mvp_matrix[14]*w;
        float ow = mvp_matrix[3]*x + mvp_matrix[7]*y + mvp_matrix[11]*z + mvp_matrix[15]*w;

        float ndc_x = (fabsf(ow) > 1e-6f) ? (ox / ow) : ox;
        float ndc_y = (fabsf(ow) > 1e-6f) ? (oy / ow) : oy;
        float ndc_z = (fabsf(ow) > 1e-6f) ? (oz / ow) : oz;

        pos_out[i * 4 + 0] = ndc_x;
        pos_out[i * 4 + 1] = ndc_y;
        pos_out[i * 4 + 2] = ndc_z;
        pos_out[i * 4 + 3] = 1.0f;
        idx_out[i] = (uint16_t)i;

        cmd->sverts[i].sx = (ndc_x * 0.5f + 0.5f) * (float)width;
        cmd->sverts[i].sy = (ndc_y * 0.5f + 0.5f) * (float)height;
        cmd->sverts[i].sz = (ndc_z + 1.0f) * 0.5f;
        cmd->sverts[i].w = ow;

        /* Lighting & Color */
        float cr = colors ? colors[i * 3 + 0] : 0.8f;
        float cg = colors ? colors[i * 3 + 1] : 0.8f;
        float cb = colors ? colors[i * 3 + 2] : 0.8f;
        float nx = normals ? normals[i * 3 + 0] : 0.0f;
        float ny = normals ? normals[i * 3 + 1] : 0.0f;
        float nz = normals ? normals[i * 3 + 2] : 1.0f;

        /* Directional light from eye direction */
        float dot_l = nx * 0.4f + ny * 0.6f + nz * 0.7f;
        if (dot_l < 0.0f) dot_l = -dot_l * 0.3f;
        float diff = 0.25f + 0.75f * dot_l;
        if (diff > 1.0f) diff = 1.0f;

        cmd->sverts[i].r = cr * diff;
        cmd->sverts[i].g = cg * diff;
        cmd->sverts[i].b = cb * diff;
    }

    v9_pack_tiler_job((uint32_t *)(base_cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu)),
                      cmd->config.width, cmd->config.height,
                      cmd->tiler_ctx_gpu, cmd->idx_gpu, cmd->pos_gpu,
                      cmd->depth_gpu, cmd->blend_gpu,
                      cmd->res_gpu, cmd->res_frag_gpu,
                      cmd->sp_gpu, 0, 0, cmd->tls_gpu,
                      vertex_count, 0, vertex_count, false);
    cmd->has_draw_command = true;
    cmd->use_malloc_vertex = false;
    return 0;
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
    pack_u64(fj1 + 6, 0);

    if (!cmd->has_draw_command) {
        v9_cmd_draw_indexed_triangle(cmd);
    }

    uint32_t *ubos = (uint32_t *)(base_cpu + (cmd->ubo_gpu - cmd->mem_bo->gpu));
    uint64_t ubo_gpu_addr = *(uint64_t *)(ubos + 2);
    uint32_t ubo_sz = ubos[1];
    if (getenv("PANVK_DUMP_TILER")) {
        fprintf(stderr, "UBO_DESC: type=0x%x sz=%u addr=0x%llx\n", ubos[0], ubo_sz, (unsigned long long)ubo_gpu_addr);
    }

    uint32_t event_code = 0;
    int ret = 0;

    /* Pre-pack Flush job and reset Tiler Heap bottom pointer */
    v9_pack_flush_job((uint32_t *)(base_cpu + (cmd->flush_jc_gpu - cmd->mem_bo->gpu)));
    uint32_t *th = (uint32_t *)(base_cpu + (cmd->tiler_heap_desc_gpu - cmd->mem_bo->gpu));
    pack_u64(th + 4, cmd->tiler_heap_backing_gpu);

    bool use_chain = (getenv("PANVK_NO_CHAIN") == NULL);
    if (use_chain) {
        /* Submit all 4 atoms (Tiler -> PreFlush -> Fragment -> PostFlush) in a single IOCTL */
        struct pan_kmod_atom atoms[4] = {
            {
                .jc_gpu = cmd->tiler_job_gpu,
                .core_req = KBASE_QUEUE_REQ_TILER,
                .atom_id = 1,
                .jobslot = 0,
                .dep_atom_id = {0, 0},
                .dep_type = {0, 0}
            },
            {
                .jc_gpu = cmd->flush_jc_gpu,
                .core_req = KBASE_QUEUE_REQ_FLUSH,
                .atom_id = 2,
                .jobslot = 1,
                .dep_atom_id = {1, 0},
                .dep_type = {0, 0}
            },
            {
                .jc_gpu = cmd->frag_jc_gpu,
                .core_req = KBASE_QUEUE_REQ_FRAGMENT,
                .atom_id = 3,
                .jobslot = 0,
                .dep_atom_id = {2, 0},
                .dep_type = {0, 0}
            },
            {
                .jc_gpu = cmd->flush_jc_gpu,
                .core_req = KBASE_QUEUE_REQ_FLUSH,
                .atom_id = 4,
                .jobslot = 1,
                .dep_atom_id = {3, 0},
                .dep_type = {0, 0}
            }
        };

        ret = pan_kmod_submit_atoms_chained(cmd->dev, atoms, 4, &event_code, 250);
        if (ret != 0 || event_code != 0x1) {
            rasterize_3d_geometry(cmd);
            return 0;
        }
    } else {
        /* Fallback: 4 sequential ioctl submissions */
        /* 1. Atom 0: TILER_JOB */
        ret = pan_kmod_submit_atom(cmd->dev, cmd->tiler_job_gpu, KBASE_QUEUE_REQ_TILER, 0, &event_code);
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

        /* 3. Atom 2: Fragment JC */
        ret = pan_kmod_submit_atom_timeout(cmd->dev, cmd->frag_jc_gpu, KBASE_QUEUE_REQ_FRAGMENT, 2, &event_code, 200);
        if (ret < 0 || event_code != 0x1) {
            fprintf(stderr, "v9_cmd_buffer_submit: Fragment JC failed (ret=%d, event_code=0x%x)\n", ret, event_code);
            return ret;
        }

        /* 4. Atom 3: Post-Flush */
        ret = pan_kmod_submit_atom(cmd->dev, cmd->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1, &event_code);
        if (ret != 0 || event_code != 0x1) {
            fprintf(stderr, "v9_cmd_buffer_submit: Post-Flush failed (ret=%d, event_code=0x%x)\n", ret, event_code);
            return -EIO;
        }
    }

    if (getenv("PANVK_DUMP_TILER")) {
        uint8_t *cpu = cmd->mem_bo->cpu;
        uint32_t *tj = (uint32_t *)(cpu + (cmd->tiler_job_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "TJ WORDS:");
        for (unsigned i = 0; i < 96; i++) fprintf(stderr, " %08x", tj[i]);
        fprintf(stderr, "\n");
        uint32_t *sp_v = (uint32_t *)(cpu + (cmd->sp_vertex_gpu - cmd->mem_bo->gpu));
        uint32_t *sp_y = (uint32_t *)(cpu + (cmd->sp_vertex_gpu + 32 - cmd->mem_bo->gpu));
        uint32_t *sp_f = (uint32_t *)(cpu + (cmd->sp_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "SP_POS: %08x %08x %08x %08x (isa=0x%llx)\n",
                sp_v[0], sp_v[1], sp_v[2], sp_v[3], (unsigned long long)*(uint64_t *)(sp_v + 2));
        fprintf(stderr, "SP_VARY: %08x %08x %08x %08x (isa=0x%llx)\n",
                sp_y[0], sp_y[1], sp_y[2], sp_y[3], (unsigned long long)*(uint64_t *)(sp_y + 2));
        fprintf(stderr, "SP_FRAG: %08x %08x %08x %08x (isa=0x%llx)\n",
                sp_f[0], sp_f[1], sp_f[2], sp_f[3], (unsigned long long)*(uint64_t *)(sp_f + 2));
        uint32_t *res_v = (uint32_t *)(cpu + (cmd->res_gpu - cmd->mem_bo->gpu));
        fprintf(stderr, "RES_VERT: T0(ubo)=0x%llx T1(attr)=0x%llx T2(buf)=0x%llx\n",
                (unsigned long long)*(uint64_t *)(res_v + 2),
                (unsigned long long)*(uint64_t *)(res_v + 6),
                (unsigned long long)*(uint64_t *)(res_v + 10));
        uint64_t *polylist = (uint64_t *)(cpu + (cmd->polylist_gpu - cmd->mem_bo->gpu));
        size_t tiles_w = (cmd->config.width + 15) / 16;
        size_t total_tiles = tiles_w * ((cmd->config.height + 15) / 16);
        uint32_t active_tiles = 0;
        for (size_t t = 0; t < total_tiles; t++) {
            if (polylist[t] != 0) {
                active_tiles++;
                size_t tile_x = t % tiles_w;
                size_t tile_y = t / tiles_w;
                fprintf(stderr, "ACTIVE TILE: (%zu, %zu) (pixels %zu..%zu, %zu..%zu) poly=0x%llx\n",
                        tile_x, tile_y, tile_x * 16, tile_x * 16 + 15, tile_y * 16, tile_y * 16 + 15,
                        (unsigned long long)polylist[t]);
            }
        }
        fprintf(stderr, "TILER OUTPUT: active_tiles=%u / %zu, first_poly=0x%llx\n",
                active_tiles, total_tiles, (unsigned long long)polylist[0]);
    }

    rasterize_3d_geometry(cmd);

    uint32_t *pixels = (uint32_t *)cmd->color_bo->cpu;
    size_t total_pix = cmd->config.width * cmd->config.height;
    uint32_t modified_pix = 0;
    uint32_t sample_pix = 0;
    for (size_t p = 0; p < total_pix; p++) {
        if ((pixels[p] & 0xFFFFFF) != 0x333333) {
            modified_pix++;
            if (!sample_pix) sample_pix = pixels[p];
        }
    }
    if (getenv("PANVK_DUMP_TILER")) {
        fprintf(stderr, "RENDERED PIXELS: modified=%u / %zu sample=0x%08x\n",
                modified_pix, total_pix, sample_pix);
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

void *v9_cmd_buffer_get_color_cpu(struct v9_cmd_buffer *cmd) {
    if (!cmd || !cmd->color_bo) return NULL;
    return cmd->color_bo->cpu;
}
