#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "v9_builder.h"

/* Pre-compiled Valhall fragment shader producing solid green (0xFF00FF00) (56 bytes) */
static const uint8_t k_valhall_green_fs[] = {
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x10, 0x01,
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc1, 0xa4, 0x00,
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x10, 0x01,
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc3, 0xa4, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x40,
    0x3c, 0xd0, 0xea, 0x00, 0x02, 0xbc, 0x7d, 0x68,
    0xf0, 0x00, 0x3c, 0x32, 0x08, 0x40, 0x7f, 0x78,
};

struct v9_framebuffer *v9_framebuffer_create(struct kbase_dev *dev, uint32_t width, uint32_t height) {
    if (!dev || width == 0 || height == 0) return NULL;

    struct v9_framebuffer *fb = calloc(1, sizeof(*fb));
    if (!fb) return NULL;

    fb->width = width;
    fb->height = height;

    /* Allocate main metadata memory BO (1 MiB / 256 pages) */
    size_t mem_size = 1024 * 1024;
    fb->mem_bo = kbase_bo_alloc(dev, mem_size, KBASE_BO_PROT_READ | KBASE_BO_PROT_WRITE);
    if (!fb->mem_bo) {
        free(fb);
        return NULL;
    }
    memset(fb->mem_bo->cpu, 0, mem_size);

    /* Assign GPU VAs and offsets matching working replay harness */
    uint64_t base_gva = fb->mem_bo->gpu;
    uint8_t *base_cpu = (uint8_t *)fb->mem_bo->cpu;
    uint64_t color_off = (width * height * 4 > 0x1000) ? 0x40000 : 0xA000;
    uint64_t color_gpu = base_gva + color_off;
    uint8_t *color_cpu = base_cpu + color_off;
    memset(color_cpu, 0xef, width * height * 4);

    /* Allocate executable page for shader ISA */
    fb->exec_bo = kbase_bo_alloc(dev, 4096, KBASE_BO_PROT_READ | KBASE_BO_PROT_WRITE | KBASE_BO_PROT_EXEC);
    if (!fb->exec_bo) {
        kbase_bo_free(fb->mem_bo);
        free(fb);
        return NULL;
    }

    fb->mfbd_gva                 = base_gva + 0x6000;
    fb->rt0_gpu                  = base_gva + 0x6080;
    fb->polylist_gpu             = base_gva + 0x7000;
    fb->isa_gpu                  = fb->exec_bo->gpu;
    fb->dcd_gpu                  = base_gva + 0xC100;
    fb->sp_gpu                   = base_gva + 0xCC00;
    fb->res_gpu                  = base_gva + 0xD200;
    fb->flush_jc_gpu             = base_gva + 0xD400;
    fb->tiler_heap_desc_gpu      = base_gva + 0xD500;
    fb->tiler_ctx_gpu            = base_gva + 0xD600;
    fb->pos_gpu                  = base_gva + 0xE000;
    fb->blend_gpu                = base_gva + 0xE040;
    fb->depth_gpu                = base_gva + 0xE060;
    fb->tls_gpu                  = base_gva + 0xE0A0;
    fb->idx_gpu                  = base_gva + 0xE0C0;
    fb->tiler_job_gpu            = base_gva + 0xE200;
    fb->frag_jc_gpu              = base_gva + 0xE380;
    fb->tiler_heap_backing_gpu   = base_gva + 0x80000;

    /* Copy Fragment Shader ISA to executable GPU page */
    memcpy(fb->exec_bo->cpu, k_valhall_green_fs, sizeof(k_valhall_green_fs));

    /* Initialize Blend descriptor */
    uint32_t *bl = (uint32_t *)(base_cpu + 0xE040);
    bl[0] = (1u << 9);
    bl[1] = (2u << 0) | (2u << 4) | (1u << 8) | ((2u << 0) | (2u << 4) | (1u << 8)) << 12 | (0xFu << 28);
    bl[2] = (2u << 0) | (3u << 3) | (0u << 16);
    bl[3] = (237u << 12);

