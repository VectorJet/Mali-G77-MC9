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

/*
 * Kernel UAPI reference headers (verified from refs/mesa-Panfork-android/):
 *   mali_kbase_jm_ioctl.h  — JOB_SUBMIT, VERSION_CHECK ioctls + structs
 *   mali_base_jm_kernel.h  — base_jd_atom (64 B), core_req flags, event codes
 *   mali_base_kernel.h     — MEM_ALLOC flags (BASE_MEM_SAME_VA, etc.)
 *   mali_kbase_ioctl.h     — MEM_ALLOC, SET_FLAGS, GET_GPUPROPS ioctls
 *
 * Valhall descriptor definitions (from refs/panfork/):
 *   src/panfrost/lib/genxml/v9.xml  — MFBD, DCD, blend, tiler structures
 *   src/panfrost/compiler/valhall/test/assembler-cases.txt  — ISA encodings
 *   src/panfrost/lib/kmod/          — pan_kmod API (kmod abstraction layer)
 */

/* Official kernel UAPI ioctl defines (refs/mesa-Panfork-android/):
 * struct kbase_ioctl_version_check { u16 major, u16 minor; };
 * #define KBASE_IOCTL_VERSION_CHECK _IOWR(0x80, 0, struct kbase_ioctl_version_check)
 * struct kbase_ioctl_set_flags { u32 create_flags; };
 * #define KBASE_IOCTL_SET_FLAGS _IOW(0x80, 1, struct kbase_ioctl_set_flags)
 * struct kbase_ioctl_job_submit { u64 addr; u32 nr_atoms; u32 stride; };
 * #define KBASE_IOCTL_JOB_SUBMIT _IOW(0x80, 2, struct kbase_ioctl_job_submit)
 * union kbase_ioctl_mem_alloc { in/out structs };
 * #define KBASE_IOCTL_MEM_ALLOC _IOWR(0x80, 5, union kbase_ioctl_mem_alloc)
 */
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

/* Triangle mode offsets (TILER_JOB approach — no vertex shader, uses
 * fixed-function Vertex Array for position data + FBD for fragment shader) */
#define OFF_TRI_POS           0xE000  /* Position buffer (3 x vec4 = 48 bytes) */
#define OFF_TRI_BLEND         0xE040  /* Blend descriptor (16 bytes) */
#define OFF_TRI_DEPTH         0xE060  /* Depth/stencil descriptor (32 bytes) */
#define OFF_TRI_TLS           0xE0A0  /* TLS/Local Storage (32 bytes) */
#define OFF_TRI_VTX_JOB       0xE200  /* TILER_JOB (256 bytes, 128-align at 0xE200=113*128) */
#define OFF_TRI_INDICES       0xE0C0  /* Index buffer (3 x uint16_t = 6 bytes) */
#define OFF_TRI_FRAG_JC       0xE380  /* Fragment JC (128 bytes, 128-align at 0xE380) */

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
    uint32_t max_tile_units = (uint32_t)(((fb_w / 16) - 1) | (((fb_h / 16) - 1) << 16));
    params[0] = (fb_w - 1) | ((fb_h - 1) << 16);  /* Bound max (pixel coords) */
    params[1] = max_tile_units;                     /* Max Tile (tile units: 0 for 16x16, 0x00030003 for 64x64, 0x000f000f for 256x256) */
    params[2] = (fb_w - 1) | ((fb_h - 1) << 16);  /* Render bounds */
    /* Packed params for single RT, 16x16 framebuffer, RGBA8:
     *   - Sample Count (bits 0-2) = 0
     *   - Sample Pattern (bits 3-5) = 0
     *   - Tie-Break Rule (bits 6-8) = 2
     *   - Effective Tile Size (bits 9-12) = 0 (16x16 tiles)
     *   - Render Target Count (bits 19-22) = 1
     *   - Color Buffer Allocation (bits 24-31) = 1 (1024 bytes / 1024)
     * Previously had Effective Tile Size = 8 and Render Target Count = 0,
     * which caused 0x58 DATA_INVALID when tiler pointer is active. */
    params[3] = (2 << 6) | (1 << 19) | (1 << 24); /* Render Target Count = 1, Tile Size 16x16 */
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
    /* NOTE: Bit 0 of the MFBD pointer may control tile iteration mode.
     * Bit 0=0 → iterate all tiles directly (like tiler=NULL mode).
     * Bit 0=1 → follow polygon list from tiler context.
     * With tiler=active, clearing bit 0 forces tile-iteration to bypass
     * the polygon list, which avoids the 0x58 DATA_INVALID fault if the
     * polygon list format is incompatible. */
    *(uint64_t *)(jc + 10) = (gva + OFF_SCRATCH_MFBD) | 0x00;

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

/* No vertex shader needed for triangle mode — we supply screen-space
 * positions via the TILER_JOB's fixed-function Vertex Array which feeds
 * the parameter assembler directly, bypassing the shader core.
 * The fragment shader is provided by the FBD's Frame Shader DCD. */

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

