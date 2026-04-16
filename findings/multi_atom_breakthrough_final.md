# Mali-G77-MC9 Multi-Atom Execution & Memory Breakthrough

**Date:** 2026-04-15  
**Device:** Mali-G77-MC9 (MediaTek MT6893)  
**Driver:** kbase r49 (UK Version 11.46)  

---

## Executive Summary

The "multi-atom hang" and the need for sequential drains have been completely resolved. We can now submit any number of atoms in a single batched `JOB_SUBMIT` ioctl, and the GPU hardware successfully executes them while respecting dependencies.

Achieving this required unraveling four compounding layers of misunderstanding:
1. **The Fake Success Illusion**: Previous tests were accidentally writing the "success" value from the CPU via pointer aliasing, meaning the GPU was never actually running.
2. **The `jit_id` Struct Alignment Bug**: A 2-byte sizing error caused the kernel to read `core_req` as `0x30000`, turning every submission into a dependency-only Soft Job.
3. **The MediaTek `frame_nr` Extension**: The kernel was compiled with a custom bandwidth monitor (`CONFIG_MALI_MTK_GPU_BM_JM`), extending the atom structure to exactly 72 bytes.
4. **The `SAME_VA` Memory Requirement**: `MEM_ALLOC` does not return a GPU Virtual Address; it returns a tracking handle ("cookie"). The GPU MMU will crash with an Unhandled Page Fault unless memory is allocated with the `BASE_MEM_SAME_VA` flag.

---

## 1. The Fake Success Illusion

In previous tests (like `test_gpu_works.c` and `test_triangle_seq_drain.c`), we believed the GPU was successfully processing single jobs or chained jobs with drains. 

This was a false positive caused by a C pointer aliasing mistake:
- The target value was checked at `cpu + 0x30`.
- The Valhall `WRITE_VALUE` job payload is initialized at index 12 of a 32-bit array (`job[12]`), which is exactly `48 bytes` (or `0x30`).
- The test code executed `job[12] = 0xCAFEBABE` using the CPU, which overwrote the target value in memory. 
- The GPU never executed the job. The test simply read back what the CPU just wrote and declared success.

## 2. The `jit_id` Misalignment and Soft Jobs

Because the GPU wasn't actually running, why didn't the kernel reject the jobs? 

Our `kbase_atom` struct defined `uint16_t jit_id[2]` (4 bytes). However, the official r49 Valhall kernel uses `uint8_t jit_id[2]` (2 bytes). 
Because of this 2-byte shift, all fields after `jit_id` were misaligned when read by the kernel. 

When we set `core_req = 0x003` (FS + CS) or `0x004` (TILER), the kernel read the shifted bytes and evaluated `core_req` as `0x00030000` or `0x00040000`. 
These shifted values do not contain any hardware execution bits (`BASE_JD_REQ_FS`, `BASE_JD_REQ_CS`, `BASE_JD_REQ_T`). 

According to the kernel's `BASE_JD_REQ_SOFT_JOB_OR_DEP` macro, any job lacking hardware execution bits is treated as a **Soft Job (Dependency Only)**. The kernel completed the jobs immediately in software without ever sending them to the GPU hardware, which perfectly bypassed any potential hardware faults.

## 3. The MediaTek `frame_nr` Extension (72-byte Stride)

When we corrected the `jit_id` size, the kernel recognized the hardware bits in `core_req` and attempted to execute them. However, submitting 3 atoms at once returned `BASE_JD_EVENT_JOB_INVALID` (0x4003).

The MT6893 driver is compiled with `CONFIG_MALI_MTK_GPU_BM_JM` (MediaTek GPU Bandwidth Monitor). This adds a custom 4-byte `frame_nr` field to the end of the standard `base_jd_atom` struct:
```c
#if defined(CONFIG_MALI_MTK_GPU_BM_JM)
    u32 frame_nr;
#endif
```
This increases the base 64-byte struct to 68 bytes. With 8-byte boundary alignment padding, the total size becomes **exactly 72 bytes**. 

When we submitted 3 atoms using a 66-byte struct array but a 72-byte stride, Atom 0 succeeded, but Atoms 1 and 2 were read from offset `72` and `144` (while the data was at `66` and `132`). The misaligned reads caused the kernel to see non-zero padding bytes, triggering a `JOB_INVALID` rejection.

