#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>

#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

#define PAGE_SIZE 4096ULL
#define MAX_PAGES 32
#define OFF_SOFT0    0x0000
#define OFF_COMPUTE  0x0100
#define OFF_FRAG     0x0200
#define OFF_SOFT3    0x0300
#define OFF_HYBRID_FBD   0x4000
#define OFF_HYBRID_RT    0x5000
#define OFF_HYBRID_COLOR 0x30000

/* Scratch MFBD layout offsets */
#define OFF_SCRATCH_MFBD      0x6000
#define OFF_SCRATCH_RT        (OFF_SCRATCH_MFBD + 0x80)
#define OFF_SCRATCH_DCD       0x6100
#define OFF_SCRATCH_TILER     0x6200
#define OFF_SCRATCH_POLYLIST  0x7000
#define OFF_SCRATCH_HEAP      0x8000
#define OFF_SCRATCH_TLS       0x9000
#define OFF_SCRATCH_COLOR     0xA000
#define OFF_SCRATCH_COLOR_LG  0x40000
#define OFF_SCRATCH_FRAG_JC   0xB000
#define OFF_SCRATCH_SAMPLELOC 0xB100

/* Shader MFBD mode offsets */
#define OFF_SHADER_ISA        0xC000  /* raw Valhall ISA */
#define OFF_SHADER_DCD        0xC100  /* 3 × 128 = 384 bytes; pre0/pre1/post */
#define OFF_SHADER_PROGRAM    0xCC00  /* 32-byte SHADER_PROGRAM descriptor */
#define OFF_SHADER_BLEND      0xD000
#define OFF_SHADER_TLS        0xD100
#define OFF_SHADER_RESOURCES  0xD200  /* dummy resources table */
#define OFF_SHADER_DEPTH      0xD300  /* DEPTH_STENCIL descriptor (32 bytes) */
#define OFF_SHADER_FLUSH_JC   0xD400  /* Cache Flush Job (chained after Fragment) */
#define OFF_TILER_HEAP_DESC   0xD500  /* TILER_HEAP descriptor (32 bytes) */
#define OFF_TILER_CTX         0xD600  /* TILER_CONTEXT struct (192 bytes) */
#define OFF_TILER_HEAP_BACKING 0x80000 /* 256 KiB backing for the tiler heap */
#define TILER_HEAP_SIZE       0x40000 /* 256 KiB == minimum chunk size */

struct base_dependency {
    uint8_t atom_id;
    uint8_t dep_type;
} __attribute__((packed));

