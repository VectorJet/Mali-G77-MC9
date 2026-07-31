/*
 * Valhall v9 GenXML Descriptor Pack Helpers for Mali-G77 MC9
 * Encapsulates hardware descriptor layouts (MFBD, DCD, Tiler Ctx, TJ, FJ)
 */

#ifndef V9_PACK_H
#define V9_PACK_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void pack_u64(uint32_t *ptr, uint64_t val) {
    ptr[0] = (uint32_t)(val & 0xFFFFFFFFu);
    ptr[1] = (uint32_t)(val >> 32);
}

static inline void v9_pack_blend(uint32_t *bl) {
    memset(bl, 0, 32);
    bl[0] = (1u << 9);                                  /* Enable=1 */
    bl[1] = (2u << 0) | (2u << 4) | (1u << 8) |         /* RGB: A=Src, B=Src, C=Zero */
            ((2u << 0) | (2u << 4) | (1u << 8)) << 12 | /* Alpha: same */
            (0xFu << 28);                               /* Color Mask = RGBA */
    bl[2] = (2u << 0) | (3u << 3) | (0u << 16);         /* Mode=2, num_comps-1=3, RT=0 */
    bl[3] = (237u << 12) | 0u;                          /* Conversion: RGBA8_TB | RGBA */
}

static inline void v9_pack_tls(uint32_t *ls, uint64_t tls_base) {
    memset(ls, 0, 32);
    ls[0] = 0;
    ls[1] = 0x80000000u;
    ls[2] = (uint32_t)(tls_base & 0xFFFFFFFFu);
    ls[3] = (uint32_t)((tls_base >> 32) & 0xFFFFu);
}

static inline void v9_pack_depth(uint32_t *zs) {
    memset(zs, 0, 32);
    zs[0] = (7u << 0) | (7u << 4) | (7u << 16);
    zs[4] = (1u << 22) | (7u << 29);
}

static inline void v9_pack_shader_program(uint32_t *sp, uint64_t isa_gpu,
                                          uint32_t work_reg_count, uint64_t preload,
                                          bool primary_shader, bool contains_barrier,
                                          bool ftz_fp16, bool ftz_fp32) {
    memset(sp, 0, 16);
    uint32_t register_allocation = work_reg_count <= 32 ? 2u : 0u;
    uint32_t ftz_mode = ftz_fp32 ? (ftz_fp16 ? 2u : 1u) : 0u;
    sp[0] = (8u << 0) | (2u << 4) |
            ((uint32_t)primary_shader << 8) | (ftz_mode << 17) |
            ((uint32_t)contains_barrier << 28) | (register_allocation << 30);
    sp[1] = (uint32_t)(preload >> 48);
    pack_u64(sp + 2, isa_gpu);
}

static inline void v9_pack_tiler_heap(uint32_t *th, uint64_t backing_gpu, uint32_t size) {
    memset(th, 0, 32);
    th[0] = (9u << 0) | (2u << 4) | (0u << 8);
    th[1] = size;
    pack_u64(th + 2, backing_gpu);
    pack_u64(th + 4, backing_gpu);
    pack_u64(th + 6, backing_gpu + size);
}

static inline void v9_pack_tiler_ctx(uint32_t *tc, uint64_t polylist_gpu, uint32_t width,
                                     uint32_t height, uint64_t heap_desc_gpu) {
    memset(tc, 0, 192);
    pack_u64(tc + 0, polylist_gpu | (1ULL << 48));
    tc[2] = 0x1; /* Hierarchy mask = 1 (Level 0 16x16 bin) */
    tc[3] = (width - 1) | ((height - 1) << 16);
    pack_u64(tc + 6, heap_desc_gpu);
}

static inline void v9_pack_rt0(uint32_t *rt0, uint64_t color_gpu, uint32_t width, uint32_t clear_color) {
    memset(rt0, 0, 64);
    rt0[0] = (1 << 26);
    uint32_t swizzle_rgba = (0 << 0) | (1 << 3) | (2 << 6) | (3 << 9);
    rt0[1] = (1 << 0) | (19 << 3) | (2 << 8) | (1 << 15) | (swizzle_rgba << 16) | (1u << 31);
    pack_u64(rt0 + 8, color_gpu);
    rt0[10] = width * 4;
    rt0[12] = clear_color;
    rt0[13] = clear_color;
    rt0[14] = clear_color;
    rt0[15] = clear_color;
}

static inline void v9_pack_mfbd(uint32_t *mfbd, uint32_t width, uint32_t height,
                                uint64_t dcd_gpu, uint64_t tiler_ctx_gpu, uint64_t sampleloc_gpu) {
    memset(mfbd, 0, 128);
    mfbd[0] = 1;
    pack_u64(mfbd + 4, sampleloc_gpu);
    pack_u64(mfbd + 6, dcd_gpu);
    uint32_t *params = mfbd + 8;
    params[0] = (width - 1) | ((height - 1) << 16);
    params[1] = 0;
    params[2] = (width - 1) | ((height - 1) << 16);
    params[3] = (2 << 6) | (1 << 19) | (1 << 24);
    params[4] = (1 << 16);
    pack_u64(params + 6, tiler_ctx_gpu);
}

