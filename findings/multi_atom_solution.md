# Mali-G77-MC9 Multi-Atom Solution and Root Cause Analysis

**Date:** 2026-04-15

## The Final Breakthrough: 3+ Atoms Work!
We have completely solved the multi-atom hang. 3+ atoms sequentially in a single `JOB_SUBMIT` works flawlessly, dependencies work, and the GPU executes them properly.

## The Massive Revelations

### 1. Previous "Working" Jobs Were Fake!
All previous test programs (like `test_triangle_seq_drain.c` and `test_gpu_works.c`) reported success because of a pointer aliasing mistake in the C code. 
- The target value was at `cpu + 0x30`. 
- The job descriptor payload was initialized at `job[12]`, which corresponds to `cpu + 0x30`. 
- The C program wrote `0x00000001` or `0xCAFEBABE` using the CPU, and then read it back! The GPU never executed a single hardware job.

### 2. The Atom Struct Was Misaligned (The `jit_id` bug)
Our `struct kbase_atom` defined `uint16_t jit_id[2]` (4 bytes). But the Mali Valhall kernel uses `uint8_t jit_id[2]` (2 bytes). 
Because of this 2-byte shift, the kernel read `core_req` (which we set to `0x003` or `0x004`) as `0x00030000` or `0x00040000`. 
These shifted values do not contain ANY hardware execution bits (like `FS` or `CS`). The kernel treated our atoms as **Soft Jobs (Dependency Only)**, completed them immediately without sending them to the GPU hardware, and returned `DONE`.

### 3. MediaTek Added a Custom Field (The 72-byte Stride)
The kernel on the device is UK Version 11.46 compiled with `CONFIG_MALI_MTK_GPU_BM_JM` (Bandwidth Monitor). This adds a custom 4-byte field to the atom structure:
```c
#if defined(CONFIG_MALI_MTK_GPU_BM_JM)
    u32 frame_nr;
#endif
```
This increases the size of `base_jd_atom` from 64 bytes to **72 bytes**. This is why Chrome and our tests used `stride=72`. When we tried to submit 3 atoms using a 66-byte struct but stride 72, Atoms 2 and 3 were read from incorrect memory offsets and failed validation, resulting in `BASE_JD_EVENT_JOB_INVALID` (0x4003).

### 4. `BASE_MEM_SAME_VA` is Mandatory!
When we fixed the struct and `core_req`, the kernel recognized it as a real hardware job and sent it to the GPU. However, the GPU immediately crashed with:
`mali 13000000.mali: Unhandled Page fault in AS1 at VA 0x0000000000041100`

Why? Because in modern Mali drivers, `MEM_ALLOC` returns a **Cookie** (a tracking handle starting at `0x41000`), NOT a real GPU Virtual Address! 
To give the GPU a valid address, you MUST allocate memory using the `BASE_MEM_SAME_VA` flag (`0x2000`). 
When you `mmap` the cookie, the kernel maps the physical memory to the CPU (e.g., `0x70141d9000`) and SIMULTANEOUSLY maps it to the EXACT SAME virtual address in the GPU's MMU. You must cast the CPU pointer directly to a `uint64_t` and use it as your `jc` pointer!

## The Correct Implementation

### Correct Allocation Flags
```c
/* BASE_MEM_PROT_CPU_RD | CPU_WR | GPU_RD | GPU_WR | BASE_MEM_SAME_VA */
uint64_t mem[4] = {4, 4, 0, 0x200F};
ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);

void *cpu = mmap(NULL, 4*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);
uint64_t gva = (uint64_t)cpu;  // THE MAGIC: CPU pointer IS the GPU pointer
```

### Correct `kbase_atom_mtk` Struct
```c
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
    uint8_t  jit_id[2];       // MUST BE uint8_t, NOT uint16_t!
    struct base_dependency pre_dep[2];
    uint8_t  atom_number;
    uint8_t  prio;
    uint8_t  device_nr;
    uint8_t  jobslot;
    uint32_t core_req;
    uint8_t  renderpass_id;
    uint8_t  padding[7];
    uint32_t frame_nr;        // MTK CUSTOM EXTENSION!
    uint32_t pad2;            // Alignment padding
} __attribute__((packed));    // EXACTLY 72 bytes!
```

### Submitting 3 Atoms
You can now submit an array of 3 `kbase_atom_mtk` structures in a single `ioctl(KBASE_IOCTL_JOB_SUBMIT)` call with `nr=3` and `stride=72`. The GPU executes all 3 flawlessly, dependencies are strictly honored, and all target addresses are updated by the hardware.
