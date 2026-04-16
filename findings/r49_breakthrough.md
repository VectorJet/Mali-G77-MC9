# Mali-G77 kbase r49 Breakthrough — Complete Session Log

**Date:** 2026-04-15  
**Device:** Mali-G77-MC9, MediaTek MT6893, kernel module `mali_kbase_mt6893_r49.ko`

---

## The Breakthrough: Magic Number Was Wrong

### Problem
ALL ioctls returned EPERM — VERSION_CHECK, SET_FLAGS, MEM_ALLOC, everything.
This was a regression from earlier sessions where ioctls worked fine.

### Root Cause
**We were using ioctl magic `0x67` but kbase r49 uses magic `0x80`.**

The r49 kernel branch defines:
```c
#define KBASE_IOCTL_TYPE 0x80
```

Older kbase versions used `0x67`. The shift to `0x80` is characteristic of the
r49 branch which consolidated vendor-specific shims into the main driver.

### Why EPERM (Not ENOTTY)
The kbase `kbase_ioctl()` handler has a two-stage dispatch:

1. **Pre-setup switch**: Handles VERSION_CHECK, SET_FLAGS, GET_GPUPROPS
2. **Gatekeeper**: `kbase_file_get_kctx_if_setup_complete()` — returns EPERM if
   the setup state machine hasn't reached `KBASE_FILE_COMPLETE`

When our ioctl command didn't match any case in the pre-setup switch (wrong magic),
it fell through to the gatekeeper. Since VERSION_CHECK never matched, the state
was stuck at `KBASE_FILE_NEED_VSN`, so the gatekeeper returned EPERM for everything.

### Previous "MTK Custom Ioctls" Were Standard kbase
The ioctls we previously identified as "MTK custom magic 0x80" were actually the
standard kbase ioctls all along. The strace output was correct — we misinterpreted it.

---

## Verified Ioctl Map (ALL WORKING)

| NR | Name | Direction | Size | Full CMD | Status |
|----|------|-----------|------|----------|--------|
| 0 | VERSION_CHECK | IOWR | 4 | `0xc0048000` | ✅ |
| 1 | SET_FLAGS | IOW | 4 | `0x40048001` | ✅ |
| 2 | JOB_SUBMIT | IOW | 16 | `0x40108002` | ✅ |
| 3 | GET_GPUPROPS | IOW | 16 | `0x40108003` | ✅ (749 bytes) |
| 5 | MEM_ALLOC | IOWR | 32 | `0xc0208005` | ✅ |
| 7 | MEM_FREE | IOW | 8 | `0x40088007` | ✅ (size confirmed) |

### Ioctl Command Builders
```c
#define KBASE_IOCTL_VERSION_CHECK  _IOC(_IOC_READ|_IOC_WRITE, 0x80, 0, 4)
#define KBASE_IOCTL_SET_FLAGS      _IOC(_IOC_WRITE, 0x80, 1, 4)
#define KBASE_IOCTL_JOB_SUBMIT     _IOC(_IOC_WRITE, 0x80, 2, 16)
#define KBASE_IOCTL_GET_GPUPROPS   _IOC(_IOC_WRITE, 0x80, 3, 16)
#define KBASE_IOCTL_MEM_ALLOC      _IOC(_IOC_READ|_IOC_WRITE, 0x80, 5, 32)
#define KBASE_IOCTL_MEM_FREE       _IOC(_IOC_WRITE, 0x80, 7, 8)
```

---

## Struct Layouts (r49)

### VERSION_CHECK (4 bytes, IOWR)
```c
struct kbase_ioctl_version_check {
    uint16_t major;  /* in/out: 11 */
    uint16_t minor;  /* in/out: 13 */
};
```

### SET_FLAGS (4 bytes, IOW)
```c
struct kbase_ioctl_set_flags {
    uint32_t create_flags;  /* in: 0 */
};
```

### GET_GPUPROPS (16 bytes, IOW)
```c
struct kbase_ioctl_get_gpuprops {
    uint64_t buffer;  /* in: userspace pointer (0 for size query) */
    uint32_t size;    /* in: buffer size */
    uint32_t flags;   /* in: 0 */
};
/* Returns: ret = actual props size (749 bytes on our device) */
```

### MEM_ALLOC (32 bytes, IOWR)
```c
/* Input (4x u64): */
struct kbase_ioctl_mem_alloc {
    uint64_t va_pages;      /* number of VA pages */
    uint64_t commit_pages;  /* number of physical pages */
    uint64_t extension;     /* grow-on-fault extension */
    uint64_t flags;         /* BASE_MEM_PROT_* flags */
};
/* Output (same buffer, overwritten): */
/*   [0] = output flags (with additions like SAME_VA)     */
/*   [1] = gpu_va (the GPU virtual address)               */
/*   [2] = unchanged                                       */
/*   [3] = flags echoed back                               */
```

