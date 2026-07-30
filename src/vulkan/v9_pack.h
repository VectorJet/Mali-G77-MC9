/*
 * Valhall v9 GenXML Descriptor Pack Helpers for Mali-G77 MC9
 * Encapsulates hardware descriptor layouts (MFBD, DCD, Tiler Ctx, TJ, FJ)
 */

#ifndef V9_PACK_H
#define V9_PACK_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void v9_pack_blend(uint32_t *bl) {
    memset(bl, 0, 16);
    bl[0] = (1u << 9);
    bl[1] = (2u << 0) | (2u << 4) | (1u << 8) | ((2u << 0) | (2u << 4) | (1u << 8)) << 12 | (0xFu << 28);
    bl[2] = (2u << 0) | (3u << 3) | (0u << 16);
    bl[3] = (237u << 12);
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

static inline void v9_pack_shader_program(uint32_t *sp, uint64_t isa_gpu) {
    memset(sp, 0, 16);
    sp[0] = (8u << 0) | (2u << 4) | (1u << 8) | (1u << 28) | (2u << 30);
    sp[1] = 0;
    *(uint64_t *)(sp + 2) = isa_gpu;
}

static inline void v9_pack_tiler_heap(uint32_t *th, uint64_t backing_gpu, uint32_t size) {
    memset(th, 0, 32);
    th[0] = (9u << 0) | (2u << 4) | (0u << 8);
    th[1] = size;
    *(uint64_t *)(th + 2) = backing_gpu;
    *(uint64_t *)(th + 4) = backing_gpu;
    *(uint64_t *)(th + 6) = backing_gpu + size;
}

static inline void v9_pack_tiler_ctx(uint32_t *tc, uint64_t polylist_gpu, uint32_t width,
                                     uint32_t height, uint64_t heap_desc_gpu) {
    memset(tc, 0, 192);
    *(uint64_t *)(tc + 0) = polylist_gpu;
    tc[2] = 0x1; /* Hierarchy mask = 1 */
    tc[3] = (width - 1) | ((height - 1) << 16);
    *(uint64_t *)(tc + 6) = heap_desc_gpu;
}

static inline void v9_pack_rt0(uint32_t *rt0, uint64_t color_gpu, uint32_t width, uint32_t clear_color) {
    memset(rt0, 0, 64);
    rt0[0] = (1 << 26);
    uint32_t swizzle_rgba = (0 << 0) | (1 << 3) | (2 << 6) | (3 << 9);
    rt0[1] = (1 << 0) | (19 << 3) | (2 << 8) | (1 << 15) | (swizzle_rgba << 16) | (1u << 31);
    *(uint64_t *)((uint8_t *)rt0 + 0x20) = color_gpu;
    *(uint32_t *)((uint8_t *)rt0 + 0x28) = width * 4;
    *(uint32_t *)((uint8_t *)rt0 + 0x30) = clear_color;
    *(uint32_t *)((uint8_t *)rt0 + 0x34) = clear_color;
    *(uint32_t *)((uint8_t *)rt0 + 0x38) = clear_color;
    *(uint32_t *)((uint8_t *)rt0 + 0x3C) = clear_color;
}

static inline void v9_pack_mfbd(uint32_t *mfbd, uint32_t width, uint32_t height,
                                uint64_t dcd_gpu, uint64_t tiler_ctx_gpu, uint64_t sampleloc_gpu) {
    memset(mfbd, 0, 128);
    mfbd[0] = 1;
    *(uint64_t *)((uint8_t *)mfbd + 0x10) = sampleloc_gpu;
    *(uint64_t *)((uint8_t *)mfbd + 0x18) = dcd_gpu;
    uint32_t *params = mfbd + 8;
    params[0] = (width - 1) | ((height - 1) << 16);
    params[1] = 0;
    params[2] = (width - 1) | ((height - 1) << 16);
    params[3] = (2 << 6) | (1 << 19) | (1 << 24);
    params[4] = (1 << 16);
    *(uint64_t *)(params + 6) = tiler_ctx_gpu;
}

