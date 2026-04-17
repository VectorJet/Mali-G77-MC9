#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <android/log.h>

#define LOG_TAG "mali_ioctl_spy"

/* Output directory for binary captures on device */
#define CAPTURE_DIR "/data/local/tmp/mali_capture"

static int capture_seq = 0;  /* incremented per JOB_SUBMIT capture */

#define KBASE_IOCTL_TYPE 0x80
#define KBASE_IOCTL_JOB_SUBMIT_NR 0x02

struct kbase_ioctl_job_submit {
    uint64_t addr;
    uint32_t nr_atoms;
    uint32_t stride;
};

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

static int (*real_ioctl)(int fd, int request, ...) = NULL;
static int spy_enabled = 1;
static int dump_count = 0;

static void spy_log_impl(const char *fmt, ...) {
    va_list ap_log;
    va_start(ap_log, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ap_log);
    va_end(ap_log);

    va_list ap_stderr;
    va_start(ap_stderr, fmt);
    fprintf(stderr, "[mali_ioctl_spy] ");
    vfprintf(stderr, fmt, ap_stderr);
    fprintf(stderr, "\n");
    va_end(ap_stderr);
}

#define spy_log(...) spy_log_impl(__VA_ARGS__)

static void dump_memory(uint64_t addr, size_t size, const char *prefix) {
    const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
    char line[100];
    for (size_t i = 0; i < size; i += 16) {
        int len = 0;
        len += snprintf(line + len, sizeof(line) - len, "%s %04zx: ", prefix, i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                len += snprintf(line + len, sizeof(line) - len, "%02x ", p[i + j]);
            } else {
                len += snprintf(line + len, sizeof(line) - len, "   ");
            }
        }
        spy_log("%s", line);
    }
}

/* Write a binary capture to a file in CAPTURE_DIR */
static void write_capture_file(const char *name, const void *data, size_t size) {
    static int dir_created = 0;
    if (!dir_created) {
        mkdir(CAPTURE_DIR, 0777);
        dir_created = 1;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/%03d_%s.bin", CAPTURE_DIR, capture_seq, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, data, size);
        close(fd);
        spy_log("[SPY_CAPTURE] Wrote %zu bytes to %s", size, path);
    } else {
        spy_log("[SPY_CAPTURE] FAILED to write %s: %s", path, strerror(errno));
    }
}

/* Try to determine shader ISA size by scanning for trailing zeros.
 * Valhall shaders are typically small (128-4096 bytes) and padded with zeros.
 * We scan up to max_size bytes to find the last non-zero 8-byte word.
 * Returns the size rounded up to 8-byte alignment.
 */
static size_t estimate_shader_size(const uint8_t *isa, size_t max_size) {
    size_t last_nonzero = 0;
    for (size_t i = 0; i + 8 <= max_size; i += 8) {
        uint64_t word;
        memcpy(&word, isa + i, 8);
        if (word != 0) last_nonzero = i + 8;
    }
    if (last_nonzero < 64) last_nonzero = 64;
    return last_nonzero;
}

/* SIGSEGV-safe memory probe: check if we can read 'size' bytes at 'addr'.
 * Uses mincore() to test page residency. Returns 1 if likely readable, 0 if not.
 * This is imperfect but catches most unmapped cases without crashing.
 */
static int probe_readable(uint64_t addr, size_t size) {
    /* Check page alignment and use mincore */
    uintptr_t page_start = addr & ~(4096ULL - 1);
    size_t page_end = ((addr + size + 4095) & ~(4095));
    size_t num_pages = (page_end - page_start) / 4096;
    unsigned char *vec = alloca(num_pages);
    if (!vec) return 0;
    /* mincore returns 0 if pages are resident, -1 with ENOMEM if unmapped */
    if (mincore((void *)page_start, page_end - page_start, vec) != 0) {
        return 0;  /* Pages not mapped — don't risk reading */
    }
    return 1;
}

/* Safe memory dump: only dump if probe_readable succeeds */
static int safe_dump_memory(uint64_t addr, size_t size, const char *prefix) {
    if (!probe_readable(addr, size)) {
        spy_log("%s UNREADABLE: VA 0x%llx (%zu bytes) — skipping", prefix, addr, size);
        return 0;
    }
    dump_memory(addr, size, prefix);
    return 1;
}

