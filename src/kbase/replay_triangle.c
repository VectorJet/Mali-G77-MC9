/*
 * replay_triangle.c — Mali-G77 (Valhall v9) Triangle Replay
 *
 * Replays captured vkmark Compute+Fragment shader binaries to produce
 * visible pixel output on the Mali-G77-MC9 via /dev/mali0.
 *
 * Strategy:
 *   1. Allocate a large SAME_VA buffer
 *   2. Copy captured shader ISA, FAU, resources, thread storage into it
 *   3. Build a Compute Job (Type 4) descriptor pointing to the captured ISA
 *   4. Build a Fragment Job (Type 9) descriptor with a proper FBD
 *   5. Submit as a 2-atom batch (compute → fragment with dependency)
 *   6. Read back the color buffer and check for modified pixels
 *
 * Device: Mali-G77-MC9, MediaTek MT6893, kbase r49
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* ===== kbase r49 ioctls (magic 0x80) ===== */
#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)

/* ===== 72-byte MTK atom struct (r49 + CONFIG_MALI_MTK_GPU_BM_JM) ===== */
struct base_dependency { uint8_t atom_id; uint8_t dep_type; } __attribute__((packed));

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

/* ===== Memory layout in SAME_VA buffer ===== */
/*
 * Offset     Content                        Size
 * ------     -------                        ----
 * 0x0000     Compute Job descriptor         128 bytes (128-byte aligned)
 * 0x0080     Fragment Job descriptor         64 bytes (128-byte aligned)
 * 0x0100     Thread Local Storage desc       32 bytes (64-byte aligned)
 * 0x0140     TLS scratch memory            4096 bytes
 * 0x1140     Resources table                32 bytes (all zeros = no resources)
 * 0x1180     FAU buffer (compute)           32 bytes
 * 0x1200     Fragment Shader DCD           128 bytes
 * 0x1280     Fragment FAU buffer            32 bytes
 * 0x1300     Fragment resources             32 bytes
 * 0x1340     Fragment thread storage        32 bytes
 * 0x1380     Fragment TLS scratch         4096 bytes
 * 0x1800     Fragment FAU[0] target        512 bytes
 * 0x2000     Fragment aux cluster         4096 bytes
 * 0x3000     Shader ISA (compute/vertex)  4096 bytes
 * 0x4000     Fragment Shader ISA          4096 bytes
 * 0x8000     Fragment local arena        17 pages (0x5effe18000..0x5effe28fff)
 * 0x19000    FBD (Framebuffer Descriptor)  256 bytes (64-byte aligned)
 * 0x1a000    Color buffer (32x32 RGBA8)   4096 bytes
 * 0x1b000    Tiler heap scratch           4096 bytes
 */

#define OFF_COMPUTE_JOB  0x0000
#define OFF_FRAG_JOB     0x0080
#define OFF_TLS_DESC     0x0100
#define OFF_TLS_SCRATCH  0x0140
#define OFF_RESOURCES    0x1140
#define OFF_FAU          0x1180
#define OFF_FRAG_DCD     0x1200
#define OFF_FRAG_FAU     0x1280
#define OFF_FRAG_RES     0x1300
#define OFF_FRAG_TLS_D   0x1340
#define OFF_FRAG_TLS_S   0x1380
#define OFF_FRAG_AUX_TGT 0x1800
#define OFF_FRAG_CLUSTER 0x2000
#define OFF_SHADER_ISA   0x3000
#define OFF_FRAG_ISA     0x4000
#define OFF_FRAG_ARENA       0x8000
#define OFF_FBD              0x19000
#define OFF_COLOR_BUF        0x1a000
#define OFF_TILER_HEAP       0x1b000

#define FB_WIDTH  32
#define FB_HEIGHT 32

#define TOTAL_PAGES 28   /* 112 KB */

/* ===== Captured binary data (embedded) ===== */

/* Captured compute shader ISA from vkmark - first 256 bytes shown inline.
 * Full 4048-byte ISA is loaded from file at runtime.
 */

/* Captured FAU (32 bytes = 4 entries × 8 bytes) */
static const uint8_t captured_fau[32] = {
    0x04, 0x00, 0x00, 0x3e, 0x28, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00,
    0x68, 0x00, 0x00, 0x00, 0xff, 0xff, 0xdf, 0xff,
    0x00, 0x00, 0xe0, 0xff, 0x01, 0x01, 0x00, 0x00,
};