struct kbase_atom_mtk {
    uint64_t seq_nr;
    uint64_t jc;
    uint64_t udata[2];
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t  jit_id[2];
    struct base_dependency pre_dep[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;
    uint32_t pad2;
} __attribute__((packed));

struct kbase_ioctl_job_submit {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

struct page_asset {
    uint64_t orig_page;
    uint64_t new_addr;
    const char *filename;
};

static const struct page_asset k_page_assets[] = {
    {0x5effe98000ULL, 0, "001_atom2_frag_arena_page_5effe98000.bin"},
    {0x5effe99000ULL, 0, "001_atom2_frag_arena_page_5effe99000.bin"},
    {0x5effe9a000ULL, 0, "001_atom2_frag_arena_page_5effe9a000.bin"},
    {0x5effe9b000ULL, 0, "001_atom2_frag_arena_page_5effe9b000.bin"},
    {0x5effe9c000ULL, 0, "001_atom2_frag_arena_page_5effe9c000.bin"},
    {0x5effe9d000ULL, 0, "001_atom2_frag_arena_page_5effe9d000.bin"},
    {0x5effe9e000ULL, 0, "001_atom2_frag_arena_page_5effe9e000.bin"},
    {0x5effe9f000ULL, 0, "001_atom2_frag_arena_page_5effe9f000.bin"},
    {0x5effea0000ULL, 0, "001_atom2_frag_arena_page_5effea0000.bin"},
    {0x5effea1000ULL, 0, "001_atom2_frag_arena_page_5effea1000.bin"},
    {0x5effea2000ULL, 0, "001_atom2_frag_arena_page_5effea2000.bin"},
    {0x5effea3000ULL, 0, "001_atom2_frag_arena_page_5effea3000.bin"},
    {0x5effea4000ULL, 0, "001_atom2_frag_arena_page_5effea4000.bin"},
    {0x5effea5000ULL, 0, "001_atom2_frag_arena_page_5effea5000.bin"},
    {0x5effea6000ULL, 0, "001_atom2_frag_arena_page_5effea6000.bin"},
    {0x5effeb9000ULL, 0, "001_atom2_frag_tls_page_page_5effeb9000.bin"},
    {0x5efffbb000ULL, 0, "001_atom2_frag_fau0_page_page_5efffbb000.bin"},
    {0x5efffbc000ULL, 0, "001_atom2_frag_fau0_page_page_5efffbc000.bin"},
    {0x5efffc1000ULL, 0, "001_atom2_frag_tls_page_page_5efffc1000.bin"},
    {0x5efffc2000ULL, 0, "001_atom2_frag_dcd_page_page_5efffc2000.bin"},
    {0x5efffc4000ULL, 0, "001_atom2_frag_fau0_page_page_5efffc4000.bin"},
    {0x5effffa000ULL, 0, "001_atom2_frag_fau0_page_page_5effffa000.bin"},
    {0x5effffe000ULL, 0, "001_atom2_frag_fau0_page_page_5effffe000.bin"},
};

static int read_file(const char *path, void *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t got = read(fd, buf, size);
    close(fd);
    return got == (ssize_t)size ? 0 : -1;
}

static void path_join(char *out, size_t out_sz, const char *dir, const char *name) {
    snprintf(out, out_sz, "%s/%s", dir, name);
}

static uint64_t relocate_ptr(uint64_t value, const struct page_asset *pages, size_t n_pages) {
    for (size_t i = 0; i < n_pages; i++) {
        uint64_t base = pages[i].orig_page;
        if (value >= base && value < base + PAGE_SIZE) {
            return pages[i].new_addr + (value - base);
        }
    }
    return value;
}

static struct page_asset *find_page(struct page_asset *pages, size_t n_pages, uint64_t orig_page) {
    for (size_t i = 0; i < n_pages; i++) {
        if (pages[i].orig_page == orig_page) return &pages[i];
    }
    return NULL;
}

static void patch_blob_qwords(void *buf, size_t size, const struct page_asset *pages, size_t n_pages, int zero_unknown_high) {
    uint8_t *p = buf;
    for (size_t off = 0; off + 8 <= size; off += 8) {
        uint64_t v = *(uint64_t *)(p + off);
        uint64_t mapped = relocate_ptr(v, pages, n_pages);
        if (mapped != v) {
            *(uint64_t *)(p + off) = mapped;
            continue;
        }
        if (zero_unknown_high && ((v >> 56) == 0xb4 || (v >> 48) == 0xb400)) {
            *(uint64_t *)(p + off) = 0;
        }
    }
}

static int load_assets(const char *dir, void *cpu, uint64_t gva, struct page_asset *pages, size_t n_pages) {
    uint8_t *base = cpu;
    uint64_t page_region = 0x10000;
    for (size_t i = 0; i < n_pages; i++) {
        char path[512];
        path_join(path, sizeof(path), dir, pages[i].filename);
        pages[i].new_addr = gva + page_region + i * PAGE_SIZE;
        if (read_file(path, base + page_region + i * PAGE_SIZE, PAGE_SIZE) != 0) {
            fprintf(stderr, "Failed to read page asset %s: %s\n", path, strerror(errno));
            return -1;
        }
    }

    struct {
        const char *name;
        size_t off;
        size_t size;
        int zero_unknown_high;
    } blobs[] = {
        {"001_atom0_soft_jc.bin", OFF_SOFT0, 64, 0},
        {"001_atom1_hw_jc.bin", OFF_COMPUTE, 128, 0},
        {"001_atom2_hw_jc.bin", OFF_FRAG, 64, 0},
        {"001_atom3_soft_jc.bin", OFF_SOFT3, 64, 1},
    };

    for (size_t i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++) {
        char path[512];
        path_join(path, sizeof(path), dir, blobs[i].name);
        if (read_file(path, base + blobs[i].off, blobs[i].size) != 0) {
            fprintf(stderr, "Failed to read blob %s: %s\n", path, strerror(errno));
            return -1;
        }
        patch_blob_qwords(base + blobs[i].off, blobs[i].size, pages, n_pages, blobs[i].zero_unknown_high);
    }

    for (size_t i = 0; i < n_pages; i++) {
        patch_blob_qwords(base + page_region + i * PAGE_SIZE, PAGE_SIZE, pages, n_pages, 0);
    }
    return 0;
}

static void dump_words(const char *label, const void *buf, size_t size) {
    const uint8_t *p = buf;
    printf("%s\n", label);
    for (size_t i = 0; i < size; i += 8) {
        uint64_t v = 0;
        memcpy(&v, p + i, (size - i) >= 8 ? 8 : size - i);
        printf("  0x%03zx: 0x%016llx\n", i, (unsigned long long)v);
    }
}

static void build_hybrid_fbd(void *cpu, uint64_t gva, uint64_t dcd_addr) {
    uint8_t *base = cpu;
    uint32_t *fbd = (uint32_t *)(base + OFF_HYBRID_FBD);
    uint32_t *rt = (uint32_t *)(base + OFF_HYBRID_RT);
    volatile uint32_t *color = (volatile uint32_t *)(base + OFF_HYBRID_COLOR);

    memset(base + OFF_HYBRID_FBD, 0, 0x100);
    memset(base + OFF_HYBRID_RT, 0, 0x40);
    for (int i = 0; i < 64 * 64; i++) color[i] = 0xdeadbeef;

    fbd[0] = 1;
    fbd[2] = 0x00010000;
    *(uint64_t *)((uint8_t *)fbd + 0x18) = dcd_addr;

    fbd[0x80 / 4 + 0] = 64;
    fbd[0x80 / 4 + 1] = 64;
    fbd[0x80 / 4 + 2] = 0x2;
    fbd[0x80 / 4 + 3] = 0;
    *(uint64_t *)((uint8_t *)fbd + 0xa0) = gva + OFF_HYBRID_RT;

    *(uint64_t *)((uint8_t *)rt + 0x00) = gva + OFF_HYBRID_COLOR;
    rt[2] = 64 * 4;
}

static void build_scratch_fbd(void *cpu, uint64_t gva, int fb_w, int fb_h, uint64_t color_off) {
    uint8_t *base = (uint8_t *)cpu;

    memset(base + OFF_SCRATCH_MFBD, 0, OFF_SCRATCH_FRAG_JC + 0x1000 - OFF_SCRATCH_MFBD);

    volatile uint32_t *color = (volatile uint32_t *)(base + color_off);
    for (int i = 0; i < fb_w * fb_h; i++) color[i] = 0xdeadbeef;

    /* === Bifrost Framebuffer Parameters (MFBD+0x00, 32 bytes) === */
    uint32_t *mfbd = (uint32_t *)(base + OFF_SCRATCH_MFBD);
    mfbd[0] = 0;  /* All Pre/Post Frame modes = Never */
    {
        uint16_t *sl = (uint16_t *)(base + OFF_SCRATCH_SAMPLELOC);
        memset(sl, 0, 192);
        sl[0] = 128; sl[1] = 128;
        for (int i = 1; i < 32; i++) { sl[i*2] = 0; sl[i*2+1] = 256; }
        sl[64] = 128; sl[65] = 128;
    }
    *(uint64_t *)(base + OFF_SCRATCH_MFBD + 0x10) = gva + OFF_SCRATCH_SAMPLELOC;

    /* === Multi-Target Framebuffer Parameters (MFBD+0x20, 24 bytes) === */
    uint32_t *params = (uint32_t *)(base + OFF_SCRATCH_MFBD + 0x20);
    params[0] = (fb_w - 1) | ((fb_h - 1) << 16);
    params[1] = 0;
    params[2] = (fb_w - 1) | ((fb_h - 1) << 16);
    params[3] = (0 << 0) | (2 << 6) | (8 << 9) | (0 << 19) | (1 << 24);
    params[4] = (1 << 16);  /* Z Internal Format = D24 */
    /* params[5] = Z Clear (word 13) -- leave 0 */
    /* NOTE: Tiler pointer at MFBD+0x38 is intentionally LEFT NULL.
     * Setting it requires a fully-valid Tiler Context + Tiler Heap
     * descriptor; with a zero-filled heap the GPU faults DATA_INVALID
     * (0x58). Pre-Frame-Shader-only rendering does not need a valid
     * tiler context — the GPU iterates tiles directly. */

    /* === RT0 descriptor (MFBD+0x80, 64 bytes) === */
    uint32_t *rt = (uint32_t *)(base + OFF_SCRATCH_RT);
    rt[0] = (1 << 26);    /* Internal Format = R8G8B8A8 */
    uint32_t swizzle_rgba = (0 << 0) | (1 << 3) | (2 << 6) | (3 << 9);
    rt[1] = (1 << 0) | (19 << 3) | (2 << 8) | (1 << 15) | (swizzle_rgba << 16) | (1u << 31);
    *(uint64_t *)(base + OFF_SCRATCH_RT + 0x20) = gva + color_off;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x28) = fb_w * 4;
    uint32_t clear_red = 0xFF0000FF;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x30) = clear_red;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x34) = clear_red;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x38) = clear_red;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x3C) = clear_red;

    /* === DCD / Renderer State (64 bytes) === */
    uint32_t *dcd = (uint32_t *)(base + OFF_SCRATCH_DCD);
    dcd[8] = 0xFFFF | (7 << 24);

    /* === Bifrost Tiler struct (192 bytes) === */
    uint32_t *tiler = (uint32_t *)(base + OFF_SCRATCH_TILER);
    *(uint64_t *)(tiler + 0) = gva + OFF_SCRATCH_POLYLIST;
    tiler[2] = 0x1;
    tiler[3] = (fb_w - 1) | ((fb_h - 1) << 16);
    *(uint64_t *)(tiler + 6) = gva + OFF_SCRATCH_HEAP;

    /* === Fragment Job (64 bytes at OFF_SCRATCH_FRAG_JC) === */
    uint32_t *jc = (uint32_t *)(base + OFF_SCRATCH_FRAG_JC);
    jc[4] = (1 << 0) | (9 << 1);
    jc[8] = 0;
    jc[9] = ((fb_w / 16) - 1) | (((fb_h / 16) - 1) << 16);
    *(uint64_t *)(jc + 10) = (gva + OFF_SCRATCH_MFBD) | 0x01;

    printf("scratch_fbd: %dx%d MFBD at gpu 0x%llx\n", fb_w, fb_h, (unsigned long long)(gva + OFF_SCRATCH_MFBD));
    printf("scratch_fbd: color buf at gpu 0x%llx (%d bytes)\n", (unsigned long long)(gva + color_off), fb_w * fb_h * 4);
    printf("scratch_fbd: frag JC at gpu 0x%llx bounds max=(%d,%d)\n",
           (unsigned long long)(gva + OFF_SCRATCH_FRAG_JC), (fb_w / 16) - 1, (fb_h / 16) - 1);
    dump_words("scratch MFBD", base + OFF_SCRATCH_MFBD, 0x80);
    dump_words("scratch RT0", base + OFF_SCRATCH_RT, 0x40);
    dump_words("scratch frag JC", base + OFF_SCRATCH_FRAG_JC, 0x40);
}