/* Safe capture: only write if probe_readable succeeds */
static int safe_write_capture(const char *name, uint64_t va, size_t size) {
    if (!probe_readable(va, size)) {
        spy_log("[SPY_CAPTURE] UNREADABLE: %s at VA 0x%llx (%zu bytes) — skipping", name, va, size);
        return 0;
    }
    write_capture_file(name, (const void *)(uintptr_t)va, size);
    return 1;
}

static int is_candidate_pointer(uint64_t ptr) {
    return ptr >= 0x10000ULL && ptr < 0x800000000000ULL;
}

static void dump_atom_meta(const struct kbase_atom_mtk *atom, int atom_idx) {
    spy_log("[SPY_ATOM] atom[%d]: seq=0x%llx jc=0x%llx atom_number=%u core_req=0x%x prio=%u device=%u jobslot=%u frame=%u renderpass=%u",
            atom_idx,
            (unsigned long long)atom->seq_nr,
            (unsigned long long)atom->jc,
            atom->atom_number,
            atom->core_req,
            atom->prio,
            atom->device_nr,
            atom->jobslot,
            atom->frame_nr,
            atom->renderpass_id);
    spy_log("[SPY_ATOM] atom[%d]: pre_dep0=(atom=%u,type=%u) pre_dep1=(atom=%u,type=%u) nr_extres=%u extres=0x%llx udata=[0x%llx,0x%llx]",
            atom_idx,
            atom->pre_dep[0].atom_id, atom->pre_dep[0].dep_type,
            atom->pre_dep[1].atom_id, atom->pre_dep[1].dep_type,
            atom->nr_extres,
            (unsigned long long)atom->extres_list,
            (unsigned long long)atom->udata[0],
            (unsigned long long)atom->udata[1]);
}

static void capture_pointer_closure(const char *prefix, int atom_idx,
                                    const uint64_t *words, int nr_words,
                                    int max_unique_pages) {
    uint64_t pages[32];
    int n_pages = 0;

    for (int i = 0; i < nr_words && n_pages < max_unique_pages; i++) {
        uint64_t ptr = words[i];
        uint64_t page = ptr & ~0xFFFULL;
        if (!is_candidate_pointer(ptr) || !probe_readable(page, 256)) continue;

        int seen = 0;
        for (int j = 0; j < n_pages; j++) {
            if (pages[j] == page) { seen = 1; break; }
        }
        if (seen) continue;

        pages[n_pages++] = page;

        char fn_page[96];
        char fn_pagehex[96];
        char fn_head[96];
        snprintf(fn_page, sizeof(fn_page), "atom%d_%s_page%d", atom_idx, prefix, n_pages - 1);
        snprintf(fn_pagehex, sizeof(fn_pagehex), "atom%d_%s_page_%llx", atom_idx, prefix,
                 (unsigned long long)page);
        snprintf(fn_head, sizeof(fn_head), "atom%d_%s_ptr%d", atom_idx, prefix, i);
        safe_write_capture(fn_page, page, 4096);
        safe_write_capture(fn_pagehex, page, 4096);
        safe_write_capture(fn_head, ptr & ~0x3FULL, 256);
        spy_log("[SPY_CAPTURE] %s word[%d] -> ptr=0x%llx page=0x%llx",
                prefix, i, ptr, page);
    }
}

/* Read a 64-bit address from a 32-bit word array at the given word index */
static uint64_t read_addr64(const uint32_t *words, int word_idx) {
    return ((uint64_t)words[word_idx + 1] << 32) | words[word_idx];
}

