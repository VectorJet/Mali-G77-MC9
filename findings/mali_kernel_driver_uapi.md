# ARM Mali Kernel Driver Analysis

## Critical Finding: Stock Mali kbase Driver

The device uses **ARM's open-source mali_kbase driver**, NOT a custom MediaTek driver:
- `/sys/class/misc/mali0/device/driver -> ../../bus/platform/drivers/mali`
- This is the standard ARM Mali kernel driver found in many Android trees

## Kernel Driver Version

From the cloned LibreELEC mali-bifrost repo:
```
refs/mali-kernel/driver/product/kernel/drivers/gpu/arm/midgard/mali_kbase_ioctl.h
```

## ioctl UAPI Documentation

The kernel driver exposes these ioctls (from `mali_kbase_ioctl.h`):

| Ioctl Number | Name | Purpose |
|-------------|------|---------|
| 0 | `KBASE_IOCTL_VERSION_CHECK` | Version handshake |
| 1 | `KBASE_IOCTL_SET_FLAGS` | Set context flags |
| 2 | `KBASE_IOCTL_JOB_SUBMIT` | **Submit GPU jobs** |
| 3 | `KBASE_IOCTL_GET_GPUPROPS` | Get GPU properties |
| 5 | `KBASE_IOCTL_MEM_ALLOC` | Allocate GPU memory |
| 6 | `KBASE_IOCTL_MEM_QUERY` | Query memory region |
| 7 | `KBASE_IOCTL_MEM_FREE` | Free GPU memory |
| 8 | `KBASE_IOCTL_HWCNT_READER_SETUP` | Performance counters |
| 14 | `KBASE_IOCTL_MEM_JIT_INIT` | JIT memory init |
| 15 | `KBASE_IOCTL_MEM_SYNC` | Cache sync |
| 18 | `KBASE_IOCTL_TLSTREAM_ACQUIRE` | Trace stream |
| 22 | `KBASE_IOCTL_MEM_IMPORT` | Import external memory |

**UAPI Version**: `BASE_UK_VERSION_MAJOR 11`, `BASE_UK_VERSION_MINOR 13`

## GPU Properties (ioctl 3)

Queryable properties via `KBASE_IOCTL_GET_GPUPROPS`:

| ID | Property |
|----|----------|
| 1 | PRODUCT_ID |
| 2 | VERSION_STATUS |
| 3 | MINOR_REVISION |
| 4 | MAJOR_REVISION |
| 55 | **GPU_ID** (0x09000800 = G77) |
| 25-28 | Shader/Tiler/L2 core presence |

## Our Device's GPU ID

From sysfs: `Mali-G77 9 cores r0p1 0x09000800`

- GPU ID `0x09000800` = Valhall G77 family
- This maps to Panfrost arch version 10+
- Correlates with Mesa's Valhall support

## Zero ioctls in libgpud.so - Explained

The "zero ioctls" from `strings libgpud.so` was a **false negative**:
- ioctl calls use `_IO` macros which compile to numeric constants
- The strings "ioctl" don't appear in the binary
- The actual calls go through libc's ioctl() syscall wrapper
- Address-referenced, not string-referenced

The communication is:
```
libgpud.so → ioctl(/dev/mali0, KBASE_IOCTL_JOB_SUBMIT, ...) → mali_kbase → GPU
```

## MediaTek Modifications

Despite using stock mali_kbase, MediaTek likely adds:
1. Platform bus integration (via device tree)
2. Custom power management (GED interface)
3. Additional debug nodes
4. Vendor-specific memory backend (ION, DMA-buf)

This is visible from:
- `/sys/class/misc/mali0/device/supplier:platform:soc:ged`
- Custom sysfs entries like `gpuinfo`, `dvfs_period`

## Next Steps

1. **Confirm ioctl usage**: Use LD_PRELOAD or strace on a simple GL app
2. **Cross-reference with libgpud.so**: Match ioctl numbers to function calls
3. **Job descriptor format**: The `KBASE_IOCTL_JOB_SUBMIT` takes `struct base_jd_atom_v2`
4. **Compare with Mesa Panfrost**: Panfrost generates identical job descriptors

## Key Files

- Kernel UAPI: `refs/mali-kernel/driver/product/kernel/drivers/gpu/arm/midgard/mali_kbase_ioctl.h`
- Userspace blob: `libgpud.so` (330KB, 335 functions)
- Device node: `/dev/mali0` (major 10, minor 113)

---

## base_jd_atom_v2 - The Crown Jewel

This is the struct submitted to `KBASE_IOCTL_JOB_SUBMIT`:

```c
typedef struct base_jd_atom_v2 {
    u64 jc;                 // Job chain GPU address (pointer to first job descriptor)
    struct base_jd_udata udata;    // User data (2x u64)
    u64 extres_list;        // External resource list
    u16 nr_extres;          // Number of external resources
    u16 compat_core_req;    // Legacy core requirements
    struct base_dependency pre_dep[2];  // Pre-dependencies
    base_atom_id atom_number;    // Unique atom ID
    base_jd_prio prio;          // Priority
    u8 device_nr;               // Core group (when using SPECIFIC_COHERENT_GROUP)
    u8 padding[1];
    base_jd_core_req core_req;  // Core requirements (FS, CS, T, etc.)
} base_jd_atom_v2;
```

### Core Requirements (base_jd_core_req)

| Flag | Meaning |
|------|---------|
| `BASE_JD_REQ_FS` | Fragment shader job |
| `BASE_JD_REQ_CS` | Compute shader job (covers vertex, geometry, compute) |
| `BASE_JD_REQ_T` | Tiling job |
| `BASE_JD_REQ_V` | Value writeback |
| `BASE_JD_REQ_DEP` | Dependency only (no HW job) |

The `jc` field points to a job chain - a linked list of Valhall job descriptors. That's what Panfrost generates and what libgpud.so builds.