/* === Minimal Valhall fragment shader writing solid green to RT0 === *
 *
 * Builds on the working scratch_fbd path. Adds a real fragment shader
 * via the Frame Shader DCD pointer at MFBD+0x18 with Pre Frame 0 = Always.
 *
 * Shader (7 instructions, 56 bytes):
 *   IADD_IMM.i32 r0, 0x0, #0x0           ; r0 = 0.0f bits
 *   FADD.f32     r1, r0, 0x3F800000      ; r1 = 1.0f
 *   IADD_IMM.i32 r2, 0x0, #0x0           ; r2 = 0.0f
 *   FADD.f32     r3, r0, 0x3F800000      ; r3 = 1.0f
 *   NOP.wait0126
 *   ATEST.discard @r60, r60, 0x3F800000, atest_datum.w0
 *   BLEND.slot0.v4.f32.end @r0:r1:r2:r3, blend_descriptor_0.w0, r60, target:0x0
 *
 * Output: RGBA = (0,1,0,1) = solid green.
 *
 * Encodings extracted from
 * refs/panfrost/src/panfrost/compiler/bifrost/valhall/test/assembler-cases.txt.
 */
static const uint8_t k_valhall_green_fs[] = {
    /* IADD_IMM.i32 r0, 0x0, #0x0 -- r0 = 0.0f */
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x10, 0x01,
    /* FADD.f32 r1, r0, 0x3F800000 -- r1 = 1.0f */
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc1, 0xa4, 0x00,
    /* IADD_IMM.i32 r2, 0x0, #0x0 -- r2 = 0.0f */
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x10, 0x01,
    /* FADD.f32 r3, r0, 0x3F800000 -- r3 = 1.0f */
    0x00, 0xd0, 0x00, 0x00, 0x00, 0xc3, 0xa4, 0x00,
    /* NOP.wait0126 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x40,
    /* ATEST.discard @r60, r60, 0x3F800000, atest_datum.w0 */
    0x3c, 0xd0, 0xea, 0x00, 0x02, 0xbc, 0x7d, 0x68,
    /* BLEND.slot0.v4.f32.end @r0:r1:r2:r3, blend_descriptor_0.w0, r60, target:0x0 */
    0xf0, 0x00, 0x3c, 0x32, 0x08, 0x40, 0x7f, 0x78,
};

/* Same as k_valhall_green_fs but outputs RED (R=1,G=0,B=0,A=1).
 * Achieved by swapping the r0/r1 setup: r0 gets FADD-loaded 1.0,
 * r1 gets IADD_IMM 0, while r2 stays 0 and r3 stays 1. */
static const uint8_t k_valhall_red_fs[] = {
    /* IADD_IMM.i32 r1, 0x0, #0x0  (encoding patches dst=r1 in word 5) */
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc1, 0x10, 0x01,
    /* FADD.f32 r0, r1, 0x3F800000 */
    0x01, 0xd0, 0x00, 0x00, 0x00, 0xc0, 0xa4, 0x00,
    /* IADD_IMM.i32 r2, 0x0, #0x0 */
    0xc0, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x10, 0x01,
    /* FADD.f32 r3, r1, 0x3F800000 */
    0x01, 0xd0, 0x00, 0x00, 0x00, 0xc3, 0xa4, 0x00,
    /* NOP.wait0126 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x40,
    /* ATEST.discard @r60, r60, 0x3F800000, atest_datum.w0 */
    0x3c, 0xd0, 0xea, 0x00, 0x02, 0xbc, 0x7d, 0x68,
    /* BLEND.slot0.v4.f32.end @r0:r1:r2:r3, blend_descriptor_0.w0, r60, target:0x0 */
    0xf0, 0x00, 0x3c, 0x32, 0x08, 0x40, 0x7f, 0x78,
};

/* Tunables for shader_fbd diagnostic variants. Set via env or recompile. */
static int g_shader_pre_frame_mode = 1;  /* 0=Never, 1=Always, 2=Intersect, 3=Early ZS always */
static int g_shader_skip_atest    = 0;
static int g_shader_use_minimal   = 0;
static int g_shader_use_red       = 0;
static int g_shader_with_tiler    = 0;   /* 1 = build & wire a Tiler Context */

/* Separate executable shader allocation (must be GPU_EX). Set up in main(). */
static uint64_t g_shader_exec_va = 0;
static void *g_shader_exec_cpu = NULL;

/* Build a v9 TILER_HEAP descriptor (32 bytes) at OFF_TILER_HEAP_DESC and a
 * TILER_CONTEXT (192 bytes) at OFF_TILER_CTX. Wire the FBD's Tiler pointer
 * (FBP word 14, MFBD+0x38) to the context.
 *
 * The heap backing storage is OFF_TILER_HEAP_BACKING (256 KiB) which we
 * reserved in the main MEM_ALLOC. Without geometry the heap stays empty,
 * but the GPU validates the descriptor.
 *
 * Tiler Heap struct (8 words):
 *   word 0: Type=Buffer(9) | BufferType=TilerHeap(2) | ChunkSize=256KiB(0)
 *   word 1: Size (aligned to 4 KiB)
 *   words 2/3: Base
 *   words 4/5: Bottom (= Base initially)
 *   words 6/7: Top  (= Base + Size)
 *
 * Tiler Context struct (48 words = 192 bytes, align 64):
 *   words 0/1: Polygon List (NULL — populated by tiler at runtime)
 *   word 2:    Hierarchy Mask (13 bits) | Sample Pattern | flags
 *   word 3:    FB Width-1 | FB Height-1
 *   word 4:    Layer count-1 | Layer offset
 *   word 5:    padding
 *   words 6/7: Heap pointer (TILER_HEAP descriptor address)
 *   words 8-15:  Weights (zero)
 *   words 16-31: State (zero — initialized by GPU)
 */
static void build_tiler_context(void *cpu, uint64_t gva, int fb_w, int fb_h) {
    uint8_t *base = (uint8_t *)cpu;

    /* Heap descriptor (8 words / 32 bytes) */
    uint32_t *th = (uint32_t *)(base + OFF_TILER_HEAP_DESC);
    memset(th, 0, 32);
    th[0] = (9u << 0)        /* Type = Buffer */
          | (2u << 4)        /* Buffer type = Tiler heap */
          | (0u << 8)        /* Chunk size = 256 KiB */
          | (0u << 10);      /* Partitioning = Dynamic */
    th[1] = (uint32_t)TILER_HEAP_SIZE;  /* Size in bytes (must be 4K-aligned) */
    uint64_t heap_base = gva + OFF_TILER_HEAP_BACKING;
    *(uint64_t *)(th + 2) = heap_base;
    *(uint64_t *)(th + 4) = heap_base;                     /* Bottom = Base */
    *(uint64_t *)(th + 6) = heap_base + TILER_HEAP_SIZE;   /* Top = Base + Size */

    /* Tiler Context (48 words / 192 bytes) */
    uint32_t *tc = (uint32_t *)(base + OFF_TILER_CTX);
    memset(tc, 0, 192);
    /* word 0/1 = Polygon List, leave 0 (filled in by tiler) */
    tc[2] = (1u << 0)        /* Hierarchy Mask = 1 (smallest level) */
          | (0u << 13);      /* Sample Pattern = Single-sampled (0) */
    tc[3] = (uint32_t)((fb_w - 1) | ((fb_h - 1) << 16));
    tc[4] = 0;               /* Layer count-1 = 0 (single layer) */
    /* word 5 = padding */
    *(uint64_t *)(tc + 6) = gva + OFF_TILER_HEAP_DESC;

    /* Patch FBD's Tiler pointer at MFBD+0x38 (FBP word 14, params[6/7]) */
    uint32_t *params = (uint32_t *)(base + OFF_SCRATCH_MFBD + 0x20);
    *(uint64_t *)((uint8_t *)params + 24) = gva + OFF_TILER_CTX;

    printf("tiler_ctx: heap_desc at gpu 0x%llx (backing 0x%llx, %d KiB)\n",
           (unsigned long long)(gva + OFF_TILER_HEAP_DESC),
           (unsigned long long)heap_base,
           TILER_HEAP_SIZE / 1024);
    printf("tiler_ctx: context at gpu 0x%llx wired to MFBD+0x38\n",
           (unsigned long long)(gva + OFF_TILER_CTX));
    dump_words("tiler heap desc", base + OFF_TILER_HEAP_DESC, 32);
    dump_words("tiler context", base + OFF_TILER_CTX, 64);
}