## 4. The `SAME_VA` Memory Requirement & The "Cookie"

With the struct fixed and exactly 72 bytes long, the kernel successfully sent the job to the GPU hardware. However, the hardware immediately crashed with a fatal dmesg error:
`mali 13000000.mali: Unhandled Page fault in AS1 at VA 0x0000000000041100`

**Why did `0x41100` page-fault?**
In modern Mali drivers, `KBASE_IOCTL_MEM_ALLOC` does **not** return a GPU Virtual Address. It returns a **Cookie** (a tracking handle starting at `BASE_MEM_FIRST_FREE_ADDRESS = 0x41000`). The GPU's MMU has absolutely no mapping for this cookie.

To give the GPU a valid address, memory must be allocated using the `BASE_MEM_SAME_VA` flag (`0x2000`).
When you `mmap` the cookie, the kernel maps the physical memory to the CPU (e.g., `0x70141d9000`) and **simultaneously maps it to the exact same virtual address in the GPU's MMU**. 

You must cast the CPU pointer directly to a `uint64_t` and use it as your `jc` (Job Chain) pointer!

---

## The Definitive Working Implementation

### 1. The Correct Struct Definition
```c
struct base_dependency { 
    uint8_t atom_id; 
    uint8_t dep_type; 
} __attribute__((packed));

struct kbase_atom_mtk {
    uint64_t seq_nr;          // +0
    uint64_t jc;              // +8  (MUST be SAME_VA mapped pointer)
    uint64_t udata[2];        // +16
    uint64_t extres_list;     // +32
    uint16_t nr_extres;       // +40
    uint8_t  jit_id[2];       // +42 (MUST BE uint8_t)
    struct base_dependency pre_dep[2]; // +44
    uint8_t  atom_number;     // +48
    uint8_t  prio;            // +49
    uint8_t  device_nr;       // +50
    uint8_t  jobslot;         // +51
    uint32_t core_req;        // +52
    uint8_t  renderpass_id;   // +56
    uint8_t  padding[7];      // +57
    uint32_t frame_nr;        // +64 (MTK CUSTOM EXTENSION)
    uint32_t pad2;            // +68 (Alignment padding)
} __attribute__((packed));    // Total Size: 72 bytes
```

### 2. Correct Memory Allocation (SAME_VA)
```c
/* BASE_MEM_PROT_CPU_RD (1) | CPU_WR (2) | GPU_RD (4) | GPU_WR (8) | BASE_MEM_SAME_VA (0x2000) */
uint64_t mem[4] = {pages, pages, 0, 0x200F};
ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem);

/* mmap using the cookie returned in mem[1] */
void *cpu = mmap(NULL, pages*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mem[1]);

/* THE MAGIC: The CPU pointer is now SIMULTANEOUSLY the GPU Virtual Address */
uint64_t gva = (uint64_t)cpu;  
```

### 3. Multi-Atom Batch Submission
You can now build an array of `struct kbase_atom_mtk` and submit them all at once. 

```c
struct kbase_atom_mtk atoms[3] = {0};

/* Atom 1: Tiler/Compute (Slot 1) */
atoms[0].jc = gva + 0x000;
atoms[0].core_req = 0x4a;     // CS + CF + COHERENT_GROUP
atoms[0].atom_number = 1;
atoms[0].jobslot = 1;
atoms[0].frame_nr = 1;

/* Atom 2: Fragment (Slot 0) */
atoms[1].jc = gva + 0x200;
atoms[1].core_req = 0x49;     // FS + CF + COHERENT_GROUP
atoms[1].atom_number = 2;
atoms[1].jobslot = 0;
atoms[1].pre_dep[0].atom_id = 1;  // Wait for Atom 1
atoms[1].pre_dep[0].dep_type = 1; // DATA dependency
atoms[1].frame_nr = 1;

/* Submit the batch */
struct { uint64_t addr; uint32_t nr, stride; } submit = { (uint64_t)atoms, 2, 72 };
ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
```

### Conclusion
With the 72-byte MTK struct, valid `core_req` flags, and `SAME_VA` memory mapping, the Mali-G77-MC9 successfully executes deeply chained, batched GPU jobs natively. No workaround drains are required.