static inline void v9_pack_dcd(uint32_t *dcd, uint64_t depth_gpu, uint64_t blend_gpu,
                               uint64_t res_gpu, uint64_t sp_gpu, uint64_t tls_gpu) {
    memset(dcd, 0, 3 * 128);
    dcd[0] = 0x00000228; /* pixel_kill=WEAK_EARLY, zs_update=STRONG_EARLY */
    dcd[1] = 0x0000FFFF; /* Sample mask 0xFFFF */
    dcd[7] = 0x3F800000;
    pack_u64(dcd + 10, depth_gpu);
    pack_u64(dcd + 12, 1ULL | blend_gpu);
    pack_u64(dcd + 24, 1ULL | res_gpu);
    pack_u64(dcd + 26, sp_gpu);
    pack_u64(dcd + 28, tls_gpu);
    pack_u64(dcd + 30, 0);
}

static inline void v9_pack_tiler_job(uint32_t *vt, uint32_t width, uint32_t height,
                                      uint64_t tiler_ctx_gpu, uint64_t idx_gpu, uint64_t pos_gpu,
                                      uint64_t depth_gpu, uint64_t blend_gpu, uint64_t res_gpu,
                                      uint64_t sp_gpu, uint64_t tls_gpu) {
    memset(vt, 0, 256);
    vt[4] = (1u << 0) | (7u << 1); /* Type = 7 (TILER_JOB) */
    pack_u64(vt + 6, 0);           /* Next = 0 */
    vt[8] = 0x38008;               /* Triangles + Index Type U16 */
    vt[9] = 0x00008100;
    vt[10] = 1;                    /* Primitive count / base index = 1 */
    vt[11] = 3;                    /* Index count = 3 */
    vt[12] = 1;                    /* Instance count = 1 */
    vt[13] = 3;                    /* Vertex count hint = 3 */
    pack_u64(vt + 14, tiler_ctx_gpu);
    vt[17] = 4;
    pack_u64(vt + 24, tiler_ctx_gpu);
    vt[27] = (width - 1) | ((height - 1) << 16);
    pack_u64(vt + 28, 0x3f800000ULL);
    pack_u64(vt + 30, idx_gpu);

    uint32_t *dw = vt + 32;
    dw[0] = (1u << 0) | (1u << 1) | (1u << 6);
    dw[1] = 0xFFFF | (0x1u << 16);
    uint64_t V = pos_gpu >> 6;
    dw[2] = (uint32_t)((V & 0x03FFFFFFu) << 6); /* Pointer bits 25:0, Packet=0 */
    dw[3] = (uint32_t)((V >> 26) & 0xFFFFFFFFu); /* Pointer bits 57:26 */
    dw[4] = (16u << 16);
    dw[7] = 0x3F800000;
    pack_u64(dw + 10, depth_gpu);
    pack_u64(dw + 12, 1ULL | blend_gpu);
    uint32_t *se = dw + 16;
    se[0] = 0;
    se[1] = 0;
    pack_u64(se + 8, 1ULL | res_gpu);
    pack_u64(se + 10, sp_gpu);
    pack_u64(se + 12, tls_gpu);
    pack_u64(se + 14, 0);
}

static inline void v9_pack_frag_job_chain(uint32_t *fj1, uint32_t *fj2,
                                           uint64_t mfbd1_gpu, uint64_t mfbd2_gpu,
                                           uint64_t fj2_gpu, uint32_t width, uint32_t height) {
    (void)fj2_gpu;
    /* Job 2: End-of-frame / completion pass */
    memset(fj2, 0, 128);
    fj2[4] = (2u << 16) | (9u << 1);  /* 0x00020012: index 2 in chain, Type 9 */
    fj2[5] = 1;                        /* Bit 0 = 1 */
    pack_u64(fj2 + 6, 0);              /* Next = NULL */
    fj2[8] = 0;
    fj2[9] = 0x00030003;
    pack_u64(fj2 + 10, mfbd2_gpu | 0x03u);

    /* Job 1: Main polygon-list rendering pass */
    memset(fj1, 0, 128);
    fj1[4] = (1u << 16) | (9u << 1);   /* 0x00010012: index 1 in chain, Type 9 */
    fj1[5] = 0;
    pack_u64(fj1 + 6, 0);        /* Next = NULL */
    fj1[8] = 0;
    fj1[9] = ((width - 1) >> 4) | (((height - 1) >> 4) << 16);
    pack_u64(fj1 + 10, mfbd1_gpu | 0x01u); /* Polygon List Mode */
}

static inline void v9_pack_mfbd2(uint32_t *mfbd2, uint32_t width, uint32_t height,
                                 uint64_t dcd2_gpu, uint64_t tiler_ctx_gpu, uint64_t sampleloc_gpu) {
    memset(mfbd2, 0, 128);
    mfbd2[0] = 0;
    mfbd2[2] = 0x00010000;
    pack_u64(mfbd2 + 4, sampleloc_gpu);
    pack_u64(mfbd2 + 6, dcd2_gpu);

    uint32_t *params = mfbd2 + 8;
    params[0] = (width - 1) | ((height - 1) << 16);
    params[1] = 0;
    params[2] = (width - 1) | ((height - 1) << 16);
    params[3] = 0x01039000; /* No color RT */
    params[4] = 0x00200000;
    pack_u64(params + 6, tiler_ctx_gpu);
}

static inline void v9_pack_dcd2(uint32_t *dcd2, uint64_t tls_gpu) {
    memset(dcd2, 0, 128);
    pack_u64(dcd2 + 28, tls_gpu);
}

static inline void v9_pack_flush_job(uint32_t *fl) {
    memset(fl, 0, 64);
    fl[4] = (3u << 1);            /* Type = 3 (Cache Flush) */
    fl[8] = 0xFFFFFFFFu;          /* Invalidate/Clean all core caches */
    fl[9] = 0xFFFFFFFFu;          /* Invalidate/Clean all L2 caches */
}

#ifdef __cplusplus
}
#endif

#endif /* V9_PACK_H */