**Verified allocation:**
- Input: `va_pages=2, commit_pages=2, ext=0, flags=0xF`
- Output: `[0]=0x200F, [1]=0x41000, [2]=0, [3]=0xF`
- GPU VA = `0x41000`

**Note:** `BASE_MEM_SAME_VA` (bit 9) causes ENOMEM. Don't use it. Use without
SAME_VA and mmap with the returned gpu_va as offset.

### Memory flags
```c
#define BASE_MEM_PROT_CPU_RD    (1ULL << 0)
#define BASE_MEM_PROT_CPU_WR    (1ULL << 1)
#define BASE_MEM_PROT_GPU_RD    (1ULL << 2)
#define BASE_MEM_PROT_GPU_WR    (1ULL << 3)
// Use all 4 for read/write from both CPU and GPU: flags = 0xF
```

### JOB_SUBMIT (16 bytes, IOW)
```c
struct kbase_ioctl_job_submit {
    uint64_t addr;      /* userspace pointer to atom array */
    uint32_t nr_atoms;  /* number of atoms */
    uint32_t stride;    /* sizeof each atom = 64 */
};
```

### MEM_FREE (8 bytes, IOW)
```c
struct kbase_ioctl_mem_free {
    uint64_t gpu_addr;  /* GPU VA to free */
};
```

---

## mmap Convention

```c
void *cpu_ptr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpu_va);
```

- `offset = gpu_va` (raw value from MEM_ALLOC output, NOT page-shifted)
- Confirmed working: `mmap(NULL, 8192, ..., fd, 0x41000)` → success

---

## Atom Layout (base_jd_atom, 64 bytes)

The 64-byte stride corresponds to `struct base_jd_atom` (not the older 48-byte
`base_jd_atom_v2`). The extra 16 bytes are:
- **+8 bytes** at front: `seq_nr` (u64) prepended before `jc`
- **+8 bytes** at back: `jobslot`, `renderpass_id`, expanded `padding[7]`

```
Offset  Size  Field
------  ----  -----
+0x00    8    seq_nr          (set 0)
+0x08    8    jc              (GPU VA of job chain descriptor)
+0x10    8    udata[0]        (user data, returned in completion event)
+0x18    8    udata[1]
+0x20    8    extres_list     (0 for simple jobs)
+0x28    2    nr_extres       (0)
+0x2a    2    jit_id[2]       (0)
+0x2c    1    pre_dep[0].atom_id
+0x2d    1    pre_dep[0].dep_type
+0x2e    1    pre_dep[1].atom_id
+0x2f    1    pre_dep[1].dep_type
+0x30    1    atom_number     (1-255, unique per in-flight atom)
+0x31    1    prio            (0=MEDIUM)
+0x32    1    device_nr       (0)
+0x33    1    jobslot         (0)
+0x34    4    core_req        (0=DEP, 0x10=V, etc.)
+0x38    1    renderpass_id   (0)
+0x39    7    padding         (must be 0, kernel checks)
```

### Valid strides accepted by kernel
| Stride | Struct |
|--------|--------|
| 48 | `offsetof(base_jd_atom_v2, renderpass_id)` — old atom |
| 56 | `sizeof(base_jd_atom_v2)` — full v2 with renderpass_id |
| 64 | `sizeof(base_jd_atom)` — **our working stride** with seq_nr |

### core_req flags
```c
#define BASE_JD_REQ_DEP  0          /* dependency only, no HW job */
#define BASE_JD_REQ_FS   (1u << 0)  /* fragment shader */
#define BASE_JD_REQ_CS   (1u << 1)  /* compute/vertex/geometry */
#define BASE_JD_REQ_T    (1u << 2)  /* tiler */
#define BASE_JD_REQ_CF   (1u << 3)  /* cache flush */
#define BASE_JD_REQ_V    (1u << 4)  /* value writeback */
```

---

## Valhall Job Descriptor Format (from Mesa genxml v9.xml)

### Job Header (32 bytes, 128-byte aligned)
```
Offset  Size  Field
------  ----  -----
+0x00    4    exception_status      (GPU writes, init 0)
+0x04    4    first_incomplete_task (GPU writes, init 0)
+0x08    8    fault_pointer         (GPU writes, init 0)
+0x10    4    control word:
                bit 0:     reserved
                bits[7:1]: Job Type (2=WRITE_VALUE)
                bit 8:     Barrier
                bit 11:    Suppress Prefetch
                bits[15:14]: Relax Dependency 1, 2
                bits[31:16]: Index (1-based)
+0x14    4    dep1[15:0] | dep2[31:16]
+0x18    8    next_job_ptr (0 if last)
```