static void analyze_and_dump_job(const struct kbase_atom_mtk *atom, int atom_idx) {
    uint32_t req = atom->core_req;
    int is_soft_job = (req & (1 << 9)); // BASE_JD_REQ_SOFT_JOB

    if (is_soft_job) {
        spy_log("[SPY_DUMP] SOFT ATOM %d: jc=0x%llx core_req=0x%x jobslot=%d",
                atom_idx, atom->jc, atom->core_req, atom->jobslot);
        if (atom->jc && probe_readable(atom->jc, 64)) {
            safe_dump_memory(atom->jc, 64, "[SPY_SOFT_JC]");
            char name[64];
            snprintf(name, sizeof(name), "atom%d_soft_jc", atom_idx);
            safe_write_capture(name, atom->jc, 64);
        }
        return;
    }

    if (atom->jc == 0) return;

    spy_log("[SPY_DUMP] HW ATOM %d: jc=0x%llx core_req=0x%x jobslot=%d", 
            atom_idx, atom->jc, atom->core_req, atom->jobslot);

    // jc is a CPU pointer in Chrome's address space because Chrome uses SAME_VA
    // Read the Job Header (32 bytes)
    const uint32_t *header = (const uint32_t *)(uintptr_t)atom->jc;
    uint32_t ctrl_word = header[4]; // offset 0x10
    uint8_t job_type = (ctrl_word >> 1) & 0x7F;
    
    spy_log("[SPY_DUMP]   -> Job Type = %u (Control Word = 0x%08x)", job_type, ctrl_word);

    // Dump based on job type size
    size_t dump_size = 0;
    if (job_type == 11) dump_size = 384;      // Malloc Vertex
    else if (job_type == 7) dump_size = 256;  // Tiler
    else if (job_type == 9) dump_size = 64;   // Fragment
    else if (job_type == 2) dump_size = 64;   // Write Value
    else if (job_type == 4) dump_size = 128;  // Compute
    else dump_size = 128;

    dump_memory(atom->jc, dump_size, "[SPY_DUMP_JC]");
    {
        char name[64];
        snprintf(name, sizeof(name), "atom%d_hw_jc", atom_idx);
        safe_write_capture(name, atom->jc, dump_size);
    }

    // ===================== COMPUTE JOB (Type 4) =====================
    // Follow the Shader Environment pointers to capture:
    //   Shader ISA, FAU, Resources, Thread Storage
    if (job_type == 4) {
        // Compute Payload starts at job offset 0x20 = header word 8
        // Shader Environment is inline starting at payload word 8 = header word 16 = job offset 0x40
        //
        // SE fields at job offsets 0x60, 0x68, 0x70, 0x78 correspond to:
        //   job 0x60 = header word 24 = payload word 16  (Resources)
        //   job 0x68 = header word 26 = payload word 18  (Shader ISA)
        //   job 0x70 = header word 28 = payload word 20  (Thread Storage)
        //   job 0x78 = header word 30 = payload word 22  (FAU)
        //
        // FAU count is at SE word 1 = payload word 9 = header word 17 = job offset 0x44
        //
        const uint32_t *payload = header + 8;  // offset 0x20
        uint32_t fau_count = (payload[9] & 0xFF);  // SE word 1 = payload word 9 = job offset 0x44
        
        // Read the 4 GPU VA pointers from the Shader Environment
        uint64_t resources_va   = read_addr64(payload, 16);  // job 0x60
        uint64_t shader_va      = read_addr64(payload, 18);  // job 0x68
        uint64_t threadstor_va  = read_addr64(payload, 20);  // job 0x70
        uint64_t fau_va         = read_addr64(payload, 22);  // job 0x78

        spy_log("[SPY_DUMP]   -> Compute Shader Env: fau_count=%u", fau_count);
        spy_log("[SPY_DUMP]   -> Resources   VA = 0x%llx", resources_va);
        spy_log("[SPY_DUMP]   -> Shader ISA  VA = 0x%llx", shader_va);
        spy_log("[SPY_DUMP]   -> Thread Stor  VA = 0x%llx", threadstor_va);
        spy_log("[SPY_DUMP]   -> FAU         VA = 0x%llx", fau_va);

        // --- Capture Shader ISA binary ---
        // SAME_VA: GPU VA == CPU VA, so we can read it directly
        // First probe a small amount, then estimate real size from that
        if (shader_va) {
            if (safe_dump_memory(shader_va, 256, "[SPY_ISA_PROBE]")) {
                const uint8_t *isa = (const uint8_t *)(uintptr_t)shader_va;
                // Estimate actual size from the first 4KB (if readable)
                size_t isa_size = 256;  // at minimum
                if (probe_readable(shader_va, 4096)) {
                    isa_size = estimate_shader_size(isa, 4096);
                }
                spy_log("[SPY_DUMP]   -> Shader ISA estimated %zu bytes", isa_size);
                // Dump the full estimated size
                safe_dump_memory(shader_va, isa_size, "[SPY_ISA]");
                // Write raw ISA to binary file
                char name[64];
                snprintf(name, sizeof(name), "atom%d_compute_shader_isa", atom_idx);
                safe_write_capture(name, shader_va, isa_size);
            }
        }

        // --- Capture FAU (Fast Access Uniforms) ---
        // fau_count entries × 8 bytes each
        if (fau_va && fau_count > 0) {
            size_t fau_size = fau_count * 8;
            spy_log("[SPY_DUMP]   -> FAU buffer: %u entries = %zu bytes", fau_count, fau_size);
            safe_dump_memory(fau_va, fau_size, "[SPY_FAU]");
            char name[64];
            snprintf(name, sizeof(name), "atom%d_compute_fau", atom_idx);
            safe_write_capture(name, fau_va, fau_size);
        }

        // --- Capture Thread Storage (Local Storage descriptor) ---
        // Local Storage struct = 8 words = 32 bytes
        if (threadstor_va) {
            spy_log("[SPY_DUMP]   -> Thread Storage / Local Storage at 0x%llx", threadstor_va);
            safe_dump_memory(threadstor_va, 32, "[SPY_TLS]");
            char name[64];
            snprintf(name, sizeof(name), "atom%d_compute_thread_storage", atom_idx);
            safe_write_capture(name, threadstor_va, 32);
        }

        // --- Capture Resources table ---
        // Walk the resource table until we hit a null/zero descriptor.
        // Each resource entry is 32 bytes. We dump up to 512 bytes max.
        if (resources_va) {
            // First probe if the resources table is readable at all
            if (probe_readable(resources_va, 64)) {
                const uint32_t *res = (const uint32_t *)(uintptr_t)resources_va;
                size_t res_total = 0;
                /* Walk descriptors: each is 32 bytes, stop at zero header or after 16 entries */
                for (int i = 0; i < 16 && res_total < 512; i++) {
                    /* Check if next descriptor page is readable */
                    if (!probe_readable(resources_va + res_total, 32)) break;
                    uint32_t desc_header = res[i * 8];  // first word of each 32-byte descriptor
                    if (desc_header == 0 && res_total > 0) break;  // null terminator
                    res_total += 32;
                }
                if (res_total == 0) res_total = 64;  // dump at least 64 bytes
                spy_log("[SPY_DUMP]   -> Resources table: %zu bytes", res_total);
                safe_dump_memory(resources_va, res_total, "[SPY_RES]");
                char name[64];
                snprintf(name, sizeof(name), "atom%d_compute_resources", atom_idx);
                safe_write_capture(name, resources_va, res_total);
            }
        }
    }

    // ===================== FRAGMENT JOB (Type 9) =====================
    if (job_type == 9) {
        uint64_t fbd_ptr = read_addr64(header, 10);  // words 10-11 at job offset 0x28
        // FBD pointer has type tag in lower bits; mask them off (aligned to 64 bytes)
        uint64_t actual_fbd = (fbd_ptr >> 6) << 6;
        if (actual_fbd) {
            spy_log("[SPY_DUMP]   -> Found Framebuffer Descriptor at 0x%llx", actual_fbd);
            safe_dump_memory(actual_fbd, 256, "[SPY_DUMP_FBD]");
            char name[64];
            snprintf(name, sizeof(name), "atom%d_frag_fbd", atom_idx);
            safe_write_capture(name, actual_fbd, 256);

            // Also try to follow the DCD pointer in the FBD
            if (probe_readable(actual_fbd, 64)) {
                const uint32_t *fbd = (const uint32_t *)(uintptr_t)actual_fbd;
                // FBD + 0x18: Frame Shader DCD pointer (in some FBD formats)
                uint64_t dcd_ptr = read_addr64(fbd, 6);   // offset 0x18
                if (dcd_ptr) {
                    uint64_t actual_dcd = (dcd_ptr >> 6) << 6;  // mask type bits
                    if (actual_dcd) {
                        spy_log("[SPY_DUMP]   -> Found Fragment Shader DCD at 0x%llx", actual_dcd);
                        safe_dump_memory(actual_dcd, 128, "[SPY_FRAG_DCD]");
                        char name2[64];
                        snprintf(name2, sizeof(name2), "atom%d_frag_shader_dcd", atom_idx);
                        safe_write_capture(name2, actual_dcd, 128);
                        if (probe_readable(actual_dcd & ~0xFFFULL, 4096)) {
                            const uint64_t *dcd_page = (const uint64_t *)(uintptr_t)(actual_dcd & ~0xFFFULL);
                            capture_pointer_closure("frag_dcd_page", atom_idx, dcd_page, 512, 8);
                        }

                        // Follow DCD Shader Environment pointers (same layout as Compute SE)
                        // DCD+0x60: Resources, DCD+0x68: Shader ISA, DCD+0x70: TLS, DCD+0x78: FAU
                        if (probe_readable(actual_dcd, 128)) {
                            const uint32_t *dcd_w = (const uint32_t *)(uintptr_t)actual_dcd;
                            uint8_t frag_fau_count = dcd_w[17] & 0xFF;  // DCD word 17 = offset 0x44

                            uint64_t frag_res_va  = read_addr64(dcd_w, 24);  // DCD+0x60
                            uint64_t frag_isa_va  = read_addr64(dcd_w, 26);  // DCD+0x68
                            uint64_t frag_tls_va  = read_addr64(dcd_w, 28);  // DCD+0x70
                            uint64_t frag_fau_va  = read_addr64(dcd_w, 30);  // DCD+0x78

                            spy_log("[SPY_DUMP]   -> Frag DCD SE: res=0x%llx isa=0x%llx tls=0x%llx fau=0x%llx fau_count=%u",
                                    frag_res_va, frag_isa_va, frag_tls_va, frag_fau_va, frag_fau_count);

                            // Capture Fragment Shader ISA
                            if (frag_isa_va && probe_readable(frag_isa_va, 256)) {
                                size_t fisa_size = 256;
                                if (probe_readable(frag_isa_va, 4096))
                                    fisa_size = estimate_shader_size((const uint8_t *)(uintptr_t)frag_isa_va, 4096);
                                spy_log("[SPY_DUMP]   -> Frag Shader ISA %zu bytes at 0x%llx", fisa_size, frag_isa_va);
                                char fn[64];
                                snprintf(fn, sizeof(fn), "atom%d_frag_shader_isa", atom_idx);
                                safe_write_capture(fn, frag_isa_va, fisa_size);
                            }

                            // Capture Fragment FAU
                            if (frag_fau_va && frag_fau_count > 0) {
                                size_t fsz = frag_fau_count * 8;
                                char fn[64];
                                snprintf(fn, sizeof(fn), "atom%d_frag_fau", atom_idx);
                                safe_write_capture(fn, frag_fau_va, fsz);

                                /* Also capture the auxiliary state referenced by FAU[0].
                                 * Multi-capture analysis shows the fragment ISA embeds:
                                 *   FAU[0], frag_isa+0x20, frag_isa+0x40,
                                 *   frag_fau-0x700, frag_fau-0x3ff
                                 * so dump both the direct FAU[0] target and a wider window
                                 * around the FAU area used by those relocations.
                                 */
                                if (probe_readable(frag_fau_va, 8)) {
                                    uint64_t fau0_target = *(const uint64_t *)(uintptr_t)frag_fau_va;
                                    spy_log("[SPY_DUMP]   -> Frag FAU[0] target = 0x%llx", fau0_target);

                                    if (fau0_target) {
                                        uint64_t target_base = fau0_target & ~0x3FULL;
                                        char fn2[64];
                                        snprintf(fn2, sizeof(fn2), "atom%d_frag_fau0_target", atom_idx);
                                        safe_write_capture(fn2, target_base, 512);

                                        char fn2b[64];
                                        snprintf(fn2b, sizeof(fn2b), "atom%d_frag_fau0_target_page", atom_idx);
                                        safe_write_capture(fn2b, target_base & ~0xFFFULL, 4096);

                                        if (probe_readable(target_base, 64)) {
                                            const uint64_t *q = (const uint64_t *)(uintptr_t)target_base;
                                            capture_pointer_closure("frag_fau0", atom_idx, q, 8, 8);
                                        }

                                        if (probe_readable(target_base & ~0xFFFULL, 4096)) {
                                            const uint64_t *qp = (const uint64_t *)(uintptr_t)(target_base & ~0xFFFULL);
                                            capture_pointer_closure("frag_fau0_page", atom_idx, qp, 512, 16);
                                        }
                                    }

                                    if (frag_fau_va >= 0x800) {
                                        uint64_t aux_base = (frag_fau_va - 0x800) & ~0x3FULL;
                                        char fn3[64];
                                        char fn4[64];
                                        snprintf(fn3, sizeof(fn3), "atom%d_frag_aux_window", atom_idx);
                                        snprintf(fn4, sizeof(fn4), "atom%d_frag_big_window", atom_idx);
                                        safe_write_capture(fn3, aux_base, 0x1000);
                                        safe_write_capture(fn4, aux_base, 0x4000);

                                        /* Also sweep neighboring pages around the fragment FAU area.
                                         * This helps capture the original driver/job arena contiguously,
                                         * not just pointer-followed islands.
                                         */
                                        uint64_t center_page = frag_fau_va & ~0xFFFULL;
                                        for (int rel = -8; rel <= 8; rel++) {
                                            uint64_t page = center_page + (int64_t)rel * 0x1000;
                                            if (!probe_readable(page, 4096)) continue;
                                            char fnp[96];
                                            snprintf(fnp, sizeof(fnp), "atom%d_frag_arena_page_%llx", atom_idx,
                                                     (unsigned long long)page);
                                            safe_write_capture(fnp, page, 4096);
                                        }
                                    }
                                }
                            }

                            // Capture Fragment TLS
                            if (frag_tls_va) {
                                char fn[64];
                                snprintf(fn, sizeof(fn), "atom%d_frag_tls", atom_idx);
                                safe_write_capture(fn, frag_tls_va, 32);
                                if (probe_readable(frag_tls_va & ~0xFFFULL, 4096)) {
                                    const uint64_t *tls_page = (const uint64_t *)(uintptr_t)(frag_tls_va & ~0xFFFULL);
                                    capture_pointer_closure("frag_tls_page", atom_idx, tls_page, 512, 8);
                                }
                            }

                            // Capture Fragment Resources
                            if (frag_res_va && probe_readable(frag_res_va, 32)) {
                                char fn[64];
                                snprintf(fn, sizeof(fn), "atom%d_frag_resources", atom_idx);
                                safe_write_capture(fn, frag_res_va, 64);
                            }
                        }
                    }
                }
            }
        }
    }

    // ===================== MALLOC VERTEX JOB (Type 11) =====================
    if (job_type == 11 || job_type == 7) {
        uint64_t tiler_ptr = read_addr64(header, 14);
        if (tiler_ptr) {
            spy_log("[SPY_DUMP]   -> Found Tiler Context at 0x%llx", tiler_ptr);
            safe_dump_memory(tiler_ptr, 128, "[SPY_DUMP_TILER]");
        }
        
        // Shader Environment is at +0x100 (Position) for Malloc Vertex
        if (job_type == 11) {
            uint64_t pos_shader = atom->jc + 0x100;
            spy_log("[SPY_DUMP]   -> Found Position Shader Env at 0x%llx", pos_shader);
            if (probe_readable(pos_shader, 64)) {
                safe_dump_memory(pos_shader, 64, "[SPY_DUMP_SH_POS]");
                const uint32_t *sh_env = (const uint32_t *)(uintptr_t)pos_shader;
                uint64_t isa_ptr = read_addr64(sh_env, 2);  // words 2-3 at offset 0x08
                if (isa_ptr) {
                    spy_log("[SPY_DUMP]   -> Found Vertex Shader ISA at 0x%llx", isa_ptr);
                    // Estimate ISA size
                    if (probe_readable(isa_ptr, 256)) {
                        size_t isa_size = 256;
                        if (probe_readable(isa_ptr, 4096)) {
                            isa_size = estimate_shader_size((const uint8_t *)(uintptr_t)isa_ptr, 4096);
                        }
                        safe_dump_memory(isa_ptr, isa_size, "[SPY_DUMP_ISA_VS]");
                        char name[64];
                        snprintf(name, sizeof(name), "atom%d_vertex_shader_isa", atom_idx);
                        safe_write_capture(name, isa_ptr, isa_size);
                    }
                }
            }
        }
    }
}

