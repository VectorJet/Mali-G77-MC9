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

    size_t total_pages = 64;
    uint64_t mem[4] = { total_pages, total_pages, 0, 0x200F };
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem) < 0) { perror("MEM_ALLOC"); return 1; }
    void *cpu = mmap(NULL, total_pages * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mem[1]);
    if (cpu == MAP_FAILED) { perror("mmap"); return 1; }
    uint64_t gva = (uint64_t)cpu;
    memset(cpu, 0, total_pages * PAGE_SIZE);
    printf("mapped SAME_VA cpu/gpu = 0x%llx\n", (unsigned long long)gva);

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