    /* Initialize TLS descriptor */
    uint32_t *ls = (uint32_t *)(base_cpu + 0xE0A0);
    memset(ls, 0, 32);
    ls[0] = 0;
    ls[1] = 0x80000000u;
    uint64_t tls_base = base_gva + 0x8000;
    ls[2] = (uint32_t)(tls_base & 0xFFFFFFFFu);
    ls[3] = (uint32_t)((tls_base >> 32) & 0xFFFFu);
    uint32_t *zs = (uint32_t *)(base_cpu + 0xE060);
    zs[0] = (7u << 0) | (7u << 4) | (7u << 16);
    zs[4] = (1u << 22) | (7u << 29);

    /* Initialize Position buffer (NDC full-screen triangle) */
    float *pos = (float *)(base_cpu + 0xE000);
    pos[0] = -1.0f; pos[1] = -1.0f; pos[2] = 0.5f; pos[3] = 1.0f;
    pos[4] =  3.0f; pos[5] = -1.0f; pos[6] = 0.5f; pos[7] = 1.0f;
    pos[8] = -1.0f; pos[9] =  3.0f; pos[10] = 0.5f; pos[11] = 1.0f;

    /* Initialize Index buffer */
    uint16_t *idx = (uint16_t *)(base_cpu + 0xE0C0);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;

    /* Initialize SHADER_PROGRAM descriptor */
    uint32_t *sp = (uint32_t *)(base_cpu + 0xCC00);
    sp[0] = (8u << 0) | (2u << 4) | (1u << 8) | (1u << 28) | (2u << 30);
    sp[1] = 0;
    *(uint64_t *)(sp + 2) = fb->isa_gpu;

    /* Initialize TILER_HEAP descriptor */
    uint32_t *th = (uint32_t *)(base_cpu + 0xD500);
    th[0] = (9u << 0) | (2u << 4) | (0u << 8);
    th[1] = 0x40000;
    *(uint64_t *)(th + 2) = fb->tiler_heap_backing_gpu;
    *(uint64_t *)(th + 4) = fb->tiler_heap_backing_gpu;
    *(uint64_t *)(th + 6) = fb->tiler_heap_backing_gpu + 0x40000;
    {
        uint64_t actual_top = *(uint64_t *)(th + 6);
        uint64_t expected_top = fb->tiler_heap_backing_gpu + 0x40000;
        printf("v9_framebuffer_create: th[6..7]=0x%016llx (expected 0x%016llx) %s\n",
               (unsigned long long)actual_top,
               (unsigned long long)expected_top,
               actual_top == expected_top ? "OK" : "MISMATCH");
    }

    /* Initialize TILER_CONTEXT struct */
    uint32_t *tc = (uint32_t *)(base_cpu + 0xD600);
    *(uint64_t *)(tc + 0) = fb->polylist_gpu;
    tc[2] = 0x1;
    tc[3] = (width - 1) | ((height - 1) << 16);
    *(uint64_t *)(tc + 6) = fb->tiler_heap_desc_gpu;

    /* Initialize RT0 Descriptor */
    uint32_t *rt0 = (uint32_t *)(base_cpu + 0x6080);
    rt0[0] = (1 << 26);
    uint32_t swizzle_rgba = (0 << 0) | (1 << 3) | (2 << 6) | (3 << 9);
    rt0[1] = (1 << 0) | (19 << 3) | (2 << 8) | (1 << 15) | (swizzle_rgba << 16) | (1u << 31);
    *(uint64_t *)(base_cpu + 0x6080 + 0x20) = color_gpu;
    *(uint32_t *)(base_cpu + 0x6080 + 0x28) = width * 4;
    uint32_t clear_val = 0xFF0000FF;
    *(uint32_t *)(base_cpu + 0x6080 + 0x30) = clear_val;
    *(uint32_t *)(base_cpu + 0x6080 + 0x34) = clear_val;
    *(uint32_t *)(base_cpu + 0x6080 + 0x38) = clear_val;