int ioctl(int fd, int request, ...) {
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    if (!real_ioctl) {
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
        if (!real_ioctl) return -1;
    }

    unsigned int dir = _IOC_DIR(request);
    unsigned int magic = _IOC_TYPE(request);
    unsigned int nr = _IOC_NR(request);
    unsigned int sz = _IOC_SIZE(request);

    // We only care about JOB_SUBMIT for the first 5 frames to avoid logcat spam
    if (spy_enabled && magic == KBASE_IOCTL_TYPE && nr == KBASE_IOCTL_JOB_SUBMIT_NR && dump_count < 10) {
        struct kbase_ioctl_job_submit *submit = arg;
        if (submit && submit->stride == 72) {
            capture_seq++;
            spy_log("Intercepted JOB_SUBMIT #%d with %d atoms", capture_seq, submit->nr_atoms);
            const struct kbase_atom_mtk *atoms = (const struct kbase_atom_mtk *)(uintptr_t)submit->addr;
            write_capture_file("atoms_raw", atoms, submit->nr_atoms * submit->stride);
            for (uint32_t i = 0; i < submit->nr_atoms; i++) {
                dump_atom_meta(&atoms[i], i);
                analyze_and_dump_job(&atoms[i], i);
            }
            dump_count++;
            if (dump_count >= 10) {
                spy_log("Reached dump limit. Disabling spy to prevent log spam.");
                spy_enabled = 0;
            }
        }
    }

    return real_ioctl(fd, request, arg);
}