### Write Value Job Payload (starts at +0x20)
```
+0x20    8    target GPU address (where to write)
+0x28    4    Write Value Type:
                1=Cycle Counter, 2=System Timestamp, 3=Zero,
                4=Immediate8, 5=Immediate16, 6=Immediate32, 7=Immediate64
+0x2c    4    reserved (0)
+0x30    8    immediate value (the value to write)
```

### Key differences from Bifrost
- **Alignment**: Valhall = 128 bytes, Bifrost = 64 bytes
- **Bit 0 of control word**: Bifrost has explicit `Is 64b` flag; Valhall implicit
- Everything else (type numbers, payload layout) is identical

---

## Current Status: JOB_SUBMIT Works, GPU Doesn't Execute

### What works
- Device open ✅
- VERSION_CHECK (11.13) ✅
- SET_FLAGS ✅
- GET_GPUPROPS (749 bytes, GPU ID 0x09000800) ✅
- MEM_ALLOC ✅ (gpu_va = 0x41000)
- mmap ✅ (offset = gpu_va raw)
- **JOB_SUBMIT ✅** (stride=64, ret=0, no error)
- **NULL DEP job ✅** (core_req=0, succeeds)

### What doesn't work yet
- **GPU execution**: WRITE_VALUE job submits successfully (ret=0) but the GPU
  doesn't write to the target address. After 1 second:
  - Target still = `0xAAAAAAAA` (sentinel, unchanged)
  - Job descriptor exception_status = `0x00000000` (GPU never touched it)
  - Job descriptor fault_pointer = `0x0000000000000000`
  - No GPU faults in dmesg

### Analysis of the execution gap
The job was submitted to the kernel's software queue but the GPU hardware never
executed it. Possible causes:

1. **core_req=0 (DEP)** — A DEP job is software-only; it doesn't dispatch to
   hardware. The kernel just completes it immediately in the software queue.
   The WRITE_VALUE job type in the descriptor is irrelevant if core_req says DEP.
   **Fix: Use a non-zero core_req that dispatches to the job manager HW.**

2. **Wrong core_req for WRITE_VALUE** — WRITE_VALUE (type 2) is a job manager
   job, not a shader job. It might need `core_req = BASE_JD_REQ_V` (0x10) or
   possibly `BASE_JD_REQ_T` to hit the right hardware slot.

3. **Job completion notification** — We're polling the target address, but the
   kernel might need us to read completion events (via `read()` or `mmap` on the
   event ring) before it actually commits the job to hardware.

4. **GPU power state** — The GED subsystem might not have powered on the GPU.
   The job sits in the kernel queue waiting for GPU power-up that never comes
   because GED wasn't notified.

---

## Next Steps

### 1. Fix core_req for WRITE_VALUE
Try submitting with `core_req = BASE_JD_REQ_V` (0x10) instead of DEP (0).
The kernel only dispatches to GPU hardware when core_req indicates a real HW job.

### 2. Read completion events
The kbase driver exposes job completion via `read()` on the mali fd or via
an mmap'd event ring. We should:
```c
// After JOB_SUBMIT, read the completion event:
struct base_jd_event_v2 {
    uint32_t event_code;
    uint32_t atom_number;
    uint64_t udata[2];
};
read(fd, &event, sizeof(event));
```

### 3. Check if GPU power needs explicit activation
```bash
cat /sys/kernel/ged/hal/gpu_power_state
cat /sys/class/misc/mali0/device/power/runtime_status
```

### 4. Strace a working app to compare
Use the LD_PRELOAD spy (`src/kbase/ioctl_spy.c`) on a working app to see
what core_req values and atom layouts they use for real GPU work.

---

## Files Created This Session

| File | Purpose |
|------|---------|
| `src/kbase/ioctl_spy.c` | LD_PRELOAD ioctl interceptor for /dev/mali0 |
| `src/kbase/replay_strace.c` | Multi-phase ioctl sequence replay test |
| `src/kbase/magic_test.c` | Confirmed magic 0x80 works |
| `src/kbase/kbase_r49_test.c` | Full ioctl exploration (sizes, mmap) |
| `src/kbase/kbase_r49_full.c` | Complete test with JOB_SUBMIT stride scan |
| `src/kbase/gpu_hello.c` | WRITE_VALUE job — submits but GPU doesn't execute yet |
| `findings/r49_ioctl_mapping.md` | Ioctl reference (quick lookup) |
| `findings/r49_breakthrough.md` | This document |

## Key Insight for Future Sessions

**The "MTK custom ioctls" never existed.** The ioctl magic was always `0x80`
(standard kbase r49). All previous EPERM failures were from using the wrong magic
`0x67`. The strace captures from lawnchair were showing us the correct ioctls
the entire time — we just misread the magic number as a vendor extension.