static void build_shader_fbd(void *cpu, uint64_t gva, int fb_w, int fb_h, uint64_t color_off) {
    uint8_t *base = (uint8_t *)cpu;

    /* Allow env override of variant */
    const char *e;
    if ((e = getenv("SHADER_PFM"))      ) g_shader_pre_frame_mode = atoi(e);
    if ((e = getenv("SHADER_SKIP_ATEST"))) g_shader_skip_atest    = atoi(e);
    if ((e = getenv("SHADER_MINIMAL"))   ) g_shader_use_minimal   = atoi(e);
    if ((e = getenv("SHADER_RED"))       ) g_shader_use_red       = atoi(e);
    if ((e = getenv("SHADER_TILER"))     ) g_shader_with_tiler    = atoi(e);
    printf("shader_fbd variants: pre_frame_mode=%d skip_atest=%d minimal=%d red=%d tiler=%d\n",
           g_shader_pre_frame_mode, g_shader_skip_atest, g_shader_use_minimal,
           g_shader_use_red, g_shader_with_tiler);

    /* Start from the proven scratch_fbd base */
    build_scratch_fbd(cpu, gva, fb_w, fb_h, color_off);

    /* Inject the Valhall fragment shader ISA. The shader MUST live in a
     * GPU_EX (executable) memory region — our main allocation is RW-only. */
    const uint8_t *shader_src = g_shader_use_red ? k_valhall_red_fs : k_valhall_green_fs;
    size_t shader_len = sizeof(k_valhall_green_fs);  /* both arrays have identical length */
    uint64_t isa_addr;
    if (g_shader_exec_cpu && g_shader_exec_va) {
        uint8_t *exec = (uint8_t *)g_shader_exec_cpu;
        if (g_shader_use_minimal) {
            memcpy(exec, shader_src + shader_len - 8, 8);
        } else if (g_shader_skip_atest) {
            memcpy(exec, shader_src, 32);
            memcpy(exec + 32, shader_src + 48, 8);
        } else {
            memcpy(exec, shader_src, shader_len);
        }
        isa_addr = g_shader_exec_va;
    } else {
        if (g_shader_use_minimal) {
            memcpy(base + OFF_SHADER_ISA, shader_src + shader_len - 8, 8);
        } else if (g_shader_skip_atest) {
            memcpy(base + OFF_SHADER_ISA, shader_src, 32);
            memcpy(base + OFF_SHADER_ISA + 32, shader_src + 48, 8);
        } else {
            memcpy(base + OFF_SHADER_ISA, shader_src, shader_len);
        }
        isa_addr = gva + OFF_SHADER_ISA;
    }
    printf("shader_fbd: ISA at gpu 0x%llx (%s, %s)\n",
           (unsigned long long)isa_addr,
           g_shader_exec_cpu ? "GPU_EX exec page" : "fallback non-exec",
           g_shader_use_red ? "RED shader" : "GREEN shader");

    /* Build the v9 SHADER_PROGRAM descriptor (32 bytes / 8 words):
     *   word 0:
     *     [3:0]   Type = Shader (8)
     *     [7:4]   Stage = Fragment (2)
     *     [8]     Fragment coverage bitmask = GL (1)
     *     [16]    Suppress NaN = 0
     *     [28]    Requires helper threads = 1 (fragment)
     *     [31:30] Register allocation = 32 Per Thread (2)
     *   word 1 [15:0] = Preload mask = 0
     *   words 2-3 = Binary address (raw ISA VA)
     */
    {
        uint32_t *sp = (uint32_t *)(base + OFF_SHADER_PROGRAM);
        memset(sp, 0, 32);
        int helpers = getenv("SHADER_HELPERS") ? atoi(getenv("SHADER_HELPERS")) : 1;
        sp[0] = (8u << 0)        /* Type = Shader */
              | (2u << 4)        /* Stage = Fragment */
              | (1u << 8)        /* Coverage bitmask type = GL */
              | ((helpers ? 1u : 0u) << 28) /* Requires helper threads */
              | (2u << 30);      /* Register allocation = 32 Per Thread */
        sp[1] = 0;               /* Preload = 0 */
        sp[2] = (uint32_t)(isa_addr & 0xFFFFFFFFu);
        sp[3] = (uint32_t)(isa_addr >> 32);
        printf("shader_fbd: SHADER_PROGRAM helpers=%d\n", helpers);
    }

    /* Build a minimum dummy resources table (32 bytes zeroed). The shader does
     * not use textures/samplers but the GPU may probe the table base. */
    memset(base + OFF_SHADER_RESOURCES, 0, 64);
    /* Pad ISA region with zeros (NOPs interpreted as instr_invalid; fine since we end at BLEND) */

    /* Build a minimal Blend descriptor (16 bytes) for fixed-function REPLACE on RGBA8 UNORM
     *
     * v9 Blend struct (16 bytes, 4 words):
     *   word 0:
     *     bit 0   Load Destination = 0
     *     bit 8   Alpha To One = 0
     *     bit 9   Enable = 1
     *     bit 10  sRGB = 0
     *     bit 11  Round to FB precision = 0
     *     bits 16-31 Blend Constant = 0
     *   word 1: Equation
     *     bits 0-11  RGB Function: A=Src(2), B=Src(2), C=Zero(1), Negate=0, Invert=0
     *                  -> packed = (2<<0) | (2<<4) | (1<<8) = 0x122
     *     bits 12-23 Alpha Function: same -> 0x122
     *     bits 28-31 Color Mask = 0xF (RGBA)
     *                  -> word1 = 0x122 | (0x122 << 12) | (0xF << 28) = 0xF0122122
     *   words 2-3: Internal Blend (Fixed-Function mode)
     *     bits 0-1   Mode = Fixed-Function (2)
     *     bits 3-4   Num Comps - 1 = 3 (4 components)
     *     bits 16-19 RT = 0
     *     word 1: Conversion (Internal Conversion struct, 22-bit Pixel Format)
     *       Memory Format = (RGBA8_TB << 12) | RGBA = (237 << 12) | 0 = 0xED000
     *
     *  We use Fixed-Function (mode=2) instead of Opaque (mode=1) to be safe.
     */
    uint32_t *bl = (uint32_t *)(base + OFF_SHADER_BLEND);
    bl[0] = (1u << 9);                                  /* Enable=1 */
    bl[1] = (2u << 0) | (2u << 4) | (1u << 8) |         /* RGB:   A=Src B=Src C=Zero */
            ((2u << 0) | (2u << 4) | (1u << 8)) << 12 | /* Alpha: same */
            (0xFu << 28);                               /* Color Mask = RGBA */
    /* Internal Blend (8 bytes): Mode=Fixed-Function, num_comps=4, conversion=RGBA8 */
    bl[2] = (2u << 0) | (3u << 3) | (0u << 16);         /* Mode=2, num_comps-1=3, RT=0 */
    bl[3] = (237u << 12) | 0u;                          /* Conversion: RGBA8_TB | RGBA */

    /* Build the v9 Draw / Renderer State (DCD) -- 128 bytes total
     *   word 0  = Flags 0
     *   word 1  = Flags 1
     *   words 2-4 = Vertex Array (96 bits)
     *   word 6  = Min Z (float)
     *   word 7  = Max Z (float)
     *   word 10 = Depth/stencil pointer (low)
     *   word 12 = Blend count (bits 0-3) | Blend pointer (bits 4-63), shr(4)
     *   word 14 = Occlusion ptr (low)
     *   word 16 = Shader Environment start
     *     SE word 0 (=DCD word 16) = attribute_offset (32)
     *     SE word 1 (=DCD word 17) = FAU count (low 8 bits)
     *     SE word 8 (=DCD word 24) = Resources ptr  -> DCD+0x60
     *     SE word 10 (=DCD word 26) = Shader ptr    -> DCD+0x68
     *     SE word 12 (=DCD word 28) = TLS ptr       -> DCD+0x70
     *     SE word 14 (=DCD word 30) = FAU ptr       -> DCD+0x78
     */
    uint32_t *dcd = (uint32_t *)(base + OFF_SHADER_DCD);
    /* Zero out the full 3-DCD array (384 bytes). The pointer at MFBD+0x18 is an
     * ARRAY of 3 Draw descriptors:
     *   index 0 -> Pre Frame 0
     *   index 1 -> Pre Frame 1
     *   index 2 -> Post Frame
     * Only Pre Frame 0 = Always in our setup, so only DCD[0] needs population.
     * DCD[1] and DCD[2] stay zeroed (their corresponding modes are Never). */
    memset(dcd, 0, 3 * 128);
    /* Flags 0: Multisample enable=0, no culling, depth/stencil ops default off
     *   Allow forward pixel to kill = 1 (bit 0)
     *   Allow forward pixel to be killed = 1 (bit 1)
     *   Allow primitive reorder = 1 (bit 6)
     */
    dcd[0] = (1u << 0) | (1u << 1) | (1u << 6);
    /* Flags 1: sample_mask = 0xFFFF, render_target_mask = 0x1 (RT0 only) */
    dcd[1] = 0xFFFF | (0x1u << 16);
    /* Min Z = 0.0, Max Z = 1.0 */
    dcd[6] = 0x00000000;
    dcd[7] = 0x3F800000;
    /* Depth/stencil pointer at DCD word 10-11 (DCD+0x28) */
    *(uint64_t *)(dcd + 10) = gva + OFF_SHADER_DEPTH;
    /* Blend count = 1, Blend pointer (shr(4)) */
    uint64_t blend_ptr = gva + OFF_SHADER_BLEND;
    *(uint64_t *)(dcd + 12) = 1ULL | blend_ptr;  /* low nibble 1, ptr 16-byte aligned */
    /* Shader Environment at DCD+0x40 (word 16) */
    /* attribute_offset = 0, fau_count = 0 */
    /* Resources pointer at DCD+0x60 -- pointer | resource_table_count.
     * Resource tables are aligned, low bits encode the count.
     * For our 1-table allocation: ptr | 1.
     * For Mali on v9, resources are 64-byte aligned so low 6 bits hold count. */
    *(uint64_t *)(dcd + 24) = (gva + OFF_SHADER_RESOURCES) | 1ULL;
    /* Shader pointer at DCD+0x68 -- this is the SHADER_PROGRAM descriptor VA,
     * NOT the raw ISA. SHADER_PROGRAM struct (32 bytes) wraps the binary
     * with stage, register-allocation, and preload metadata. */
    *(uint64_t *)(dcd + 26) = gva + OFF_SHADER_PROGRAM;
    /* TLS pointer at DCD+0x70 -- minimal Local Storage descriptor */
    *(uint64_t *)(dcd + 28) = gva + OFF_SHADER_TLS;
    /* FAU pointer at DCD+0x78 -- 0 since fau_count = 0 */
    *(uint64_t *)(dcd + 30) = 0;

    /* Build minimal v9 Depth/stencil descriptor (32 bytes / 8 words):
     *   word 0:
     *     [3:0]   Type = Depth/stencil (7)
     *     [6:4]   Front compare function = Always (7)
     *     [9:7]   Front stencil fail = Keep (0)
     *     [12:10] Front depth fail    = Keep (0)
     *     [15:13] Front depth pass    = Keep (0)
     *     [18:16] Back compare function = Always (7)
     *     [29:24] Back ops = Keep
     *     [30] Stencil from shader = 0
     *     [31] Stencil test enable = 0
     *   word 4:
     *     [22] Depth cull enable = 1
     *     [24:23] Depth clamp mode = [0,1] (0)
     *     [26:25] Depth source = Fixed function (0)
     *     [27] Depth write enable = 0
     *     [28] Depth bias enable = 0
     *     [31:29] Depth function = Always (7)
     */
    {
        uint32_t *zs = (uint32_t *)(base + OFF_SHADER_DEPTH);
        memset(zs, 0, 32);
        zs[0] = (7u << 0)    /* Type = Depth/stencil */
              | (7u << 4)    /* Front compare = Always */
              | (7u << 16);  /* Back compare = Always */
        zs[4] = (1u << 22)   /* Depth cull enable = true */
              | (7u << 29);  /* Depth function = Always */
    }

    /* Write minimal v9 "Local Storage" descriptor (32 bytes / 8 words):
     *
     *   word 0 [4:0]   = TLS Size = 0
     *   word 1 [4:0]   = WLS Instances log2 (NO_WORKGROUP_MEM = 0x80000000)
     *   word 2-3 [47:0]= TLS Base Pointer (48 bits, low)
     *   word 3 [31:28] = TLS Address Mode (0 = Flat)
     *   word 4-5       = WLS Base Pointer = 0
     *
     * For a fragment shader with no register spill / no shared memory:
     *   TLS size = 0, WLS = NO_WORKGROUP_MEM (0x80000000)
     *
     * Even with TLS Size = 0, we point TLS Base at a valid scratch
     * allocation so the GPU never dereferences VA 0x0 if it touches
     * the TLS base register during fragment thread launch.
     */
    {
        uint32_t *ls = (uint32_t *)(base + OFF_SHADER_TLS);
        memset(ls, 0, 32);
        ls[0] = 0;                        /* TLS Size = 0 */
        ls[1] = 0x80000000u;              /* WLS Instances = NO_WORKGROUP_MEM */
        /* TLS Base Pointer at words 2-3 (48-bit) -- give it a valid backing VA.
         * Use OFF_SCRATCH_HEAP which is a 4 KiB scratch region in our same
         * SAME_VA mapping, already zero. */
        uint64_t tls_base = gva + OFF_SCRATCH_HEAP;
        ls[2] = (uint32_t)(tls_base & 0xFFFFFFFFu);
        ls[3] = (uint32_t)((tls_base >> 32) & 0xFFFFu);
        /* TLS Address Mode = Flat (0) -- already zero */
        /* WLS Base Pointer = 0 (no workgroup mem) */
    }

    /* Now patch the MFBD to enable the frame shader */
    /* MFBD word 0: Pre Frame 0 = configurable (default Always = 1) */
    uint32_t *mfbd = (uint32_t *)(base + OFF_SCRATCH_MFBD);
    mfbd[0] = (uint32_t)(g_shader_pre_frame_mode & 0x7);
    /* Frame Shader DCDs pointer at MFBD+0x18 (word 6) -- points to array of DCD pointers? Or DCD itself?
     * In v9, this field is "Frame Shader DCDs" = address. Single DCD here. */
    *(uint64_t *)(base + OFF_SCRATCH_MFBD + 0x18) = gva + OFF_SHADER_DCD;

    /* Disable the RT-write-enable clear path so we see the shader's output, not the clear.
     * Actually keep RT Write Enable=1 so the tile gets written back; just change clear color
     * to a sentinel so we can distinguish shader output from clear. */
    /* Set clear color so it differs from the expected shader output, otherwise
     * we cannot distinguish "shader didn't run" from "shader ran successfully". */
    uint32_t clear_sentinel = g_shader_use_red ? 0xFF00FF00 : 0xFF0000FF;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x30) = clear_sentinel;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x34) = clear_sentinel;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x38) = clear_sentinel;
    *(uint32_t *)(base + OFF_SCRATCH_RT + 0x3C) = clear_sentinel;

    /* Build a Cache Flush Job (Type 3) with L2 Clean = 1, and chain it
     * after the Fragment Job. Without this, at 256x256 the GPU's tile
     * writeback to memory races with the JOB_DONE event delivery, leaving
     * some tiles still in L2 cache (CPU sees clear-color sentinel).
     *
     * Job Header (32 bytes):
     *   word 0/1/2/3 = 0 (exception status, fault pointer)
     *   word 4 [7:1] = Type (3 << 1 = 6)
     *   word 5       = 0 (no deps in single-atom chain)
     *   word 6/7     = Next pointer = 0 (terminate chain)
     *
     * Cache Flush Job Payload (8 bytes at offset 32):
     *   word 0 bit 0  = Clean Shader Core LS = 1
     *   word 0 bit 16 = Job Manager Clean    = 1
     *   word 0 bit 24 = Tiler Clean          = 1
     *   word 1 bit 0  = L2 Clean             = 1  (the critical one)
     */
    {
        uint32_t *fl = (uint32_t *)(base + OFF_SHADER_FLUSH_JC);
        memset(fl, 0, 64);
        fl[4] = (3u << 1);                  /* Type = Cache flush */
        /* fl[6/7] = Next = 0 (chain end) */
        fl[8]  = (1u << 0)                  /* Clean Shader Core LS */
               | (1u << 16)                 /* Job Manager Clean */
               | (1u << 24);                /* Tiler Clean */
        fl[9]  = (1u << 0);                 /* L2 Clean */
    }
    /* Patch the existing Fragment Job to chain to the Cache Flush Job. */
    {
        uint8_t *jc = base + OFF_SCRATCH_FRAG_JC;
        *(uint64_t *)(jc + 0x18) = gva + OFF_SHADER_FLUSH_JC;
    }

    /* Optionally wire a real Tiler Context. With SHADER_TILER=1 we set up
     * a TILER_HEAP descriptor + TILER_CONTEXT and patch FBD.Tiler. The
     * suspicion is that 256x256 needs the GPU's tiler to know about the
     * framebuffer extent so the per-shader-core tile-writeback is fully
     * sequenced. */
    if (g_shader_with_tiler) {
        build_tiler_context(cpu, gva, fb_w, fb_h);
    }

    printf("shader_fbd: shader ISA at gpu 0x%llx (%zu bytes)\n",
           (unsigned long long)(gva + OFF_SHADER_ISA), sizeof(k_valhall_green_fs));
    printf("shader_fbd: DCD at gpu 0x%llx, blend at 0x%llx, TLS at 0x%llx\n",
           (unsigned long long)(gva + OFF_SHADER_DCD),
           (unsigned long long)(gva + OFF_SHADER_BLEND),
           (unsigned long long)(gva + OFF_SHADER_TLS));
    dump_words("shader ISA", base + OFF_SHADER_ISA, sizeof(k_valhall_green_fs));
    dump_words("shader DCD", base + OFF_SHADER_DCD, 128);
    dump_words("shader Blend", base + OFF_SHADER_BLEND, 16);
    dump_words("shader Flush JC", base + OFF_SHADER_FLUSH_JC, 64);
    dump_words("shader MFBD (post-patch)", base + OFF_SCRATCH_MFBD, 0x80);
}