/* Captured thread storage descriptor (32 bytes) */
static const uint8_t captured_tls[32] = {
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Captured resources (32 bytes - all zeros, meaning no resource descriptors) */
static const uint8_t captured_resources[32] = {0};

/* Captured fragment shader DCD (128 bytes) — pointers will be patched */
static const uint8_t captured_frag_dcd[128] = {
    0x28, 0x02, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x25, 0xfc, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x28: old color ptr */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc4, 0x50, 0xe9, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x60: old resources */
    0xc0, 0x4f, 0xe9, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x68: old frag shader */
    0x40, 0x00, 0xe9, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x70: old thread stor */
    0x00, 0x51, 0xe9, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x78: old FAU */
};

/* Captured FBD (256 bytes) — pointers at 0x10 and 0x18 will be patched */
static const uint8_t captured_fbd[256] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0xfc, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x10: old TLS base */
    0x80, 0x4b, 0xe9, 0xff, 0x5e, 0x00, 0x00, 0x00, /* +0x18: old DCD ptr */
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x03, 0x00,
    /* 0x30 onwards = zeros */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 0x80: Parameters */
    0x00, 0x00, 0x00, 0x04, 0x98, 0x00, 0x88, 0x86,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void write64(void *base, size_t offset, uint64_t val) {
    *(uint64_t *)((uint8_t *)base + offset) = val;
}

static uint64_t read64(const void *base, size_t offset) {
    return *(const uint64_t *)((const uint8_t *)base + offset);
}

static int load_file(const char *path, void *dst, size_t max_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    ssize_t n = read(fd, dst, max_size);
    close(fd);
    if (n < 0) { perror("read"); return -1; }
    printf("  Loaded %s: %zd bytes\n", path, n);
    return (int)n;
}

static void relocate_pointer_range(void *cpu, size_t off, size_t size,
                                   uint64_t old_base, uint64_t old_size,
                                   uint64_t new_base) {
    for (size_t i = 0; i + 8 <= size; i += 8) {
        uint64_t v = read64(cpu, off + i);
        if (v >= old_base && v < old_base + old_size) {
            write64(cpu, off + i, new_base + (v - old_base));
        }
    }
}

static void patch_fragment_relocations(void *cpu, uint64_t gva,
                                       size_t frag_isa_off, size_t frag_fau_off,
                                       uint64_t new_fau0_target) {
    uint64_t new_frag_fau = gva + frag_fau_off;
    uint64_t new_frag_isa = gva + frag_isa_off;

    /* Known stable relocation pattern from multi-capture analysis */
    write64(cpu, frag_fau_off + 0x00, new_fau0_target);
    write64(cpu, frag_isa_off + 0x20, new_fau0_target);
    write64(cpu, frag_isa_off + 0x48, new_frag_isa + 0x20);
    write64(cpu, frag_isa_off + 0x100, new_frag_isa + 0x40);
    write64(cpu, frag_isa_off + 0x140, new_fau0_target);
    write64(cpu, frag_isa_off + 0x1d8, new_frag_fau - 0x700);
    write64(cpu, frag_isa_off + 0x1e8, new_frag_fau - 0x3ff);

    printf("  Patched fragment relocations:\n");
    printf("    FAU[0]      -> 0x%llx\n", (unsigned long long)new_fau0_target);
    printf("    ISA+0x20    -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x20));
    printf("    ISA+0x48    -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x48));
    printf("    ISA+0x100   -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x100));
    printf("    ISA+0x140   -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x140));
    printf("    ISA+0x1d8   -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x1d8));
    printf("    ISA+0x1e8   -> 0x%llx\n", (unsigned long long)read64(cpu, frag_isa_off + 0x1e8));
}

int main(int argc, char **argv) {
    printf("=== Mali-G77 Triangle Replay (vkmark shader capture) ===\n\n");

    /* ===== 1. Open device and set up context ===== */
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open /dev/mali0"); return 1; }

    uint16_t ver = 11;
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
        perror("VERSION_CHECK"); return 1;
    }
    printf("[OK] VERSION_CHECK: major=%u\n", ver);

    uint32_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags) < 0) {
        perror("SET_FLAGS"); return 1;
    }
    printf("[OK] SET_FLAGS: context created\n");

    /* ===== 2. Allocate SAME_VA GPU memory ===== */
    uint64_t mem[4] = { TOTAL_PAGES, TOTAL_PAGES, 0, 0x200F };
    /* 0x200F = CPU_RD | CPU_WR | GPU_RD | GPU_WR | SAME_VA */
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) {
        perror("MEM_ALLOC"); return 1;
    }

    void *cpu = mmap(NULL, TOTAL_PAGES * 4096, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) {
        perror("mmap"); return 1;
    }

    uint64_t gva = (uint64_t)cpu;  /* SAME_VA: CPU ptr == GPU VA */
    printf("[OK] SAME_VA buffer: CPU=%p  GVA=0x%llx  (%d pages)\n",
           cpu, (unsigned long long)gva, TOTAL_PAGES);

    memset(cpu, 0, TOTAL_PAGES * 4096);

    /* ===== 3. Load captured shader ISA from file ===== */
    const char *isa_path = (argc > 1) ? argv[1] :
        "/data/data/com.termux/files/home/mali_capture/002_atom1_compute_shader_isa.bin";

    int isa_size = load_file(isa_path, (uint8_t *)cpu + OFF_SHADER_ISA, 4096);
    if (isa_size < 0) {
        printf("WARN: Could not load ISA from %s, using embedded zeros\n", isa_path);
        isa_size = 0;
    }

    /* Load the FRAGMENT shader ISA (separate binary!) */
    const char *frag_isa_path = (argc > 2) ? argv[2] :
        "/data/data/com.termux/files/home/mali_capture/002_atom2_frag_shader_isa.bin";
    int frag_isa_size = load_file(frag_isa_path, (uint8_t *)cpu + OFF_FRAG_ISA, 4096);
    if (frag_isa_size < 0) {
        printf("WARN: Could not load fragment ISA from %s\n", frag_isa_path);
        frag_isa_size = 0;
    }

    const char *frag_cluster_path = (argc > 3) ? argv[3] :
        "/data/data/com.termux/files/home/mali_capture/002_atom2_frag_aux_window.bin";
    const char *frag_fau0_target_path = (argc > 4) ? argv[4] :
        "/data/data/com.termux/files/home/mali_capture/002_atom2_frag_fau0_target.bin";
    int frag_cluster_size = load_file(frag_cluster_path, (uint8_t *)cpu + OFF_FRAG_CLUSTER, 4096);
    int frag_fau0_target_size = load_file(frag_fau0_target_path, (uint8_t *)cpu + OFF_FRAG_AUX_TGT, 512);
    int use_frag_cluster = (frag_cluster_size == 4096 && frag_fau0_target_size > 0);

    const uint64_t old_arena_base = 0x5effe18000ULL;
    const int arena_pages = 17;
    int loaded_arena_pages = 0;
    for (int i = 0; i < arena_pages; i++) {
        char path[256];
        uint64_t old_page = old_arena_base + (uint64_t)i * 0x1000;
        snprintf(path, sizeof(path),
                 "/data/data/com.termux/files/home/mali_capture/002_atom2_frag_arena_page_%llx.bin",
                 (unsigned long long)old_page);
        int n = load_file(path, (uint8_t *)cpu + OFF_FRAG_ARENA + i * 0x1000, 4096);
        if (n == 4096) loaded_arena_pages++;
    }
    int use_frag_arena = (loaded_arena_pages == arena_pages);

    /* ===== 4. Copy captured descriptors into buffer ===== */
    /* Thread Local Storage descriptor */
    memcpy((uint8_t *)cpu + OFF_TLS_DESC, captured_tls, 32);

    /* Patch TLS base pointer (offset 0x08 in TLS desc) to point to our scratch */
    write64(cpu, OFF_TLS_DESC + 0x08, gva + OFF_TLS_SCRATCH);
    printf("  TLS desc at 0x%llx, scratch at 0x%llx\n",
           (unsigned long long)(gva + OFF_TLS_DESC),
           (unsigned long long)(gva + OFF_TLS_SCRATCH));

    /* Resources table (all zeros = empty) */
    memcpy((uint8_t *)cpu + OFF_RESOURCES, captured_resources, 32);

    /* FAU buffer */
    memcpy((uint8_t *)cpu + OFF_FAU, captured_fau, 32);

    /* Fragment TLS descriptor (copy from compute TLS, patch base) */
    memcpy((uint8_t *)cpu + OFF_FRAG_TLS_D, captured_tls, 32);
    write64(cpu, OFF_FRAG_TLS_D + 0x08, gva + OFF_FRAG_TLS_S);

    if (use_frag_cluster) {
        uint64_t fau0_target_gva = gva + OFF_FRAG_AUX_TGT;
        if (use_frag_arena) {
            for (int i = 0; i < arena_pages; i++) {
                uint64_t old_page = old_arena_base + (uint64_t)i * 0x1000;
                relocate_pointer_range(cpu, OFF_FRAG_ARENA, arena_pages * 0x1000,
                                       old_page, 0x1000,
                                       gva + OFF_FRAG_ARENA + i * 0x1000);
            }
            fau0_target_gva = gva + OFF_FRAG_ARENA + (0x5effe1b940ULL - old_arena_base);
            printf("  Fragment arena loaded at 0x%llx..0x%llx (%d pages)\n",
                   (unsigned long long)(gva + OFF_FRAG_ARENA),
                   (unsigned long long)(gva + OFF_FRAG_ARENA + arena_pages * 0x1000 - 1),
                   arena_pages);
            printf("  Fragment FAU0 target=0x%llx\n",
                   (unsigned long long)fau0_target_gva);
        }

        const size_t CL_DCD = OFF_FRAG_CLUSTER + 0x280;
        const size_t CL_ISA = OFF_FRAG_CLUSTER + 0x6c0;
        const size_t CL_FAU = OFF_FRAG_CLUSTER + 0x800;
        const size_t CL_RES = OFF_FRAG_CLUSTER + 0x7c4;

        memcpy((uint8_t *)cpu + OFF_FRAG_DCD, (uint8_t *)cpu + CL_DCD, 128);
        write64(cpu, OFF_FRAG_DCD + 0x28, gva + OFF_COLOR_BUF);
        write64(cpu, OFF_FRAG_DCD + 0x60, gva + CL_RES);
        write64(cpu, OFF_FRAG_DCD + 0x68, gva + CL_ISA);
        write64(cpu, OFF_FRAG_DCD + 0x70, gva + OFF_FRAG_TLS_D);
        write64(cpu, OFF_FRAG_DCD + 0x78, gva + CL_FAU);

        patch_fragment_relocations(cpu, gva, CL_ISA, CL_FAU, fau0_target_gva);

        printf("  Fragment cluster loaded at 0x%llx\n",
               (unsigned long long)(gva + OFF_FRAG_CLUSTER));
        printf("    cluster DCD -> 0x%llx (copied to canonical DCD)\n",
               (unsigned long long)(gva + CL_DCD));
        printf("    cluster ISA -> 0x%llx\n",
               (unsigned long long)(gva + CL_ISA));
        printf("    cluster FAU -> 0x%llx\n",
               (unsigned long long)(gva + CL_FAU));
        printf("    cluster RES -> 0x%llx\n",
               (unsigned long long)(gva + CL_RES));
    } else {
        /* Fragment shader DCD — copy and patch all pointers */
        memcpy((uint8_t *)cpu + OFF_FRAG_DCD, captured_frag_dcd, 128);
        write64(cpu, OFF_FRAG_DCD + 0x28, gva + OFF_COLOR_BUF);
        write64(cpu, OFF_FRAG_DCD + 0x60, gva + OFF_FRAG_RES);
        write64(cpu, OFF_FRAG_DCD + 0x68, gva + OFF_FRAG_ISA);
        write64(cpu, OFF_FRAG_DCD + 0x70, gva + OFF_FRAG_TLS_D);
        write64(cpu, OFF_FRAG_DCD + 0x78, gva + OFF_FRAG_FAU);

        printf("  Fragment DCD at 0x%llx (patched 5 pointers)\n",
               (unsigned long long)(gva + OFF_FRAG_DCD));

        load_file("/data/data/com.termux/files/home/mali_capture/002_atom2_frag_resources.bin",
                  (uint8_t *)cpu + OFF_FRAG_RES, 64);
        load_file("/data/data/com.termux/files/home/mali_capture/002_atom2_frag_fau.bin",
                  (uint8_t *)cpu + OFF_FRAG_FAU, 32);

        if (frag_isa_size >= 0x1f0) {
            patch_fragment_relocations(cpu, gva, OFF_FRAG_ISA, OFF_FRAG_FAU,
                                       gva + OFF_FRAG_AUX_TGT);
        } else {
            printf("  WARN: Fragment ISA too small for relocation patching (%d bytes)\n",
                   frag_isa_size);
        }
    }

    /* ===== 5. Build FBD (Framebuffer Descriptor) ===== */
    memcpy((uint8_t *)cpu + OFF_FBD, captured_fbd, 256);

    /* Patch FBD Local Storage section pointers */
    /* FBD+0x10: TLS base for fragment → our fragment TLS scratch */
    write64(cpu, OFF_FBD + 0x10, gva + OFF_FRAG_TLS_S);
    /* FBD+0x18: DCD pointer → our patched fragment DCD */
    write64(cpu, OFF_FBD + 0x18, gva + OFF_FRAG_DCD);

    printf("  FBD at 0x%llx (patched TLS and DCD pointers)\n",
           (unsigned long long)(gva + OFF_FBD));

    /* ===== 6. Initialize color buffer with sentinel ===== */
    volatile uint32_t *color = (volatile uint32_t *)((uint8_t *)cpu + OFF_COLOR_BUF);
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        color[i] = 0xDEADBEEF;
    }
    printf("  Color buffer at 0x%llx (%dx%d, init=0xDEADBEEF)\n",
           (unsigned long long)(gva + OFF_COLOR_BUF), FB_WIDTH, FB_HEIGHT);

    /* ===== 7. Build Compute Job descriptor (128 bytes at OFF_COMPUTE_JOB) ===== */
    uint32_t *cjob = (uint32_t *)((uint8_t *)cpu + OFF_COMPUTE_JOB);

    /* Job Header (32 bytes) */
    cjob[0] = 0;                          /* exception_status */
    cjob[1] = 0;                          /* first_incomplete_task */
    cjob[2] = 0; cjob[3] = 0;            /* fault_pointer */
    cjob[4] = (4 << 1) | (1 << 16);      /* Type=4 (Compute), Index=1 */
    cjob[5] = 0;                          /* dependencies */
    cjob[6] = 0; cjob[7] = 0;            /* next_job = 0 (end of chain) */

    /* Compute Payload (96 bytes starting at offset 0x20) */
    /* Workgroup parameters */
    cjob[8]  = 0x80000000;   /* size_x=0,y=0,z=0 (1×1×1), allow_merging=1 */
    cjob[9]  = 0x00008100;   /* task_increment=256, task_axis=Z(2) */
    cjob[10] = 1;            /* workgroup_count_x = 1 */
    cjob[11] = 1;            /* workgroup_count_y = 1 */
    cjob[12] = 1;            /* workgroup_count_z = 1 */
    cjob[13] = 0;            /* offset_x = 0 */
    cjob[14] = 0;            /* offset_y = 0 */
    cjob[15] = 0;            /* offset_z = 0 */

    /* Shader Environment (64 bytes at job offset 0x40) */
    cjob[16] = 0;            /* attribute_offset = 0 */
    cjob[17] = 4;            /* fau_count = 4 (4 entries × 8 bytes = 32 bytes) */
    /* SE words 2-7: reserved */
    cjob[18] = 0; cjob[19] = 0;
    cjob[20] = 0; cjob[21] = 0;
    cjob[22] = 0; cjob[23] = 0;
    /* SE word 8-9: Resources pointer */
    write64(cjob, 16*4, gva + OFF_RESOURCES);   /* 0x60: Resources */
    /* SE word 10-11: Shader ISA pointer */
    write64(cjob, 18*4, gva + OFF_SHADER_ISA);  /* 0x68: Shader ISA */
    /* SE word 12-13: Thread Storage pointer */
    write64(cjob, 20*4, gva + OFF_TLS_DESC);    /* 0x70: Thread Storage */
    /* SE word 14-15: FAU pointer */
    write64(cjob, 22*4, gva + OFF_FAU);         /* 0x78: FAU */

    printf("\n  Compute Job at 0x%llx:\n", (unsigned long long)(gva + OFF_COMPUTE_JOB));
    printf("    Shader ISA  → 0x%llx (%d bytes)\n",
           (unsigned long long)(gva + OFF_SHADER_ISA), isa_size);
    printf("    Resources   → 0x%llx\n", (unsigned long long)(gva + OFF_RESOURCES));
    printf("    TLS desc    → 0x%llx\n", (unsigned long long)(gva + OFF_TLS_DESC));
    printf("    FAU         → 0x%llx\n", (unsigned long long)(gva + OFF_FAU));

    /* ===== 8. Build Fragment Job descriptor (64 bytes at OFF_FRAG_JOB) ===== */
    uint32_t *fjob = (uint32_t *)((uint8_t *)cpu + OFF_FRAG_JOB);

    /* Job Header (32 bytes) */
    fjob[0] = 0;                          /* exception_status */
    fjob[1] = 0;                          /* first_incomplete_task */
    fjob[2] = 0; fjob[3] = 0;            /* fault_pointer */
    fjob[4] = (9 << 1) | (1 << 16);      /* Type=9 (Fragment), Index=1 */
    fjob[5] = 0;                          /* dependencies (atom-level deps used) */
    fjob[6] = 0; fjob[7] = 0;            /* next_job = 0 */

    /* Fragment Payload (32 bytes starting at offset 0x20) */
    fjob[8]  = 0;                         /* Bound Min X=0, Y=0 */
    fjob[9]  = (FB_WIDTH - 1) | ((FB_HEIGHT - 1) << 16);  /* Bound Max */
    /* FBD pointer with type tag in lower bits */
    uint64_t fbd_ptr = gva + OFF_FBD;
    /* The lower 6 bits encode the FBD type. Captured value had 0x01 tag.
     * Bit 0 = MFBD flag (1 = multi-target FBD format) */
    fbd_ptr |= 0x01;   /* MFBD flag */
    write64(fjob, 10*4, fbd_ptr);         /* 0x28: FBD pointer */
    fjob[12] = 0; fjob[13] = 0;          /* tile_enable_map = 0 */
    fjob[14] = 0;                         /* tile_enable_map_stride */
    fjob[15] = 0;                         /* padding */

    printf("\n  Fragment Job at 0x%llx:\n", (unsigned long long)(gva + OFF_FRAG_JOB));
    printf("    FBD         → 0x%llx (tagged: 0x%llx)\n",
           (unsigned long long)(gva + OFF_FBD), (unsigned long long)fbd_ptr);
    printf("    Bounds      → (%d,%d) to (%d,%d)\n", 0, 0, FB_WIDTH-1, FB_HEIGHT-1);

    /* ===== 9. Submit as 2-atom batch ===== */
    printf("\n--- Submitting 2-atom batch (Compute → Fragment) ---\n");

    struct kbase_atom_mtk atoms[2];
    memset(atoms, 0, sizeof(atoms));

    /* Atom 1: Compute (jobslot 1) */
    atoms[0].jc = gva + OFF_COMPUTE_JOB;
    atoms[0].core_req = 0x4a;     /* CS + CF + COHERENT_GROUP (Chrome pattern) */
    atoms[0].atom_number = 1;
    atoms[0].jobslot = 1;
    atoms[0].frame_nr = 1;

    /* Atom 2: Fragment (jobslot 0, depends on Atom 1) */
    atoms[1].jc = gva + OFF_FRAG_JOB;
    atoms[1].core_req = 0x49;     /* FS + CF + COHERENT_GROUP */
    atoms[1].atom_number = 2;
    atoms[1].jobslot = 0;
    atoms[1].pre_dep[0].atom_id = 1;
    atoms[1].pre_dep[0].dep_type = 1;  /* DATA dependency */
    atoms[1].frame_nr = 1;

    struct kbase_ioctl_job_submit submit = {
        .addr = (uint64_t)atoms,
        .nr_atoms = 2,
        .stride = 72,
    };

    int ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
    printf("  JOB_SUBMIT ret=%d\n", ret);
    if (ret < 0) {
        perror("JOB_SUBMIT");
    }

    /* Wait for completion */
    usleep(500000);  /* 500ms */

    /* Read completion events */
    uint8_t event_buf[48];
    for (int i = 0; i < 2; i++) {
        ssize_t n = read(fd, event_buf, 24);
        if (n > 0) {
            uint32_t event_code = *(uint32_t *)event_buf;
            uint32_t atom_nr = *(uint32_t *)(event_buf + 4);
            printf("  Event: atom=%u code=0x%x (%s)\n", atom_nr, event_code,
                   event_code == 1 ? "SUCCESS" : "FAIL");
        }
    }

    /* ===== 10. Check results ===== */
    printf("\n=== RESULTS ===\n");

    /* Check if any compute job fields were modified by GPU */
    printf("Compute job exception_status: 0x%08x\n", cjob[0]);
    printf("Fragment job exception_status: 0x%08x\n", fjob[0]);

    int changed = 0;
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        if (color[i] != 0xDEADBEEF) changed++;
    }
    printf("\nColor buffer: %d / %d pixels changed\n", changed, FB_WIDTH * FB_HEIGHT);

    /* Print first 4 rows */
    printf("\nFirst 4 rows of color buffer:\n");
    for (int y = 0; y < 4 && y < FB_HEIGHT; y++) {
        printf("  Row %2d: ", y);
        for (int x = 0; x < 8 && x < FB_WIDTH; x++) {
            printf("%08x ", color[y * FB_WIDTH + x]);
        }
        printf("...\n");
    }

    /* Print unique values */
    printf("\nUnique pixel values (first 16):\n");
    uint32_t unique[16];
    int n_unique = 0;
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT && n_unique < 16; i++) {
        uint32_t v = color[i];
        int found = 0;
        for (int j = 0; j < n_unique; j++) {
            if (unique[j] == v) { found = 1; break; }
        }
        if (!found) {
            unique[n_unique++] = v;
            printf("  0x%08x (first at pixel %d)\n", v, i);
        }
    }

    if (changed > 0) {
        printf("\n*** SUCCESS! GPU modified %d pixels! ***\n", changed);
    } else {
        printf("\nNo pixels changed. Trying standalone fragment-only...\n");

        /* ===== Fallback: Submit fragment job alone ===== */
        printf("\n--- Fallback: Fragment-only submission ---\n");

        /* Re-init color buffer */
        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
            color[i] = 0xDEADBEEF;
        }

        struct kbase_atom_mtk frag_atom;
        memset(&frag_atom, 0, sizeof(frag_atom));
        frag_atom.jc = gva + OFF_FRAG_JOB;
        frag_atom.core_req = 0x003;  /* FS + CS (proven working pattern) */
        frag_atom.atom_number = 1;
        frag_atom.jobslot = 0;
        frag_atom.frame_nr = 1;

        struct kbase_ioctl_job_submit submit2 = {
            .addr = (uint64_t)&frag_atom,
            .nr_atoms = 1,
            .stride = 72,
        };

        ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit2);
        printf("  Fragment-only JOB_SUBMIT ret=%d\n", ret);
        usleep(500000);
        read(fd, event_buf, 24);

        uint32_t ev_code = *(uint32_t *)event_buf;
        uint32_t ev_atom = *(uint32_t *)(event_buf + 4);
        printf("  Event: atom=%u code=0x%x\n", ev_atom, ev_code);
        printf("  Fragment exception_status: 0x%08x\n", fjob[0]);

        changed = 0;
        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
            if (color[i] != 0xDEADBEEF) changed++;
        }
        printf("  Color buffer: %d / %d pixels changed\n", changed, FB_WIDTH * FB_HEIGHT);

        if (changed > 0) {
            printf("\n*** FRAGMENT-ONLY SUCCESS! %d pixels modified! ***\n", changed);
            printf("First 4 rows:\n");
            for (int y = 0; y < 4; y++) {
                printf("  Row %2d: ", y);
                for (int x = 0; x < 8; x++) {
                    printf("%08x ", color[y * FB_WIDTH + x]);
                }
                printf("...\n");
            }
        }
    }

    munmap(cpu, TOTAL_PAGES * 4096);
    close(fd);
    return 0;
}