/* Triangle mode tuneables */
static int g_triangle_fb_w = 256;
static int g_triangle_fb_h = 256;
static int g_triangle_use_fs = 1;  /* 1 = use our hand-crafted fragment shader */

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
    /* word 0/1 = Polygon List — MUST point to valid writable memory!
     * The tiler writes the produced polygon list here during binning.
     * NULL (0) causes DATA_INVALID (0x58) as the GPU faults on the
     * write to address zero. */
    *(uint64_t *)(tc + 0) = gva + OFF_SCRATCH_POLYLIST;
    /* Hierarchy Mask: each bit enables one bin size level starting from 16x16.
     * NOTE: hierarchy_mask=0x1f (5 levels for 256x256) produced EMPTY polygon list output.
     * Reverting to 0x1 (level 0 only = 16x16 bin) which DID produce polygon list output.
     * The mask expansion broke the tiler output -- the tiler may require more polygon list
     * space per level, or the multi-level layout differs from what we assumed.
     * TODO: investigate per-level polygon list layout before re-enabling higher levels. */
    uint32_t hierarchy_mask = 1u;
    const char *h_env = getenv("TRI_HIERARCHY");
    if (h_env) hierarchy_mask = (uint32_t)atoi(h_env);
    printf("tiler_ctx: fb=%dx%d hierarchy_mask=0x%x\n", fb_w, fb_h, hierarchy_mask);
    tc[2] = hierarchy_mask;      /* Hierarchy Mask */
    /* FB Width/Height: use raw pixel dimensions, NOT max-index (panfrost v9 genxml
     * uses FB Width/FB Height, not Width-1/Height-1 as in MFBD params).
     * Previously used (fb_w-1)|((fb_h-1)<<16) which encoded 15x15 for a 16x16 fb. */
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

/* Build a full triangle rendering pipeline using v9 TILER_JOB (type=7).
 *
 * TILER_JOB (256 bytes) is the standard tiler job format shared across
 * all Mali architectures (Midgard/Bifrost/Valhall). It includes a Draw
 * section with Shader Environment for vertex shader binding, and the
 * tiler context for binning.
 *
 * Unlike MALLOC_VERTEX_JOB (Type 11), TILER_JOB is supported on all
 * MTK r49 job slots — no Position/Varying Shader Environments at
 * offsets 0x100/0x140.
 *
 * Memory layout of TILER_JOB (256 bytes, align 128):
 *   +0x000: Header (32B, type=7)
 *   +0x020: Primitive (16B)
 *   +0x030: Instance Count (4B)
 *   +0x034: Vertex Count (4B)
 *   +0x038: Tiler Pointer (8B)
 *   +0x068: Scissor (8B)
 *   +0x070: Primitive Size (8B)
 *   +0x078: Indices (8B)
 *   +0x080: Draw (128B) — includes Shader Environment at offset 0xC0
 *
 * Attribute descriptors at OFF_TRI_ATTR/ATTR_BUF and a resource table
 * at OFF_TRI_RES_TABLE provide LD_VAR-based vertex attribute access
 * for the passthrough vertex shader.
 */
