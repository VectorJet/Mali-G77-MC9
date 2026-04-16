# Mali kbase r49 - Correct Ioctl Mapping

## CRITICAL DISCOVERY

**The ioctl magic number is `0x80`, NOT `0x67`!**

All previous tests used magic `0x67` which caused ALL ioctls to return EPERM.
The r49 kbase driver defines `KBASE_IOCTL_TYPE` as `0x80`.

When the magic doesn't match any case in the pre-setup switch in `kbase_ioctl()`,
the code falls through to the gatekeeper which checks `kbase_file_get_kctx_if_setup_complete()`.
Since setup was never completed (VERSION_CHECK never matched), it returns EPERM.

## Verified Ioctl Map (ALL WORKING)

| NR | Name | Dir | Size | Full CMD | Status |
|----|------|-----|------|----------|--------|
| 0  | VERSION_CHECK | IOWR | 4 (u16+u16) | 0xc0048000 | **OK** |
| 1  | SET_FLAGS | IOW | 4 (u32) | 0x40048001 | **OK** |
| 2  | JOB_SUBMIT | IOW | 16 (ptr+count+stride) | 0x40108002 | **OK** (stride=64) |
| 3  | GET_GPUPROPS | IOW | 16 | 0x40108003 | **OK** (749 bytes) |
| 5  | MEM_ALLOC | IOWR | 32 | 0xc0208005 | **OK** |
| 7  | MEM_FREE | IOW | 8 | 0x40088007 | OK (needs correct gpu_va) |

## Struct Layouts (r49)

### VERSION_CHECK (4 bytes)
```c
struct kbase_ioctl_version_check {
    uint16_t major;  /* in/out: 11 */
    uint16_t minor;  /* in/out: 13 */
};
```

### SET_FLAGS (4 bytes)
```c
struct kbase_ioctl_set_flags {
    uint32_t create_flags;  /* in: 0 */
};
```

### MEM_ALLOC (32 bytes)
```c
/* Input: */
struct kbase_ioctl_mem_alloc {
    uint64_t va_pages;      /* in: number of VA pages */
    uint64_t commit_pages;  /* in: number of physical pages */
    uint64_t extension;     /* in: grow-on-fault extension */
    uint64_t flags;         /* in: protection flags */
};
/* Output (same struct, overwritten): */
/* [0] = flags (with additions)  */
/* [1] = gpu_va                  */
/* [2] = unchanged               */
/* [3] = flags echoed back        */
```

Verified output example:
- Input: va_pages=1, commit_pages=1, ext=0, flags=0xF
- Output: [0]=0x200F, [1]=0x41000, [2]=0, [3]=0xF
- GPU VA = **0x41000**

### JOB_SUBMIT (16 bytes)
```c
struct kbase_ioctl_job_submit {
    uint64_t addr;     /* userspace pointer to atom array */
    uint32_t nr_atoms; /* number of atoms */
    uint32_t stride;   /* sizeof each atom */
};
```
The atoms themselves are `base_jd_atom_v2` structs pointed to by `addr`.

### MEM_FREE (8 bytes)
```c
struct kbase_ioctl_mem_free {
    uint64_t gpu_addr;  /* GPU VA to free */
};
```

## mmap

GPU memory is mapped via:
```c
mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, mali_fd, gpu_va);
```
Where `gpu_va` comes from MEM_ALLOC output field [1].
Confirmed: offset = gpu_va raw (NOT page-shifted).

## JOB_SUBMIT Details

The atom stride is **64 bytes** (not 48 as in older kbase versions).
A null DEP job (jc=0, core_req=0) with stride=64 succeeds.

The 64-byte atom likely has this layout (needs verification):
```c
struct base_jd_atom_v2_r49 {
    uint64_t jc;                    /*  0: job chain GPU addr */
    uint64_t udata[2];             /*  8: user data (16 bytes) */
    uint64_t extres_list;          /* 24: external resource list */
    uint16_t nr_extres;            /* 32 */
    uint16_t compat_core_req;      /* 34 */
    struct base_dependency pre_dep[2]; /* 36: 4 bytes */
    uint16_t atom_number;          /* 40 */
    uint8_t  prio;                 /* 42 */
    uint8_t  device_nr;            /* 43 */
    uint32_t core_req;             /* 44 */
    uint8_t  padding[16];          /* 48: extra 16 bytes vs old 48-byte struct */
};                                 /* Total: 64 bytes */
```

## Previous "MTK Custom Ioctls"

The ioctls previously identified as "MTK custom" (magic 0x80, NRs 0x13, 0x14, 0x24)
are likely standard kbase ioctls that we hadn't mapped yet:
- NR 0x13 (19) = KBASE_IOCTL_MEM_JIT_INIT?
- NR 0x14 (20) = KBASE_IOCTL_MEM_SYNC?
- NR 0x24 (36) = higher-numbered kbase ioctl

The "MTK magic 0x80" was never MTK-custom — it IS the standard kbase magic.