static void drain_events(int fd) {
    for (int i = 0; i < 8; i++) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) {
            printf("event drain: no more events after %d reads\n", i);
            return;
        }
        uint8_t ev[24] = {0};
        ssize_t n = read(fd, ev, sizeof(ev));
        if (n <= 0) {
            printf("event drain: read=%zd errno=%d (%s)\n", n, errno, strerror(errno));
            return;
        }
        printf("event[%d] read=%zd code=0x%x atom=%u data0=0x%x data1=0x%x\n",
               i, n,
               *(uint32_t *)ev,
               *(uint32_t *)(ev + 4),
               *(uint32_t *)(ev + 8),
               *(uint32_t *)(ev + 12));
    }
}

int main(int argc, char **argv) {
    const char *asset_dir = argc > 1 ? argv[1] : ".";
    const char *mode = argc > 2 ? argv[2] : "exact4";
    printf("=== Replay EGL Triangle From Vendor Capture ===\n");
    printf("asset dir: %s\n", asset_dir);
    printf("mode: %s\n", mode);

    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }
    uint16_t ver = 11;
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) { perror("VERSION_CHECK"); return 1; }
    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) { perror("SET_FLAGS"); return 1; }

    size_t total_pages = 256;
    /* 0x200F = CPU_RD|CPU_WR|GPU_RD|GPU_WR | SAME_VA(0x2000)
     * Write-combine; reads bypass CPU cache. */
    uint64_t mem[4] = { total_pages, total_pages, 0, 0x200F };
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) { perror("MEM_ALLOC"); return 1; }
    void *cpu = mmap(NULL, total_pages * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) { perror("mmap"); return 1; }
    uint64_t gva = (uint64_t)cpu;
    memset(cpu, 0, total_pages * PAGE_SIZE);
    printf("mapped SAME_VA cpu/gpu = 0x%llx\n", (unsigned long long)gva);

    /* Allocate a separate executable page for shader binaries (GPU_EX flag).
     * Flags: CPU_RD | CPU_WR | GPU_RD | GPU_EX | SAME_VA = 0x2015
     * Note: shader memory should NOT be GPU_WR (GPU_EX + GPU_WR is rejected
     * on most kernels for security). */
    {
        uint64_t mem_ex[4] = { 1 /*va_pages*/, 1 /*commit*/, 0,
                               0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x2000 };
        if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_ex) >= 0) {
            void *exec = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, mem_ex[1]);
            if (exec != MAP_FAILED) {
                memset(exec, 0, PAGE_SIZE);
                g_shader_exec_cpu = exec;
                g_shader_exec_va = (uint64_t)exec;
                printf("allocated GPU_EX shader page cpu/gpu = 0x%llx\n",
                       (unsigned long long)g_shader_exec_va);
            } else {
                perror("mmap shader_exec");
            }
        } else {
            printf("MEM_ALLOC GPU_EX failed errno=%d (%s) -- shader will use non-exec fallback\n",
                   errno, strerror(errno));
        }
    }

    struct page_asset pages[sizeof(k_page_assets) / sizeof(k_page_assets[0])];
    memcpy(pages, k_page_assets, sizeof(k_page_assets));
    if (load_assets(asset_dir, cpu, gva, pages, sizeof(pages) / sizeof(pages[0])) != 0) {
        return 1;
    }
    uint8_t *baseline = malloc((sizeof(pages) / sizeof(pages[0])) * PAGE_SIZE);
    if (!baseline) {
        perror("malloc baseline");
        return 1;
    }
    memcpy(baseline, (uint8_t *)cpu + 0x10000, (sizeof(pages) / sizeof(pages[0])) * PAGE_SIZE);

    struct kbase_atom_mtk atoms[4];
    char atoms_path[512];
    path_join(atoms_path, sizeof(atoms_path), asset_dir, "001_atoms_raw.bin");
    if (read_file(atoms_path, atoms, sizeof(atoms)) != 0) {
        fprintf(stderr, "Failed to read %s\n", atoms_path);
        return 1;
    }

    atoms[0].jc = gva + OFF_SOFT0;
    atoms[1].jc = gva + OFF_COMPUTE;
    atoms[2].jc = gva + OFF_FRAG;
    atoms[3].jc = gva + OFF_SOFT3;

    int is_shader_mode = (strncmp(mode, "shader_fbd", 10) == 0);
    if (strcmp(mode, "scratch_fbd") == 0 || strcmp(mode, "scratch_fbd_64") == 0
        || strcmp(mode, "scratch_fbd_256") == 0
        || is_shader_mode) {
        int fb_w = 16, fb_h = 16;
        if (strstr(mode, "_256")) { fb_w = 256; fb_h = 256; }
        else if (strstr(mode, "_64")) { fb_w = 64; fb_h = 64; }
        uint64_t color_off = (fb_w * fb_h * 4 > 0x1000) ? OFF_SCRATCH_COLOR_LG : OFF_SCRATCH_COLOR;
        if (is_shader_mode) {
            build_shader_fbd(cpu, gva, fb_w, fb_h, color_off);
        } else {
            build_scratch_fbd(cpu, gva, fb_w, fb_h, color_off);
        }

        struct kbase_atom_mtk frag_atom;
        memset(&frag_atom, 0, sizeof(frag_atom));
        frag_atom.jc = gva + OFF_SCRATCH_FRAG_JC;
        frag_atom.atom_number = 1;
        frag_atom.core_req = 0x001;

        struct kbase_ioctl_job_submit sub = {0};
        sub.addr = (uint64_t)&frag_atom;
        sub.nr_atoms = 1;
        sub.stride = sizeof(struct kbase_atom_mtk);
        int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub);
        printf("JOB_SUBMIT (scratch_fbd) ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
        drain_events(fd);

        volatile uint32_t *color = (volatile uint32_t *)((uint8_t *)cpu + color_off);
        int changed = 0, n_green = 0, n_red = 0, n_other = 0;
        uint32_t expected_shader = g_shader_use_red ? 0xff0000ff : 0xff00ff00;
        uint32_t clear_sentinel  = g_shader_use_red ? 0xff00ff00 : 0xff0000ff;
        int first_red_idx = -1, last_red_idx = -1;
        for (int i = 0; i < fb_w * fb_h; i++) {
            uint32_t v = color[i];
            if (v != 0xdeadbeef) {
                changed++;
                if (v == expected_shader) {
                    n_green++;
                } else if (v == clear_sentinel) {
                    n_red++;
                    if (first_red_idx < 0) first_red_idx = i;
                    last_red_idx = i;
                } else {
                    n_other++;
                    if (n_other <= 16) {
                        int x = i % fb_w, y = i / fb_w;
                        printf("color[%d] (%d,%d) = 0x%08x (other)\n", i, x, y, v);
                    }
                }
                if (changed <= 4) {
                    int x = i % fb_w, y = i / fb_w;
                    printf("color[%d] (%d,%d) = 0x%08x\n", i, x, y, v);
                }
            }
        }
        printf("scratch_fbd: color changed=%d / %d (%dx%d)\n", changed, fb_w * fb_h, fb_w, fb_h);
        printf("scratch_fbd: shader_color=%d clear_sentinel=%d other=%d\n", n_green, n_red, n_other);
        if (n_red > 0) {
            int fx = first_red_idx % fb_w, fy = first_red_idx / fb_w;
            int lx = last_red_idx % fb_w, ly = last_red_idx / fb_w;
            int ftx = fx / 16, fty = fy / 16;
            int ltx = lx / 16, lty = ly / 16;
            printf("scratch_fbd: first sentinel pixel idx=%d (%d,%d) tile=(%d,%d)\n",
                   first_red_idx, fx, fy, ftx, fty);
            printf("scratch_fbd: last  sentinel pixel idx=%d (%d,%d) tile=(%d,%d)\n",
                   last_red_idx, lx, ly, ltx, lty);
            /* Per-tile red count to identify which tiles failed to write back */
            int tiles_x = (fb_w + 15) / 16;
            int tiles_y = (fb_h + 15) / 16;
            int total_tiles = tiles_x * tiles_y;
            int *tile_red = calloc(total_tiles, sizeof(int));
            if (tile_red) {
                for (int i = 0; i < fb_w * fb_h; i++) {
                    if (color[i] == clear_sentinel) {
                        int x = i % fb_w, y = i / fb_w;
                        tile_red[(y/16) * tiles_x + (x/16)]++;
                    }
                }
                int affected = 0;
                printf("scratch_fbd: tiles with sentinel pixels (out of %d):\n", total_tiles);
                for (int ty = 0; ty < tiles_y; ty++) {
                    for (int tx = 0; tx < tiles_x; tx++) {
                        int c = tile_red[ty * tiles_x + tx];
                        if (c > 0) {
                            if (affected < 32) {
                                printf("  tile (%d,%d) red=%d\n", tx, ty, c);
                            }
                            affected++;
                        }
                    }
                }
                printf("scratch_fbd: affected tiles=%d / %d\n", affected, total_tiles);
                free(tile_red);
            }
        }
        if (changed == 0) {
            printf("scratch_fbd: NO pixels written\n");
        } else {
            printf("scratch_fbd: first=0x%08x last=0x%08x\n", color[0], color[fb_w*fb_h-1]);
        }
        dump_words("POST scratch MFBD", (uint8_t *)cpu + OFF_SCRATCH_MFBD, 0x80);
        dump_words("POST scratch RT0", (uint8_t *)cpu + OFF_SCRATCH_RT, 0x40);
        dump_words("POST scratch frag JC", (uint8_t *)cpu + OFF_SCRATCH_FRAG_JC, 0x40);
        if (is_shader_mode) {
            dump_words("POST shader Flush JC", (uint8_t *)cpu + OFF_SHADER_FLUSH_JC, 0x40);
        }
        free(baseline);
        munmap(cpu, total_pages * PAGE_SIZE);
        close(fd);
        return 0;
    }

    if (strcmp(mode, "hw2_zero_fc4000") == 0) {
        struct page_asset *rt = find_page(pages, sizeof(pages) / sizeof(pages[0]), 0x5efffc4000ULL);
        if (!rt) {
            fprintf(stderr, "missing fc4000 page\n");
            return 1;
        }
        memset((void *)(uintptr_t)rt->new_addr, 0, PAGE_SIZE);
        printf("zeroed relocated page for 0x5efffc4000 at 0x%llx\n", (unsigned long long)rt->new_addr);
        mode = "hw2";
    } else if (strcmp(mode, "hw2_zero_fc2540") == 0) {
        struct page_asset *rt = find_page(pages, sizeof(pages) / sizeof(pages[0]), 0x5efffc2000ULL);
        if (!rt) {
            fprintf(stderr, "missing fc2000 page\n");
            return 1;
        }
        memset((uint8_t *)(uintptr_t)rt->new_addr + 0x540, 0, 0x80);
        printf("zeroed relocated fc2540 region inside 0x5efffc2000 at 0x%llx\n",
               (unsigned long long)(rt->new_addr + 0x540));
        mode = "hw2";
    } else if (strcmp(mode, "hw2_hybrid_fbd") == 0) {
        uint64_t dcd_addr = *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x18);
        build_hybrid_fbd(cpu, gva, dcd_addr);
        *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x28) = (gva + OFF_HYBRID_FBD) | 1ULL;
        printf("built hybrid FBD at 0x%llx RT at 0x%llx color at 0x%llx\n",
               (unsigned long long)(gva + OFF_HYBRID_FBD),
               (unsigned long long)(gva + OFF_HYBRID_RT),
               (unsigned long long)(gva + OFF_HYBRID_COLOR));
        mode = "hw2";
    } else if (strcmp(mode, "hw2_hybrid_fbd_dcd28_color") == 0) {
        uint64_t dcd_addr = *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x18);
        uint8_t *dcd_cpu = (uint8_t *)cpu + (dcd_addr - gva);
        build_hybrid_fbd(cpu, gva, dcd_addr);
        *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x28) = (gva + OFF_HYBRID_FBD) | 1ULL;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_FBD + 0x18) = dcd_addr;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_FBD + 0xa0) = gva + OFF_HYBRID_RT;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_RT + 0x00) = gva + OFF_HYBRID_COLOR;
        *(uint64_t *)(dcd_cpu + 0x28) = gva + OFF_HYBRID_COLOR;
        printf("built hybrid FBD and patched DCD+0x28 to color buffer 0x%llx\n",
               (unsigned long long)(gva + OFF_HYBRID_COLOR));
        mode = "hw2";
    } else if (strcmp(mode, "hw3_hybrid_fbd_dcd28_color_soft3") == 0) {
        uint64_t dcd_addr = *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x18);
        uint8_t *dcd_cpu = (uint8_t *)cpu + (dcd_addr - gva);
        build_hybrid_fbd(cpu, gva, dcd_addr);
        *(uint64_t *)((uint8_t *)cpu + OFF_FRAG + 0x28) = (gva + OFF_HYBRID_FBD) | 1ULL;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_FBD + 0x18) = dcd_addr;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_FBD + 0xa0) = gva + OFF_HYBRID_RT;
        *(uint64_t *)((uint8_t *)cpu + OFF_HYBRID_RT + 0x00) = gva + OFF_HYBRID_COLOR;
        *(uint64_t *)(dcd_cpu + 0x28) = gva + OFF_HYBRID_COLOR;
        printf("built hybrid FBD + DCD patch and will include soft atom 3\n");
        mode = "hw3";
    }

    dump_words("soft0", (uint8_t *)cpu + OFF_SOFT0, 64);
    dump_words("compute jc", (uint8_t *)cpu + OFF_COMPUTE, 128);
    dump_words("frag jc", (uint8_t *)cpu + OFF_FRAG, 64);
    dump_words("soft3", (uint8_t *)cpu + OFF_SOFT3, 64);

    struct kbase_ioctl_job_submit submit = {0};
    if (strcmp(mode, "hw2") == 0) {
        struct kbase_atom_mtk hw_atoms[2];
        memset(hw_atoms, 0, sizeof(hw_atoms));
        hw_atoms[0] = atoms[1];
        hw_atoms[1] = atoms[2];
        hw_atoms[0].atom_number = 1;
        hw_atoms[0].pre_dep[0].atom_id = 0;
        hw_atoms[0].pre_dep[0].dep_type = 0;
        hw_atoms[0].pre_dep[1].atom_id = 0;
        hw_atoms[0].pre_dep[1].dep_type = 0;
        hw_atoms[1].atom_number = 2;
        hw_atoms[1].pre_dep[0].atom_id = 1;
        hw_atoms[1].pre_dep[0].dep_type = 1;
        submit.addr = (uint64_t)hw_atoms;
        submit.nr_atoms = 2;
        submit.stride = sizeof(struct kbase_atom_mtk);
        int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
        printf("JOB_SUBMIT ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
        drain_events(fd);
        for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
            uint8_t *now = (uint8_t *)cpu + 0x10000 + i * PAGE_SIZE;
            if (memcmp(now, baseline + i * PAGE_SIZE, PAGE_SIZE) != 0) {
                size_t first = 0;
                while (first < PAGE_SIZE && now[first] == baseline[i * PAGE_SIZE + first]) first++;
                printf("page changed: orig=0x%llx new=0x%llx first_diff=0x%zx before=%02x after=%02x\n",
                       (unsigned long long)pages[i].orig_page,
                       (unsigned long long)pages[i].new_addr,
                       first,
                       baseline[i * PAGE_SIZE + first],
                       now[first]);
            }
        }
        volatile uint32_t *color = (volatile uint32_t *)((uint8_t *)cpu + OFF_HYBRID_COLOR);
        int changed = 0;
        for (int i = 0; i < 64 * 64; i++) {
            if (color[i] != 0xdeadbeef) {
                changed++;
                if (changed <= 8) {
                    printf("hybrid_color[%d]=0x%08x\n", i, color[i]);
                }
            }
        }
        printf("hybrid color changed=%d first=0x%08x center=0x%08x\n",
               changed, color[0], color[(32 * 64) + 32]);
        free(baseline);
        munmap(cpu, total_pages * PAGE_SIZE);
        close(fd);
        return 0;
    } else if (strcmp(mode, "hw3") == 0) {
        struct kbase_atom_mtk replay_atoms[3];
        memset(replay_atoms, 0, sizeof(replay_atoms));
        replay_atoms[0] = atoms[1];
        replay_atoms[1] = atoms[2];
        replay_atoms[2] = atoms[3];

        replay_atoms[0].atom_number = 1;
        replay_atoms[0].pre_dep[0].atom_id = 0;
        replay_atoms[0].pre_dep[0].dep_type = 0;
        replay_atoms[0].pre_dep[1].atom_id = 0;
        replay_atoms[0].pre_dep[1].dep_type = 0;

        replay_atoms[1].atom_number = 2;
        replay_atoms[1].pre_dep[0].atom_id = 1;
        replay_atoms[1].pre_dep[0].dep_type = 1;
        replay_atoms[1].pre_dep[1].atom_id = 0;
        replay_atoms[1].pre_dep[1].dep_type = 0;

        replay_atoms[2].atom_number = 3;
        replay_atoms[2].pre_dep[0].atom_id = 2;
        replay_atoms[2].pre_dep[0].dep_type = 2;
        replay_atoms[2].pre_dep[1].atom_id = 0;
        replay_atoms[2].pre_dep[1].dep_type = 0;

        submit.addr = (uint64_t)replay_atoms;
        submit.nr_atoms = 3;
        submit.stride = sizeof(struct kbase_atom_mtk);
        int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
        printf("JOB_SUBMIT ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
        drain_events(fd);
        for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
            uint8_t *now = (uint8_t *)cpu + 0x10000 + i * PAGE_SIZE;
            if (memcmp(now, baseline + i * PAGE_SIZE, PAGE_SIZE) != 0) {
                size_t first = 0;
                while (first < PAGE_SIZE && now[first] == baseline[i * PAGE_SIZE + first]) first++;
                printf("page changed: orig=0x%llx new=0x%llx first_diff=0x%zx before=%02x after=%02x\n",
                       (unsigned long long)pages[i].orig_page,
                       (unsigned long long)pages[i].new_addr,
                       first,
                       baseline[i * PAGE_SIZE + first],
                       now[first]);
            }
        }
        volatile uint32_t *color = (volatile uint32_t *)((uint8_t *)cpu + OFF_HYBRID_COLOR);
        int changed = 0;
        for (int i = 0; i < 64 * 64; i++) {
            if (color[i] != 0xdeadbeef) {
                changed++;
                if (changed <= 8) {
                    printf("hybrid_color[%d]=0x%08x\n", i, color[i]);
                }
            }
        }
        printf("hybrid color changed=%d first=0x%08x center=0x%08x\n",
               changed, color[0], color[(32 * 64) + 32]);
        free(baseline);
        munmap(cpu, total_pages * PAGE_SIZE);
        close(fd);
        return 0;
    }

    submit.addr = (uint64_t)atoms;
    submit.nr_atoms = 4;
    submit.stride = sizeof(struct kbase_atom_mtk);

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("JOB_SUBMIT ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
    drain_events(fd);
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        uint8_t *now = (uint8_t *)cpu + 0x10000 + i * PAGE_SIZE;
        if (memcmp(now, baseline + i * PAGE_SIZE, PAGE_SIZE) != 0) {
            size_t first = 0;
            while (first < PAGE_SIZE && now[first] == baseline[i * PAGE_SIZE + first]) first++;
            printf("page changed: orig=0x%llx new=0x%llx first_diff=0x%zx before=%02x after=%02x\n",
                   (unsigned long long)pages[i].orig_page,
                   (unsigned long long)pages[i].new_addr,
                   first,
                   baseline[i * PAGE_SIZE + first],
                   now[first]);
        }
    }
    free(baseline);

    munmap(cpu, total_pages * PAGE_SIZE);
    close(fd);
    return 0;
}