static void build_triangle_mode(void *cpu, uint64_t gva, int fb_w, int fb_h) {
    uint8_t *base = (uint8_t *)cpu;
    printf("\n=== BUILD TRIANGLE MODE (TILER_JOB, type=7) ===\n");
    printf("framebuffer %dx%d\n", fb_w, fb_h);

    g_triangle_fb_w = fb_w;
    g_triangle_fb_h = fb_h;

    /* === 1. Build scratch MFBD + RT with Frame Shaders ===
     * The fragment shader is wired through the FBD's Frame Shader DCD
     * (MFBD+0x18 → DCD array with Draw struct → SHADER_PROGRAM → ISA).
     * No vertex shader needed — position data from the fixed-function
     * Vertex Array flows through the parameter assembler directly. The
     * fragment shader is provided by the FBD's Frame Shader DCD. */
    uint64_t color_off = (fb_w * fb_h * 4 > 0x1000) ? OFF_SCRATCH_COLOR_LG : OFF_SCRATCH_COLOR;
    build_scratch_fbd(cpu, gva, fb_w, fb_h, color_off);

    /* Enable Pre Frame 0 = Always and wire the DCD pointer (like shader_fbd) */
    uint32_t *mfbd = (uint32_t *)(base + OFF_SCRATCH_MFBD);
    mfbd[0] = 1;  /* Pre Frame 0 = Always */
    *(uint64_t *)(base + OFF_SCRATCH_MFBD + 0x18) = gva + OFF_SHADER_DCD;

    build_tiler_context(cpu, gva, fb_w, fb_h);

    /* === 1b. Build Cache Flush Job at OFF_SHADER_FLUSH_JC ===
     * Type 3 cache flush with L2 Clean + Shader Core LS + JM + Tiler.
     * Placed BEFORE the vertex job in the submission chain to ensure
     * any stale cache lines are flushed before vertex+tiler writes.
     * The flush job chains to nowhere (hardware chain disabled - we
     * use kbase atom dependencies instead). */
    {
        uint32_t *fl = (uint32_t *)(base + OFF_SHADER_FLUSH_JC);
        memset(fl, 0, 64);
        fl[4] = (3u << 1);                  /* Type = Cache flush (3) */
        /* Next = 0 (no hardware chain; deps via kbase atoms) */
        fl[8]  = 0xFFFFFFFF;                /* Invalidate/Clean all core caches */
        fl[9]  = 0xFFFFFFFF;                /* Invalidate/Clean all L2 caches */
    }
    printf("triangle: cache flush job at gpu 0x%llx\n",
           (unsigned long long)(gva + OFF_SHADER_FLUSH_JC));

    /* === 2. Position buffer: 3 full-screen triangle vertices (screen-space) === */
    {
        float *pos = (float *)(base + OFF_TRI_POS);
        const char *sp_env = getenv("TRI_SCREEN_POS");
        if (sp_env && atoi(sp_env)) {
            pos[0]  = 0.0f;           pos[1]  = 0.0f;           pos[2]  = 0.0f; pos[3]  = 1.0f;
            pos[4]  = (float)fb_w;    pos[5]  = 0.0f;           pos[6]  = 0.0f; pos[7]  = 1.0f;
            pos[8]  = 0.0f;           pos[9]  = (float)fb_h;    pos[10] = 0.0f; pos[11] = 1.0f;
            printf("triangle: using SCREEN-SPACE pixel position coordinates {0,0}, {%d,0}, {0,%d}\n", fb_w, fb_h);
        } else {
            pos[0]  = -1.0f;  pos[1]  = -1.0f;  pos[2]  = 0.5f; pos[3]  = 1.0f;
            pos[4]  =  3.0f;  pos[5]  = -1.0f;  pos[6]  = 0.5f; pos[7]  = 1.0f;
            pos[8]  = -1.0f;  pos[9]  =  3.0f;  pos[10] = 0.5f; pos[11] = 1.0f;
        }
    }
    uint64_t pos_addr = gva + OFF_TRI_POS;
    printf("triangle: position buffer at gpu 0x%llx\n", (unsigned long long)pos_addr);

    /* === 2b. Index buffer: uint16_t indices [0, 1, 2] === */
    {
        uint16_t *idx = (uint16_t *)(base + OFF_TRI_INDICES);
        idx[0] = 0;
        idx[1] = 1;
        idx[2] = 2;
    }
    uint64_t idx_addr = gva + OFF_TRI_INDICES;
    printf("triangle: index buffer at gpu 0x%llx\n", (unsigned long long)idx_addr);

    /* === 3. Fragment SHADER_PROGRAM descriptor === */
    uint64_t isa_addr;
    if (g_shader_exec_cpu && g_shader_exec_va) {
        uint8_t *exec = (uint8_t *)g_shader_exec_cpu;
        memcpy(exec, k_valhall_green_fs, 32);
        memcpy(exec + 32, k_valhall_green_fs + 48, 8);
        isa_addr = g_shader_exec_va;
    } else {
        memcpy(base + OFF_SHADER_ISA, k_valhall_green_fs, 32);
        memcpy(base + OFF_SHADER_ISA + 32, k_valhall_green_fs + 48, 8);
        isa_addr = gva + OFF_SHADER_ISA;
    }
    printf("triangle: fragment shader ISA at gpu 0x%llx\n", (unsigned long long)isa_addr);

    uint32_t *sp = (uint32_t *)(base + OFF_SHADER_PROGRAM);
    memset(sp, 0, 32);
    sp[0] = (8u << 0) | (2u << 4) | (1u << 8) | (1u << 28) | (2u << 30);
    sp[2] = (uint32_t)(isa_addr & 0xFFFFFFFFu);
    sp[3] = (uint32_t)(isa_addr >> 32);
    uint64_t sp_addr = gva + OFF_SHADER_PROGRAM;
    printf("triangle: SHADER_PROGRAM at gpu 0x%llx\n", (unsigned long long)sp_addr);

    /* No vertex shader — position data from fixed-function Vertex Array
     * flows directly to the parameter assembler. Fragment shader is
     * provided by the FBD's Frame Shader DCD. */

    /* === 4. Descriptors: Blend, Depth/stencil, TLS === */
    {
        uint32_t *bl = (uint32_t *)(base + OFF_TRI_BLEND);
        memset(bl, 0, 16);
        bl[0] = (1u << 9);                                  /* Enable=1 */
        bl[1] = (2u << 0) | (2u << 4) | (1u << 8) |         /* RGB:   A=Src B=Src C=Zero */
                ((2u << 0) | (2u << 4) | (1u << 8)) << 12 | /* Alpha: same */
                (0xFu << 28);                               /* Color Mask = RGBA */
        bl[2] = (2u << 0) | (3u << 3) | (0u << 16);         /* Mode=2, num_comps-1=3, RT=0 */
        bl[3] = (237u << 12) | 0u;                          /* Conversion: RGBA8_TB | RGBA */
    }
    uint64_t blend_addr = gva + OFF_TRI_BLEND;

    {
        uint32_t *zs = (uint32_t *)(base + OFF_TRI_DEPTH);
        memset(zs, 0, 32);
        zs[0] = (7u << 0) | (7u << 4) | (7u << 16);
        zs[4] = (1u << 22) | (7u << 29);
    }
    uint64_t depth_addr = gva + OFF_TRI_DEPTH;

    {
        uint32_t *ls = (uint32_t *)(base + OFF_TRI_TLS);
        memset(ls, 0, 32);
        ls[0] = 0;
        ls[1] = 0x80000000u;
        uint64_t tls_base = gva + OFF_SCRATCH_HEAP;
        ls[2] = (uint32_t)(tls_base & 0xFFFFFFFFu);
        ls[3] = (uint32_t)((tls_base >> 32) & 0xFFFFu);
    }
    uint64_t tls_addr = gva + OFF_TRI_TLS;

    /* Resource table not needed — no vertex shader means no LD_VAR
     * attribute lookups. Position data flows through fixed-function
     * Vertex Array in the Draw section. */

    /* === 4b. Frame Shader DCD at OFF_SHADER_DCD (reuse shader_fbd area) ===
     * The FBD's Frame Shader DCD pointer (MFBD+0x18) points here.
     * This is an array of 3 Draw descriptors (3 × 128 bytes = 384 bytes)
     * for Pre Frame 0, Pre Frame 1, Post Frame. Only Pre Frame 0 = Always
     * needs population. The Draw struct inside provides the fragment shader
     * (via Shader Environment → SHADER_PROGRAM) and associated descriptors. */
    {
        uint32_t *dcd = (uint32_t *)(base + OFF_SHADER_DCD);
        memset(dcd, 0, 3 * 128);

        /* Pre Frame 0 DCD — match captured vendor 001_atom2_frag_shader_dcd.bin */
        dcd[0] = 0x00000228;                           /* pixel_kill=WEAK_EARLY, zs_update=STRONG_EARLY */
        dcd[1] = 0x0000FFFF;                           /* Sample mask 0xFFFF */
        dcd[6] = 0x00000000;                           /* Min Z */
        dcd[7] = 0x3F800000;                           /* Max Z */
        *(uint64_t *)(dcd + 10) = depth_addr;          /* Depth/stencil */
        *(uint64_t *)(dcd + 12) = 1ULL | blend_addr;   /* Match working shader_fbd: 1ULL | blend_ptr */
        *(uint64_t *)(dcd + 14) = 0;                   /* Occlusion = 0 */
        dcd[17] = 0;                                   /* FAU count = 0 (green_fs uses no FAU uniforms) */
        /* Shader Environment at words 16-31 */
        *(uint64_t *)(dcd + 24) = (gva + OFF_SHADER_RESOURCES) | 1ULL; /* Match working shader_fbd */
        dcd[26] = (uint32_t)(sp_addr & 0xFFFFFFFFu);
        dcd[27] = (uint32_t)(sp_addr >> 32);
        dcd[28] = (uint32_t)(tls_addr & 0xFFFFFFFFu);
        dcd[29] = (uint32_t)(tls_addr >> 32);
        *(uint64_t *)(dcd + 30) = 0;                   /* FAU = 0 */
    }
    printf("triangle: Frame Shader DCD at gpu 0x%llx\n",
           (unsigned long long)(gva + OFF_SHADER_DCD));

    /* === 5. Build MALLOC_VERTEX_JOB (384 bytes at OFF_TRI_VTX_JOB = 0xE200) ===
     * Layout from v9.xml:
     *   0x000: Header (32B, type=7)
     *   0x020: Primitive (16B)
     *   0x030: Instance Count (4B)
     *   0x034: Allocation (4B): vertex_attribute_stride / vertex count
     *   0x038: Tiler Pointer (48B) — Tiler Pointer section
     *   0x068: Scissor (8B)
     *   0x070: Primitive Size (8B)
     *   0x078: Indices (8B)
     *   0x080: Draw (128B) includes Shader Environment
     */
    uint32_t *vt = (uint32_t *)(base + OFF_TRI_VTX_JOB);
    memset(vt, 0, 256);
    uint64_t tiler_ctx_addr = gva + OFF_TILER_CTX;
    uint64_t vtx_job_addr = gva + OFF_TRI_VTX_JOB;

    /* 5a. Header (words 0-7): Type=7 (Tiler), 64-bit. */
    vt[4] = (1u << 0) | (7u << 1);
    *(uint64_t *)(vt + 6) = 0;  /* No hardware chain */

    /* 5b. Primitive (words 8-11, offset 0x20)
     * Match captured vendor atom1_hw_jc.bin:
     * vt[8]  = 0x80000000 (Index Type = U16/U32 enabled)
     * vt[9]  = 0x00008100
     * vt[10] = 1 (primitive count / base index)
     * vt[11] = 3 (index count = 3)
     */
    int use_indexed = 1;
    const char *idx_env = getenv("TRI_INDEXED");
    if (idx_env) use_indexed = atoi(idx_env);

    if (use_indexed) {
        vt[8]    = 0x38008;    /* Triangles + Index Type U16 */
        vt[9]    = 0;
        vt[10]   = 0;
        vt[11]   = 3;          /* Index count = 3 */
        vt[12]   = 1;          /* Instance count = 1 */
        vt[13]   = 3;          /* Vertex count hint = 3 */
        *(uint64_t *)(vt + 30) = idx_addr; /* Index buffer pointer at offset 0x78 */
        printf("triangle: TILER_JOB set up for INDEXED draw (indices at 0x%llx, vt[8]=0x%x)\n",
               (unsigned long long)idx_addr, vt[8]);
    } else {
        vt[8]    = (8u << 0);  /* Non-indexed Triangles */
        vt[9]    = 0;
        vt[10]   = 0;
        vt[11]   = 3;          /* Vertex count = 3 */
        vt[12]   = 1;          /* Instance count = 1 */
        vt[13]   = 3;          /* Vertex count = 3 */
        *(uint64_t *)(vt + 30) = 0;
        printf("triangle: TILER_JOB set up for NON-INDEXED draw\n");
    }

    /* 5e. Tiler Pointer (words 14-15, offset 0x38, and words 24-25, offset 0x60) */
    *(uint64_t *)(vt + 14) = tiler_ctx_addr;
    vt[17] = 4;                            /* Match vendor capture word 17 */
    *(uint64_t *)(vt + 24) = tiler_ctx_addr; /* Valhall v9 Tiler Context pointer at offset 0x60 */

    /* 5f. Scissor (words 26-27, offset 0x68) */
    vt[26] = 0;
    vt[27] = (fb_w - 1) | ((fb_h - 1) << 16);

    /* 5g. Primitive Size (words 28-29, offset 0x70) */
    *(float *)(vt + 28) = 1.0f;

    /* 5h. Indices (words 30-31, offset 0x78) set above in indexed/non-indexed branch */

    /* 5i. Draw (words 32-63, offset 0x80, 128 bytes) */
    uint32_t *dw = vt + 32;
    dw[0] = (1u << 0) | (1u << 1) | (1u << 6);
    dw[1] = 0xFFFF | (0x1u << 16);
    /* Vertex Array (non-packet mode — no vertex shader, fixed-function path)
     * v9 genxml Vertex Array struct (3 words / 12 bytes):
     *   word 0 bits 31:6: Pointer[25:0] (address shr 6), bit 0 = Packet (0=disabled)
     *   word 1 bits 31:0: Pointer[57:26]
     *   word 2 bits 31:16: Vertex attribute stride (bytes per vertex)
     * Packet=0: GPU reads raw vertex data directly at pos_addr.
     * NOTE: Packet=1 caused DATA_INVALID on TILER_JOB — packet mode requires
     * a live vertex shader + packet stream; without one it faults immediately. */
    {
        uint64_t V = pos_addr >> 6;
        dw[2] = (uint32_t)((V & 0x03FFFFFFu) << 6); /* Pointer bits 25:0, Packet=0 */
        dw[3] = (uint32_t)((V >> 26) & 0xFFFFFFFFu); /* Pointer bits 57:26 */
        dw[4] = (16u << 16);                          /* Vertex attribute stride = 16 (vec4) */
    }
    dw[6] = 0x00000000;  /* Min Z */
    dw[7] = 0x3F800000;  /* Max Z */
    *(uint64_t *)(dw + 10) = depth_addr;  /* Depth/stencil */
    *(uint64_t *)(dw + 12) = 1ULL | blend_addr;   /* Match Frame Shader DCD: 1ULL | blend_ptr */
    *(uint64_t *)(dw + 14) = 0;  /* Occlusion = 0 */

    /* Shader Environment (words 16-31) — NO vertex shader. */
    {
        uint32_t *se = dw + 16;
        se[0] = 0;                    /* Attribute offset = 0 */
        se[1] = 0;                    /* FAU count = 0 */
        *(uint64_t *)(se + 8) = (gva + OFF_SHADER_RESOURCES) | 1ULL; /* Resources pointer */
        *(uint64_t *)(se + 10) = 0;   /* Shader = 0 (no vertex shader) */
        *(uint64_t *)(se + 12) = 0;   /* TLS = 0 */
        *(uint64_t *)(se + 8) = (gva + OFF_SHADER_RESOURCES) | 1ULL; /* Resources pointer */
        *(uint64_t *)(se + 10) = sp_addr; /* Point to Fragment SHADER_PROGRAM so primitive descriptor retains shader VA */
        *(uint64_t *)(se + 12) = tls_addr;
        *(uint64_t *)(se + 14) = 0;   /* FAU = 0 */
        printf("triangle: TILER_JOB wired to fragment SHADER_PROGRAM at 0x%llx\n", (unsigned long long)sp_addr);
    }

    printf("triangle: TILER_JOB at gpu 0x%llx (type=7, no VS)\n", (unsigned long long)vtx_job_addr);
    dump_words("TJ full (256 bytes)", base + OFF_TRI_VTX_JOB, 256);

    /* === 6. Fragment Job (128 bytes at OFF_TRI_FRAG_JC = 0xE380) ===
     * Next pointer is ZEROED — no hardware chain. Will be submitted
     * as a separate kbase atom with dependency on the cache flush. */
    uint32_t *fj = (uint32_t *)(base + OFF_TRI_FRAG_JC);
    memset(fj, 0, 128);
    fj[4] = (1u << 0) | (9u << 1);
    fj[8] = 0;
    fj[9] = 0;  /* Match vendor capture 001_atom2_hw_jc.bin word 9 = 0 */
    *(uint64_t *)(fj + 10) = (gva + OFF_SCRATCH_MFBD) | 0x81;  /* Bit 0=1: polygon-list mode, bit 7=1: valid */
    *(uint64_t *)(fj + 12) = 0;
    fj[14] = 0;
    /* Next = 0 (already zeroed by memset) */

    printf("triangle: Fragment JC at gpu 0x%llx\n", (unsigned long long)(gva + OFF_TRI_FRAG_JC));
    dump_words("triangle Fragment JC", base + OFF_TRI_FRAG_JC, 64);
    printf("=== BUILD TRIANGLE MODE DONE ===\n\n");
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

    /* Skip asset loading for self-contained modes that don't need captured binaries */
    int needs_assets = !(strncmp(mode, "shader_fbd", 10) == 0
                         || strncmp(mode, "scratch_fbd", 11) == 0
                         || strcmp(mode, "triangle") == 0
                         || strcmp(mode, "triangle_64") == 0
                         || strcmp(mode, "triangle_256") == 0);
    if (needs_assets) {
        if (load_assets(asset_dir, cpu, gva, pages, sizeof(pages) / sizeof(pages[0])) != 0) {
            return 1;
        }
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
    if (needs_assets && read_file(atoms_path, atoms, sizeof(atoms)) != 0) {
        fprintf(stderr, "Failed to read %s\n", atoms_path);
        return 1;
    }

    atoms[0].jc = gva + OFF_SOFT0;
    atoms[1].jc = gva + OFF_COMPUTE;
    atoms[2].jc = gva + OFF_FRAG;
    atoms[3].jc = gva + OFF_SOFT3;

    int is_shader_mode = (strncmp(mode, "shader_fbd", 10) == 0);
    int is_triangle_mode = (strcmp(mode, "triangle") == 0 || strcmp(mode, "triangle_64") == 0
                            || strcmp(mode, "triangle_256") == 0);
    if (strcmp(mode, "scratch_fbd") == 0 || strcmp(mode, "scratch_fbd_64") == 0
        || strcmp(mode, "scratch_fbd_256") == 0
        || is_shader_mode || is_triangle_mode) {
        int fb_w = 16, fb_h = 16;
        if (strstr(mode, "_256")) { fb_w = 256; fb_h = 256; }
        else if (strstr(mode, "_64")) { fb_w = 64; fb_h = 64; }
        uint64_t color_off = (fb_w * fb_h * 4 > 0x1000) ? OFF_SCRATCH_COLOR_LG : OFF_SCRATCH_COLOR;
        if (is_triangle_mode) {
            build_triangle_mode(cpu, gva, fb_w, fb_h);
        } else if (is_shader_mode) {
            build_shader_fbd(cpu, gva, fb_w, fb_h, color_off);
        } else {
            build_scratch_fbd(cpu, gva, fb_w, fb_h, color_off);
        }

        if (is_triangle_mode) {

    /* Now submit the full triangle chain as 3 atoms:
     *   Atom 0: TILER_JOB (Type 7) — vertex+tiler processing
     *   Atom 1: Cache Flush (Type 3)
     *   Atom 2: Fragment (Type 9)
     *
     * Dependencies:
     *   Atom 1 depends on Atom 0 (dep_type=1 = data)
     *   Atom 2 depends on Atom 1 (dep_type=1 = data)
     *
     * Atom 0 uses core_req=0x04E — Chrome's exact captured atom config:
     *   PROTECTED_MODE_SWITCH (bit 1) | TILER (bit 2) | CS (bit 3) | COHERENT_GROUP (bit 6)
     *   = 0x02 | 0x04 | 0x08 | 0x40 = 0x4E
     *
     * Stride = sizeof(struct kbase_atom_mtk) = packed 72 bytes
     */
            #define TRI_NR_ATOMS 3
            struct kbase_atom_mtk tri_atoms[TRI_NR_ATOMS];
            memset(tri_atoms, 0, sizeof(tri_atoms));

            /* Atom 0: TILER_JOB (vertex+tiler processing) */
            tri_atoms[0].jc = gva + OFF_TRI_VTX_JOB;
            tri_atoms[0].atom_number = 0;
            tri_atoms[0].core_req = 0x04E;  /* Chrome-matching: PROTECTED | TILER | CS | COHERENT */
            /* No pre-dep — first in chain */

            /* Atom 1: Cache Flush Job */
            tri_atoms[1].jc = gva + OFF_SHADER_FLUSH_JC;
            tri_atoms[1].atom_number = 1;
            tri_atoms[1].core_req = 0x002;  /* CS slot — cache flush runs on compute cores */
            tri_atoms[1].pre_dep[0].atom_id = 0;
            tri_atoms[1].pre_dep[0].dep_type = 0;  /* No dep — sequential submission enforces ordering */

            /* Atom 2: Fragment Job */
            tri_atoms[2].jc = gva + OFF_TRI_FRAG_JC;
            tri_atoms[2].atom_number = 2;
            tri_atoms[2].core_req = 0x041;  /* BASE_JD_REQ_FS (0x01) | BASE_JD_REQ_COHERENT_GROUP (0x40) */
            tri_atoms[2].pre_dep[0].atom_id = 0;
            tri_atoms[2].pre_dep[0].dep_type = 0;  /* No dep — sequential submission enforces ordering */

            printf("triangle: submitting 3-atom chain: Vertex→Flush→Fragment\n");
            struct kbase_ioctl_job_submit sub_single = {0};
            sub_single.nr_atoms = 1;
            sub_single.stride = sizeof(struct kbase_atom_mtk);

            int ret = 0;

            /* Reinitialize TILER_JOB header words 0-7 (exception status,
             * fault pointer, type, next) that the GPU wrote back during
             * the tiler-only diagnostic. Without this, the second
             * submit sees stale exception data and faults DATA_INVALID. */
            {
                uint8_t *cpu_local = (uint8_t *)cpu;
                uint32_t *vt_local = (uint32_t *)(cpu_local + OFF_TRI_VTX_JOB);
                memset(vt_local, 0, 32);
                vt_local[4] = (1u << 0) | (7u << 1);  /* Restore Type=7, job valid */
                *(uint64_t *)(vt_local + 6) = 0;       /* Next = 0 (no hardware chain) */
            }

            /* Zero polygon list before TILER_JOB — ensures tiler writes to a clean buffer,
             * free from any stale data left by the diagnostic tiler-only run. */
            memset((uint8_t *)cpu + OFF_SCRATCH_POLYLIST, 0, 4096);

            /* Zero Polygon List Header buffer before TILER_JOB */
            memset((uint8_t *)cpu + OFF_SCRATCH_POLYLIST, 0, 4096);
            /* Submit Atom 0: TILER_JOB — generates polygon list in heap */
            sub_single.addr = (uint64_t)&tri_atoms[0];
            ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub_single);
            printf("JOB_SUBMIT (atom 0: TILER) ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
            drain_events(fd);

            /* Scan the first 512 KiB of allocation for any non-zero data written by tiler */
            {
                int count = 0;
                uint64_t scan_size = 512 * 1024;
                for (uint64_t i = 0; i < scan_size; i += 8) {
                    uint64_t val = *(uint64_t *)((uint8_t *)cpu + i);
                    /* Skip known sentinel/initial values */
                    if (val != 0 && val != 0xdeadbeefdeadbeefULL) {
                        if (count < 32) {
                            printf("alloc non-zero at +0x%05llx: 0x%016llx\n",
                                   (unsigned long long)i, (unsigned long long)val);
                        }
                        count++;
                    }
                }
                printf("alloc scan: found %d non-zero 64-bit words in first %llu KiB\n",
                       count, (unsigned long long)(scan_size / 1024));
            }
            /* Scan all 256 tile slots in OFF_SCRATCH_POLYLIST (4096 bytes) for non-zero headers */
            {
                uint64_t *hdr_table = (uint64_t *)((uint8_t *)cpu + OFF_SCRATCH_POLYLIST);
                int found_tiles = 0;
                for (int t = 0; t < 256; t++) {
                    uint64_t poly_hdr = hdr_table[t];
                    if (poly_hdr != 0) {
                        uint32_t lo = (uint32_t)(poly_hdr & 0xFFFFFFFFULL);
                        uint32_t hi = (uint32_t)(poly_hdr >> 32);
                        int tile_x = t % 16;
                        int tile_y = t / 16;
                        printf("poly list slot %d (tile %d,%d): raw 0x%016llx (lo=0x%08x hi=0x%08x)\n",
                               t, tile_x, tile_y, (unsigned long long)poly_hdr, lo, hi);
                        found_tiles++;
                    }
                }
                printf("poly list scan: found %d active tile headers in OFF_SCRATCH_POLYLIST\n", found_tiles);
            }
            /* Scan entire Tiler Heap backing buffer (256 KiB) for non-zero primitive data */
            {
                uint8_t *heap = (uint8_t *)cpu + OFF_TILER_HEAP_BACKING;
                int count = 0;
                for (int i = 0; i < TILER_HEAP_SIZE; i += 8) {
                    uint64_t val = *(uint64_t *)(heap + i);
                    if (val != 0) {
                        if (count < 16) {
                            printf("tiler heap non-zero at heap+0x%05x (raw 0x%05lx): 0x%016llx\n",
                                   i, OFF_TILER_HEAP_BACKING + i, (unsigned long long)val);
                        }
                        count++;
                    }
                }
                printf("tiler heap scan: found %d non-zero 64-bit words in 256 KiB heap\n", count);
            }

            /* Submit Atom 1: Cache Flush standalone — ensures tiler's polygon list writeback is visible to Fragment */
            sub_single.addr = (uint64_t)&tri_atoms[1];
            ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub_single);
            printf("JOB_SUBMIT (atom 1: Flush) ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
            drain_events(fd);

            /* Dump full MFBD, Frame Shader DCD, RT, tiler ctx, and heap desc */
            dump_words("MFBD before Fragment JC (128 bytes)",
                       (uint8_t *)cpu + OFF_SCRATCH_MFBD, 128);
            dump_words("Frame Shader DCD before Fragment JC (128 bytes)",
                       (uint8_t *)cpu + OFF_SHADER_DCD, 128);
            dump_words("RT descriptor before Fragment JC (64 bytes)",
                       (uint8_t *)cpu + OFF_SCRATCH_RT, 64);
            dump_words("Tiler Context before Fragment JC (192 bytes)",
                       (uint8_t *)cpu + OFF_TILER_CTX, 192);
            dump_words("Tiler Heap Desc before Fragment JC (32 bytes)",
                       (uint8_t *)cpu + OFF_TILER_HEAP_DESC, 32);

            /* Reinitialize Fragment JC header words 0-3 (exception status,
             * fault pointer) that the standalone diagnostic wrote back.
             * Also restore Type=9 and clear Next pointer so the second
             * submit doesn't see stale exception data and fault 0x58. */
            {
                uint8_t *cpu_local = (uint8_t *)cpu;
                uint32_t *frag_local = (uint32_t *)(cpu_local + OFF_TRI_FRAG_JC);
                memset(frag_local, 0, 32);  /* Zero header words 0-7 */
                frag_local[4] = (1u << 0) | (9u << 1);  /* Restore Type=9 (Fragment), job valid */
                *(uint64_t *)(frag_local + 6) = 0;       /* Next = 0 (no hardware chain) */
                /* Restore MFBD pointer at words 10-11 which was also written back.
                 * In v9, the fragment JC's MFBD pointer is at offset 0x28 (word 10).
                 * Bit 0 selects iteration mode:
                 *   0 = tile-iteration (bypasses polygon list, works with tiler=NULL)
                 *   1 = polygon-list mode (reads tiler binning output, required with tiler=active)
                 * After a successful TILER_JOB the polygon list is populated, so bit 0=1
                 * is the correct mode. Bit 0=0 caused DATA_INVALID (0x58) with tiler=active.
                 * Use env var TRI_MFBD_BIT0=0 to test tile-iteration mode for comparison. */
                int mfbd_flags = 0x01; /* Polygon List Mode (bit 0=1, bit 7=0) */
                const char *mfbd_bit0_env = getenv("TRI_MFBD_BIT0");
                if (mfbd_bit0_env) mfbd_flags = atoi(mfbd_bit0_env);
                uint64_t mfbd_ptr = (gva + OFF_SCRATCH_MFBD) | (uint64_t)mfbd_flags;
                const char *null_tiler_env = getenv("TRI_NULL_TILER");
                if (null_tiler_env && atoi(null_tiler_env)) {
                    *(uint64_t *)(cpu_local + OFF_SCRATCH_MFBD + 0x38) = 0;
                    printf("fragment reinit: FORCED MFBD+0x38 = NULL (0)\n");
                }
                /* Reset Bottom pointer in Tiler Heap Desc back to heap_base so Fragment HW reads from start of heap */
                {
                    uint32_t *th = (uint32_t *)((uint8_t *)cpu + OFF_TILER_HEAP_DESC);
                    uint64_t heap_base = gva + OFF_TILER_HEAP_BACKING;
                    const char *reset_bottom_env = getenv("TRI_RESET_HEAP_BOTTOM");
                    int reset_bottom = !reset_bottom_env || atoi(reset_bottom_env);
                    if (reset_bottom) {
                        *(uint64_t *)(th + 4) = heap_base;
                    }
                    printf("fragment reinit: heap bottom %s (0x%llx)\n",
                           reset_bottom ? "reset" : "preserved",
                           (unsigned long long)*(uint64_t *)(th + 4));
                }

                printf("fragment reinit: MFBD ptr=0x%llx flags=0x%x (%s)\n",
                       (unsigned long long)mfbd_ptr, mfbd_flags,
                       (mfbd_flags & 1) ? "polygon-list mode" : "tile-iteration mode");
                *(uint64_t *)(frag_local + 10) = mfbd_ptr;
            }

            /* Submit Atom 2: Fragment standalone */
            sub_single.addr = (uint64_t)&tri_atoms[2];
            ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub_single);
            printf("JOB_SUBMIT (atom 2: Fragment) ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
            drain_events(fd);

            /* Submit Atom 3: Post-Fragment Cache Flush — flushes GPU L2 cache to CPU system RAM */
            {
                uint32_t *fl = (uint32_t *)((uint8_t *)cpu + OFF_SHADER_FLUSH_JC);
                memset(fl, 0, 64);
                fl[4] = (3u << 1);                  /* Type = Cache flush (3) */
                fl[8]  = 0xFFFFFFFF;                /* Invalidate/Clean all core caches */
                fl[9]  = 0xFFFFFFFF;                /* Invalidate/Clean all L2 caches */
            }
            sub_single.addr = (uint64_t)&tri_atoms[1];
            ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &sub_single);
            printf("JOB_SUBMIT (atom 3: Post-Fragment Flush) ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
            drain_events(fd);

            const char *watchdog_wait = getenv("TRI_WATCHDOG_WAIT");
            if (watchdog_wait && atoi(watchdog_wait) > 0) {
                int seconds = atoi(watchdog_wait);
                printf("triangle: waiting %d seconds for late Fragment events\n", seconds);
                sleep((unsigned int)seconds);
                drain_events(fd);
            }

            /* Check color buffer for shader output */
            volatile uint32_t *color = (volatile uint32_t *)((uint8_t *)cpu + color_off);
            printf("RAW color[0]=0x%08x color[1]=0x%08x color[128]=0x%08x\n",
                   color[0], color[1], color[128]);
            int changed = 0, n_green = 0, n_red = 0, n_other = 0;
            uint32_t expected_green = 0xff00ff00;
            uint32_t expected_red   = 0xff0000ff;
            for (int i = 0; i < fb_w * fb_h; i++) {
                uint32_t v = color[i];
                if (v != 0xdeadbeef) {
                    changed++;
                    if (v == expected_green) {
                        n_green++;
                    } else if (v == expected_red) {
                        n_red++;
                    } else if (v == 0xdeadbeef) {
                    } else {
                        n_other++;
                        if (n_other <= 16) {
                            int x = i % fb_w, y = i / fb_w;
                            printf("color[%d] (%d,%d) = 0x%08x (other)\n", i, x, y, v);
                        }
                    }
                }
            }
            printf("triangle: color changed=%d / %d (%dx%d)\n", changed, fb_w * fb_h, fb_w, fb_h);
            printf("triangle: green=%d red=%d other=%d\n", n_green, n_red, n_other);
            if (changed > 0) {
                printf("triangle: first=0x%08x last=0x%08x\n", color[0], color[fb_w*fb_h-1]);
            }
            free(baseline);
            munmap(cpu, total_pages * PAGE_SIZE);
            close(fd);
            return 0;
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