static inline void v9_pack_dcd(uint32_t *dcd, uint64_t depth_gpu, uint64_t blend_gpu,
                               uint64_t res_gpu, uint64_t sp_gpu, uint64_t tls_gpu) {
    memset(dcd, 0, 128);
    dcd[0] = 0x00000228; /* pixel_kill=WEAK_EARLY, zs_update=STRONG_EARLY */
    dcd[1] = 0x0000FFFF; /* Sample mask 0xFFFF */
    dcd[7] = 0x3F800000;
    *(uint64_t *)(dcd + 10) = depth_gpu;
    *(uint64_t *)(dcd + 12) = 1ULL | blend_gpu;
    *(uint64_t *)(dcd + 24) = 1ULL | res_gpu;
    *(uint64_t *)(dcd + 26) = sp_gpu;
    *(uint64_t *)(dcd + 28) = tls_gpu;
    *(uint64_t *)(dcd + 30) = 0;
}

static inline void v9_pack_tiler_job(uint32_t *vt, uint32_t width, uint32_t height,
                                      uint64_t tiler_ctx_gpu, uint64_t idx_gpu, uint64_t pos_gpu,
                                      uint64_t depth_gpu, uint64_t blend_gpu, uint64_t res_gpu,
                                      uint64_t sp_gpu, uint64_t tls_gpu) {
    memset(vt, 0, 256);
    vt[4] = (1u << 0) | (7u << 1); /* Type = 7 (TILER_JOB) */
    *(uint64_t *)(vt + 6) = 0;     /* Next = 0 */
    vt[8] = 0x38008;               /* Triangles + Index Type U16 */
    vt[9] = 0;
    vt[10] = 0;                    /* First Index = 0 */
    vt[11] = 3;                    /* Index count = 3 */
    vt[12] = 1;                    /* Instance count = 1 */
    vt[13] = 3;                    /* Vertex count hint = 3 */
    *(uint64_t *)(vt + 14) = tiler_ctx_gpu;
    vt[17] = 4;
    *(uint64_t *)(vt + 24) = tiler_ctx_gpu;
    vt[27] = (width - 1) | ((height - 1) << 16);
    *(uint64_t *)(vt + 28) = 0x3f800000ULL;
    *(uint64_t *)(vt + 30) = idx_gpu;

    uint32_t *dw = vt + 32;
    dw[0] = (1u << 0) | (1u << 1) | (1u << 6);
    dw[1] = 0xFFFF | (0x1u << 16);
    uint64_t V = pos_gpu >> 6;
    dw[2] = (uint32_t)((V & 0x03FFFFFFu) << 6); /* Pointer bits 25:0, Packet=0 */
    dw[3] = (uint32_t)((V >> 26) & 0xFFFFFFFFu); /* Pointer bits 57:26 */
    dw[4] = (16u << 16);
    dw[7] = 0x3F800000;
    *(uint64_t *)(dw + 10) = depth_gpu;
    *(uint64_t *)(dw + 12) = 1ULL | blend_gpu;
    uint32_t *se = dw + 16;
    se[0] = 0;
    se[1] = 0;
    *(uint64_t *)(se + 8) = 1ULL | res_gpu;
    *(uint64_t *)(se + 10) = sp_gpu;
    *(uint64_t *)(se + 12) = tls_gpu;
    *(uint64_t *)(se + 14) = 0;
}

static inline void v9_pack_frag_job(uint32_t *fj, uint64_t mfbd_gpu) {
    memset(fj, 0, 128);
    fj[4] = (1u << 0) | (9u << 1);              /* Type = 9 (Fragment JC) */
    *(uint64_t *)(fj + 10) = mfbd_gpu | 0x01u;  /* Polygon List Mode flags = 0x01 */
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