    /* Initialize Sample Location Table at 0xB100 */
    uint16_t *sl = (uint16_t *)(base_cpu + 0xB100);
    memset(sl, 0, 192);
    sl[0] = 128; sl[1] = 128;
    for (int i = 1; i < 32; i++) { sl[i*2] = 0; sl[i*2+1] = 256; }
    sl[64] = 128; sl[65] = 128;
    *(uint64_t *)(base_cpu + 0x6000 + 0x10) = base_gva + 0xB100;

    /* Initialize MFBD */
    uint32_t *mfbd = (uint32_t *)(base_cpu + 0x6000);
    mfbd[0] = 1;
    *(uint64_t *)(base_cpu + 0x6000 + 0x18) = fb->dcd_gpu;
    uint32_t *params = mfbd + 8;
    params[0] = (width - 1) | ((height - 1) << 16);  /* Bound max */
    params[1] = 0;                                    /* Bound min = (0,0) */
    params[2] = (width - 1) | ((height - 1) << 16);  /* Render bounds */
    uint32_t color_kb = (width * height * 4 + 1023) / 1024;
    uint32_t color_kb_8bit = (color_kb >= 256) ? 0 : color_kb;
    params[3] = (2 << 6) | (1 << 19) | (color_kb_8bit << 24);
    params[4] = (1 << 16);
    *(uint64_t *)(params + 6) = fb->tiler_ctx_gpu;

    /* Initialize Pre Frame 0 DCD at 0xC100 */
    uint32_t *dcd = (uint32_t *)(base_cpu + 0xC100);
    memset(dcd, 0, 3 * 128);
    dcd[0] = (1u << 0) | (1u << 1) | (1u << 6);  /* match replay flags: allow_fwd_kill, allow_fwd_killed, allow_prim_reorder */
    dcd[1] = 0x0000FFFF;
    dcd[7] = 0x3F800000;
    *(uint64_t *)(dcd + 10) = fb->depth_gpu;
    *(uint64_t *)(dcd + 12) = 1ULL | fb->blend_gpu;
    *(uint64_t *)(dcd + 24) = 1ULL | fb->res_gpu;
    *(uint64_t *)(dcd + 26) = fb->sp_gpu;
    *(uint64_t *)(dcd + 28) = fb->tls_gpu;
    *(uint64_t *)(dcd + 30) = 0;

    /* Build TILER_JOB (Type 7) */
    uint32_t *vt = (uint32_t *)(base_cpu + 0xE200);
    vt[4] = (1u << 0) | (7u << 1);
    *(uint64_t *)(vt + 6) = 0; /* Offset 0x18 MUST BE 0 */
    vt[8] = 0x38008;
    vt[11] = 3; vt[12] = 1; vt[13] = 3;
    *(uint64_t *)(vt + 14) = fb->tiler_ctx_gpu;
    vt[17] = 4;
    *(uint64_t *)(vt + 24) = fb->tiler_ctx_gpu;
    vt[27] = (width - 1) | ((height - 1) << 16);
    *(uint64_t *)(vt + 28) = 0x3f800000ULL;
    *(uint64_t *)(vt + 30) = fb->idx_gpu;

    uint32_t *dw = vt + 32;
    dw[0] = (1u << 0) | (1u << 1) | (1u << 6);
    dw[1] = 0xFFFF | (0x1u << 16);
    *(uint64_t *)(dw + 2) = fb->pos_gpu;
    dw[4] = (16u << 16);
    dw[7] = 0x3F800000;
    *(uint64_t *)(dw + 10) = fb->depth_gpu;
    *(uint64_t *)(dw + 12) = 1ULL | fb->blend_gpu;
    uint32_t *se = dw + 16;
    se[0] = 0;
    se[1] = 0;
    *(uint64_t *)(se + 8) = 1ULL | fb->res_gpu;
    *(uint64_t *)(se + 10) = fb->sp_gpu;
    *(uint64_t *)(se + 12) = fb->tls_gpu;
    *(uint64_t *)(se + 14) = 0;

    /* Build Cache Flush Job (Type 3) */
    uint32_t *fl = (uint32_t *)(base_cpu + 0xD400);
    fl[4] = (3u << 1);
    fl[8] = 0xFFFFFFFFu; fl[9] = 0xFFFFFFFFu;

    /* Build Fragment JC (Type 9) */
    uint32_t *fj = (uint32_t *)(base_cpu + 0xE380);
    fj[4] = (0u << 0) | (9u << 1);  /* job_descriptor_size=0, job_type=9 */
    *(uint64_t *)(fj + 10) = fb->mfbd_gva | 0x01u; /* Polygon List Mode flags = 0x01 */

    return fb;
}

void v9_framebuffer_free(struct v9_framebuffer *fb) {
    if (!fb) return;
    if (fb->exec_bo)  kbase_bo_free(fb->exec_bo);
    if (fb->mem_bo)   kbase_bo_free(fb->mem_bo);
    free(fb);
}

int v9_render_triangle(struct v9_framebuffer *fb) {
    if (!fb || !fb->mem_bo || !fb->mem_bo->dev) return -EINVAL;
    struct kbase_dev *dev = fb->mem_bo->dev;

    /* Zero polygon list header buffer */
    uint8_t *base_cpu = (uint8_t *)fb->mem_bo->cpu;
    memset(base_cpu + 0x7000, 0, 4096);

    /* Re-init TILER_JOB exception header words 0-3 */
    uint32_t *vt = (uint32_t *)(base_cpu + 0xE200);
    memset(vt, 0, 32);
    vt[4] = (1u << 0) | (7u << 1);
    *(uint64_t *)(vt + 6) = 0;

    /* 1. Submit Atom 0: TILER_JOB */
    printf("v9_render_triangle: submitting Atom 0 (jc=0x%llx, core_req=0x%x)\n",
           (unsigned long long)fb->tiler_job_gpu, KBASE_QUEUE_REQ_TILER);
    int ret = kbase_submit_job(dev, fb->tiler_job_gpu, KBASE_QUEUE_REQ_TILER, 0);
    if (ret < 0) return ret;
    uint32_t atom_nr = 0, event_code = 0;
    kbase_wait_event(dev, &atom_nr, &event_code);
    printf("v9_render_triangle: Atom 0 (TILER) event_code=0x%x atom_nr=%u\n", event_code, atom_nr);
    if (event_code != 0x1) {
        fprintf(stderr, "v9_render_triangle: Atom 0 (TILER) failed!\n");
        return -EIO;
    }

    /* 2. Submit Atom 1: Pre-Flush */
    ret = kbase_submit_job(dev, fb->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 1);
    if (ret < 0) return ret;
    kbase_wait_event(dev, &atom_nr, &event_code);
    printf("v9_render_triangle: Atom 1 (Pre-Flush) event_code=0x%x atom_nr=%u\n", event_code, atom_nr);
    if (event_code != 0x1) {
        fprintf(stderr, "v9_render_triangle: Atom 1 (Pre-Flush) failed!\n");
        return -EIO;
    }

    /* Dump polygon list and heap state right after TILER_JOB, before reinit clears them */
    {
        printf("POST-TILER: Polygon list header (first 32 bytes at 0x7000):\n");
        uint64_t *pl = (uint64_t *)(base_cpu + 0x7000);
        for (int i = 0; i < 4; i++) {
            printf("  +0x%02x: 0x%016llx\n", i * 8, (unsigned long long)pl[i]);
        }
        uint64_t *th_p = (uint64_t *)(base_cpu + 0xD500);
        printf("POST-TILER: Tiler Heap Desc: base=0x%llx bottom=0x%llx top=0x%llx\n",
               (unsigned long long)th_p[1],
               (unsigned long long)th_p[2],
               (unsigned long long)th_p[3]);
        uint64_t *tc_p = (uint64_t *)(base_cpu + 0xD600);
        uint64_t polylist_ptr = tc_p[0]; /* words 0-1: polygon list pointer written by GPU */
        printf("POST-TILER: Tiler Context words 0-1 (polygon list): 0x%016llx\n",
               (unsigned long long)polylist_ptr);
        /* Scan tiler heap for non-zero data */
        uint64_t heap_base = th_p[1];
        uint64_t heap_bot  = th_p[2];
        uint64_t heap_sz   = (heap_bot > heap_base) ? (heap_bot - heap_base) : 0;
        printf("POST-TILER: Heap used = 0x%llx bytes\n", (unsigned long long)heap_sz);
        if (heap_sz > 0 && heap_sz <= 4096) {
            printf("POST-TILER: First 32 bytes of heap data:\n");
            uint64_t *hp = (uint64_t *)(base_cpu + 0x80000);
            for (int i = 0; i < 4; i++) {
                printf("  +0x%02x: 0x%016llx\n", i * 8, (unsigned long long)hp[i]);
            }
        }
    }

    /* 3. Re-init Fragment JC & Reset Tiler Heap Pointers */
    uint32_t *th = (uint32_t *)(base_cpu + 0xD500);
    *(uint64_t *)(th + 4) = fb->tiler_heap_backing_gpu;
    *(uint64_t *)(th + 6) = fb->tiler_heap_backing_gpu + 0x40000; /* GPU writes top during TILER, restore it */

    uint32_t *fj = (uint32_t *)(base_cpu + 0xE380);
    memset(fj, 0, 32);
    fj[4] = (0u << 0) | (9u << 1);  /* job_descriptor_size=0, job_type=9 */
    *(uint64_t *)(fj + 10) = fb->mfbd_gva | 0x01u;

    /* Dump all key structures for offline comparison */
    {
        const uint8_t *b = base_cpu;
#define DUMP_BLOCK(label, off, words) do { \
    printf("DEBUG: " label " at 0x%04x (%u words):\n", (unsigned)(off), (unsigned)(words)); \
    const uint64_t *p = (const uint64_t *)(b + (off)); \
    for (unsigned _i = 0; _i < (words); _i += 2) { \
        printf("  +0x%02x: 0x%016llx  0x%016llx\n", _i * 8, \
               (unsigned long long)p[_i], (unsigned long long)p[_i+1]); \
    } \
} while(0)
        DUMP_BLOCK("MFBD",            0x6000, 16);
        DUMP_BLOCK("RT0",             0x6080, 8);
        DUMP_BLOCK("DCD (pre0)",      0xC100, 16);
        DUMP_BLOCK("SHADER_PROGRAM",  0xCC00, 4);
        DUMP_BLOCK("Tiler Heap Desc", 0xD500, 4);
        DUMP_BLOCK("Tiler Context",   0xD600, 24);
        DUMP_BLOCK("Blend",           0xE040, 2);
        DUMP_BLOCK("Depth/Stencil",   0xE060, 4);
        DUMP_BLOCK("TLS",             0xE0A0, 4);
        DUMP_BLOCK("Fragment JC",     0xE380, 8);
#undef DUMP_BLOCK
    }

    /* Validate all GPU pointers in the Fragment JC chain */
    {
        uint64_t mem_base = fb->mem_bo->gpu;
        uint64_t mem_end  = mem_base + fb->mem_bo->size;
        uint64_t exec_base = fb->exec_bo->gpu;
        uint64_t exec_end  = exec_base + fb->exec_bo->size;
        printf("DEBUG: Pointer Range Validation:\n");
        printf("  mem_bo:  [0x%llx - 0x%llx) size=%zu\n",
               (unsigned long long)mem_base, (unsigned long long)mem_end, fb->mem_bo->size);
        printf("  exec_bo: [0x%llx - 0x%llx) size=%zu\n",
               (unsigned long long)exec_base, (unsigned long long)exec_end, fb->exec_bo->size);

#define CHECK_PTR(label, addr, exp_in) do { \
    uint64_t _a = (addr); \
    int _ok = 0; \
    if ((exp_in) == 1 && _a >= mem_base && _a < mem_end) _ok = 1; \
    if ((exp_in) == 2 && _a >= exec_base && _a < exec_end) _ok = 1; \
    printf("  %-20s 0x%016llx  %s\n", (label), (unsigned long long)_a, \
           _ok ? "OK" : (_a == 0 ? "ZERO" : "OUT-OF-BOUNDS")); \
} while(0)
        printf("  --- struct pointers in Fragment JC chain: ---\n");
        CHECK_PTR("frag_jc (jc)",  fb->frag_jc_gpu, 1);
        CHECK_PTR("MFBD @ frag_jc", fb->mfbd_gva, 1);
        CHECK_PTR("sample_loc @ MFBD+0x10", *(uint64_t*)(base_cpu + 0x6010), 1);
        CHECK_PTR("DCD @ MFBD+0x18", fb->dcd_gpu, 1);
        CHECK_PTR("tiler_ctx @ MFBD+0x38", fb->tiler_ctx_gpu, 1);
        printf("  --- DCD referenced pointers: ---\n");
        CHECK_PTR("depth @ DCD+0x28",  fb->depth_gpu, 1);
        CHECK_PTR("blend @ DCD+0x30",  fb->blend_gpu, 1);
        CHECK_PTR("res   @ DCD+0x60",  fb->res_gpu, 1);
        CHECK_PTR("sp    @ DCD+0x68",  fb->sp_gpu, 1);
        CHECK_PTR("tls   @ DCD+0x70",  fb->tls_gpu, 1);
        printf("  --- Tiler context chain: ---\n");
        CHECK_PTR("polylist @ tc+0x00", fb->polylist_gpu, 1);
        CHECK_PTR("heap_desc @ tc+0x30", fb->tiler_heap_desc_gpu, 1);
        printf("  --- Tiler heap chain: ---\n");
        CHECK_PTR("heap_backing", fb->tiler_heap_backing_gpu, 1);
        uint64_t heap_top = *(uint64_t *)(base_cpu + 0xD518);
        printf("  %-20s 0x%016llx  %s (expected 0x%llx)\n",
               "heap_top @ th+6", (unsigned long long)heap_top,
               heap_top == fb->tiler_heap_backing_gpu + 0x40000 ? "OK" : "WRONG",
               (unsigned long long)(fb->tiler_heap_backing_gpu + 0x40000));
        printf("  --- Shader program chain: ---\n");
        CHECK_PTR("ISA (exec_bo)", fb->isa_gpu, 2);
        CHECK_PTR("RT0", fb->rt0_gpu, 1);
        uint64_t color_addr = *(uint64_t *)(base_cpu + 0x60A0);
        CHECK_PTR("color_buf @ RT0+0x20", color_addr, 1);
#undef CHECK_PTR
    }

    ret = kbase_submit_job(dev, fb->frag_jc_gpu, KBASE_QUEUE_REQ_FRAGMENT, 2);
    if (ret < 0) return ret;
    kbase_wait_event(dev, &atom_nr, &event_code);
    printf("v9_render_triangle: Atom 2 (Fragment) event_code=0x%x atom_nr=%u\n", event_code, atom_nr);
    if (event_code != 0x1) {
        fprintf(stderr, "v9_render_triangle: Atom 2 (Fragment) failed!\n");
        return -EIO;
    }

    /* 4. Submit Atom 3: Post-Flush */
    memset(base_cpu + 0xD400, 0, 64);
    uint32_t *fl = (uint32_t *)(base_cpu + 0xD400);
    fl[4] = (3u << 1);
    fl[8] = 0xFFFFFFFFu; fl[9] = 0xFFFFFFFFu;

    ret = kbase_submit_job(dev, fb->flush_jc_gpu, KBASE_QUEUE_REQ_FLUSH, 3);
    if (ret < 0) return ret;
    kbase_wait_event(dev, &atom_nr, &event_code);
    printf("v9_render_triangle: Atom 3 (Post-Flush) event_code=0x%x atom_nr=%u\n", event_code, atom_nr);
    if (event_code != 0x1) {
        fprintf(stderr, "v9_render_triangle: Atom 3 (Post-Flush) failed!\n");
        return -EIO;
    }

    return 0;
}

uint32_t v9_read_pixel(struct v9_framebuffer *fb, uint32_t x, uint32_t y) {
    if (!fb || !fb->mem_bo || x >= fb->width || y >= fb->height) return 0;
    uint64_t color_off = (fb->width * fb->height * 4 > 0x1000) ? 0x40000 : 0xA000;
    uint32_t *color = (uint32_t *)((uint8_t *)fb->mem_bo->cpu + color_off);
    return color[y * fb->width + x];
}
