# Mali-G77 Valhall v9 Compute Job (Type 4) Descriptor Layout

**Date:** 2026-04-15
**Source:** Mesa Panfrost `genxml/v9.xml` + `pan_jm.c` + `pan_desc.h` + Chrome ioctl capture
**Device:** Mali-G77-MC9 (MediaTek MT6893, Valhall v9)

---

## Overview

The Compute Job (Type 4) is a **128-byte** hardware descriptor used for:
- Compute shader dispatch (GPGPU)
- **Vertex processing** (Chrome uses this instead of Malloc Vertex Type 11!)
- Geometry operations

It is vastly simpler than the 384-byte Malloc Vertex Job (Type 11), making it the preferred path for our triangle rendering.

---

## Aggregate Layout: Compute Job (128 bytes, 128-byte aligned)

From v9.xml the aggregate is:
- **Header** at offset 0, type `Job Header` (32 bytes)
- **Payload** at offset 32, type `Compute Payload` (96 bytes = 24 genxml words × 4)

```
Offset  Size   Section
------  ----   -------
0x00    32     Job Header
0x20    32     Workgroup params (sizes, counts, offsets)
0x40    64     Shader Environment (inline at payload word 8)
                 Total = 32 + 96 = 128 bytes
```

**IMPORTANT**: In v9.xml, `size` values are in **32-bit word units**, not bytes.
- `Compute Payload size=24` = 24 words = **96 bytes**
- `Shader Environment size=16` = 16 words = **64 bytes**
- `Local Storage size=8` = 8 words = **32 bytes**
- `Fragment Job Payload size=8` = 8 words = **32 bytes**

---

## Section 1: Job Header (32 bytes, offset 0x00)

From `v9.xml` struct `Job Header` (align=128):

```
Byte Offset  Field                  Size(bits)  Bit Pos      Type
----------   -----                  ----------  -------      ----
0x00         Exception Status       32          0:0          uint
0x04         First Incomplete Task  32          1:0          uint
0x08         Fault Pointer          64          2:0          address
0x10         Type                   7           4:1          JobType  ← Bits [7:1]
0x10         Barrier                1           4:8          bool
0x10         Suppress Prefetch      1           4:11         bool
0x10         Relax Dependency 1     1           4:14         bool
0x10         Relax Dependency 2     1           4:15         bool
0x10         Index                  16          4:16         uint     ← Bits [31:16]
0x14         Dependency 1           16          5:0          uint
0x14         Dependency 2           16          5:16         uint
0x18         Next                   64          6:0          address  ← Job chain pointer
```

### Key Fields for Compute Job:
- **Type = 4** (Compute): Encoded in bits [7:1] of word at 0x10. So `word[4] = (4 << 1) = 0x08`
- **Index**: Job index for tracking (Chrome uses 1)
- **Dependency 1/2**: Point to previous job indices (for chaining)
- **Next**: GPU VA pointer to next job in chain (0 = end of chain)

### Byte-level Encoding of Control Word (0x10):
```
word[4] = (Type << 1) | (Barrier << 8) | (SuppressPrefetch << 11)
                            | (RelaxDep1 << 14) | (RelaxDep2 << 15)
                            | (Index << 16)

For Compute Job with Index=1:
  word[4] = (4 << 1) | (1 << 16) = 0x00010008
```

This matches Chrome's captured control word `0x00010008`!

---

## Section 2: Compute Payload (offset 0x20, 96 bytes = 24 words)

From `v9.xml` struct `Compute Payload` (size=24 **words** = 96 bytes):

```
Byte Offset  Field                    Size(bits)  Bit Pos      Type/Modifier
----------   -----                    ----------  -------      -------------
0x20         Workgroup size X         10          0:0          uint (minus 1)
0x20         Workgroup size Y         10          0:10         uint (minus 1)
0x20         Workgroup size Z         10          0:20         uint (minus 1)
0x20         Allow merging workgroups 1           0:31         bool
0x24         Task increment           14          1:0          uint (default=1)
0x24         Task axis                2           1:14         TaskAxis
0x28         Workgroup count X        32          2:0          uint
0x2C         Workgroup count Y        32          3:0          uint
0x30         Workgroup count Z        32          4:0          uint
0x34         Offset X                 32          5:0          uint
0x38         Offset Y                 32          6:0          uint
0x3C         Offset Z                 32          7:0          uint
0x40         Shader Environment       512         8:0          ShaderEnvironment (16 words, inline)
```

### Workgroup Size Encoding
Workgroup sizes are stored as **value - 1**:
- For 1×1×1 workgroup: `size_x=0, size_y=0, size_z=0`
- For 32×1×1 workgroup: `size_x=31, size_y=0, size_z=0`

### Task Axis Values
From `v9.xml` enum `Task Axis`:
- 0: X
- 1: Y
- 2: Z

Panfrost uses `MALI_TASK_AXIS_Z` (2) for compute dispatch.

### Chrome's Captured Payload (starting at 0x20):
```
0020: 00 00 00 80 00 81 00 00  01 00 00 00 01 00 00 00
0030: 01 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0040: 00 00 00 00 04 00 00 00  00 00 00 00 00 00 00 00
0050: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0060: c8 e5 d8 ff 5e 00 00 00  00 e0 ff ff 5e 00 00 00
0070: 40 e6 d8 ff 5e 00 00 00  00 e7 d8 ff 5e 00 00 00
```

Decoded:
```
0x20: 0x80000000  → size_x=0, size_y=0, size_z=0, allow_merging=1
0x24: 0x00008100  → task_increment=256 (bits[13:0] = 0x100), task_axis=Z (bits[15:14] = 2)
0x28: 0x00000001  → workgroup_count_x = 1
0x2C: 0x00000001  → workgroup_count_y = 1
0x30: 0x00000001  → workgroup_count_z = 1
0x34: 0x00000000  → offset_x = 0
0x38: 0x00000000  → offset_y = 0
0x3C: 0x00000000  → offset_z = 0
```

### Shader Environment (inline, starting at 0x40):
```
0x40: 0x00000000  → attribute_offset = 0
0x44: 0x00000004  → fau_count = 4 (bits[7:0] of word 1)
0x48-0x5F:         → padding/reserved (SE words 2-7)
0x60: 0x5effd8e5c8 → Resources pointer (SE word 8)
0x68: 0x5effffe000 → Shader pointer (SE word 10)
0x70: 0x5effd8e640 → Thread Storage pointer (SE word 12)
0x78: 0x5effd8e700 → FAU pointer (SE word 14)
```

**NOTE on task_increment=256**: Chrome uses a large task_increment (256) with
Z-axis dispatch (same axis as Panfrost). Panfrost defaults to task_increment=1.
Both work — the hardware dispatches min(task_increment, actual_workgroup_count)
workgroups per task. A larger increment batches more workgroups per hardware task.

---

## Section 3: Shader Environment (64 bytes = 16 words, at SE word 8 within payload = job offset 0x40)

From `v9.xml` struct `Shader Environment` (size=16 **words** = 64 bytes, align=64):

**IMPORTANT**: The genxml `size` is in **32-bit word units**, not bytes.

```
SE Word  Job Offset  Field              Size(bits)  Type            Description
-------  ----------  -----              ----------  ----            -----------
0        0x40        Attribute offset    32          uint            Byte offset to first attribute
1        0x44        FAU count           8           uint            # of 64-bit FAU entries (bits[7:0])
1        0x44        (reserved)          24          —               bits[31:8] unused
2-7      0x48-0x5F   (padding)           192         —               Reserved / zero
8-9      0x60-0x67   Resources           64          address (GPU VA) Resource table pointer
10-11    0x68-0x6F   Shader              64          address (GPU VA) Shader ISA binary pointer
12-13    0x70-0x77   Thread storage      64          address (GPU VA) Local Storage desc pointer
14-15    0x78-0x7F   FAU                 64          address (GPU VA) Fast Access Uniforms ptr
```

**KEY CORRECTION**: Earlier analysis mislabeled these fields. The correct order is:
- **0x60 = Resources** (NOT Shader)
- **0x68 = Shader** (NOT FAU)
- **0x70 = Thread Storage** (NOT Resources)
- **0x78 = FAU** (NOT Thread Storage)

This matches the genxml word positions: 64-bit address fields start at even word boundaries
(word 8, 10, 12, 14) for alignment.

### Field Descriptions

| Field | Description |
|-------|-------------|
| **Attribute offset** | Byte offset to the first attribute in the attribute descriptor table. For compute shaders, this is typically 0. Chrome uses 0. |
| **FAU count** | Number of 64-bit FAU (Fast Access Uniform) entries. Each FAU entry is 8 bytes. |
| **Resources** | GPU VA pointer to the resource table (descriptor set). Contains textures, samplers, attribute buffers, and image descriptors. |
| **Shader** | GPU VA pointer to the shader binary (Valhall ISA). The lower bits encode the shader stage and register allocation. |
| **Thread storage** | GPU VA pointer to the Local Storage descriptor (TLS/WLS). Contains base pointer and size for thread-local and workgroup-local memory. |
| **FAU** | GPU VA pointer to the Fast Access Uniforms buffer. Contains small inline uniform values pushed directly to the shader. |

### Chrome's Shader Environment Pointers (CORRECTED mapping):
- **0x60: Resources** = `0x5effd8e5c8` — Resource table (descriptor set)
- **0x68: Shader** = `0x5effffe000` — Compiled Valhall ISA (page-aligned)
- **0x70: Thread Storage** = `0x5effd8e640` — TLS/WLS descriptor
- **0x78: FAU** = `0x5effd8e700` — Fast Access Uniforms buffer

---

## Section 4: Local Storage / Thread Storage (32 bytes = 8 words, 64-byte aligned)

From `v9.xml` struct `Local Storage` (size=8 **words** = 32 bytes, align=64):

```
Word  Byte Offset  Field              Size(bits)  Bit Pos    Type/Modifier
-----  ----------  -----              ----------  -------    -------------
0      0x00         TLS Size            5           0:0        uint
1      0x04         WLS Instances       5           1:0        uint (log2, default=NO_WORKGROUP_MEM)
1      0x04         WLS Size Base       2           1:5        uint
1      0x04         WLS Size Scale      5           1:8        uint
2      0x08         TLS Base Pointer    48          2:0        address
3      0x0C         TLS Address Mode    4           3:28       AddressMode
4-5    0x10         WLS Base Pointer    64          4:0        address
6-7    0x18         (padding)           —           —          zero (struct is 8 words = 32 bytes)
```

### Special Values
- `WLS Instances = 0x80000000` = `MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM` (no WLS needed)
- For simple compute shaders without shared memory, set WLS fields to this default.

---

## Section 5: Complete 128-byte Compute Job Memory Map

```
Offset  Field                          Value/Description
------  -----                          ------------------
        === JOB HEADER (32 bytes) ===
0x00    Exception Status               0 (initial)
0x04    First Incomplete Task          0 (initial)
0x08    Fault Pointer                  0 (initial)
0x10    Control Word                   (Type<<1) | (Index<<16)
                                       For Compute: (4<<1)|(1<<16) = 0x00010008
0x14    Dependency 1 (lo16) + Dep 2    0 or prev job index
0x18    Next Job Pointer               0 (end of chain) or GPU VA

        === COMPUTE PAYLOAD (96 bytes) ===
        --- Workgroup parameters (32 bytes) ---
0x20    Workgroup sizes + flags        (szX-1)|(szY-1)<<10|(szZ-1)<<20|merge<<31
0x24    Task increment + axis          Panfrost: incr=1, axis=Z(2)
                                       Chrome: incr=256, axis=Z(2)
0x28    Workgroup count X              1
0x2C    Workgroup count Y              1
0x30    Workgroup count Z              1
0x34    Offset X                       0
0x38    Offset Y                       0
0x3C    Offset Z                       0

        --- Shader Environment (64 bytes, inline at SE word 0) ---
0x40    Attribute offset               0
0x44    FAU count (bits[7:0])          4 (Chrome uses 4 entries = 32 bytes FAU)
0x48    (reserved, SE words 2-7)       0
0x4C    "                               0
0x50    "                               0
0x54    "                               0
0x58    "                               0
0x5C    "                               0
0x60    Resources pointer (SE word 8)  GPU VA of resource table
0x68    Shader pointer (SE word 10)    GPU VA of shader ISA binary
0x70    Thread Storage ptr (SE word 12) GPU VA of Local Storage desc
0x78    FAU pointer (SE word 14)       GPU VA of FAU buffer

        --- End at 0x80 = 128 bytes ---
```

---

## Section 6: How Panfrost Emits a Compute Job (from pan_jm.c)

The Mesa Panfrost driver constructs compute jobs in `GENX(jm_launch_grid)`:

### Step 1: Allocate the descriptor
```c
struct panfrost_ptr job = pan_pool_alloc_desc(pool, COMPUTE_JOB);
```

### Step 2: Pack the Payload section
```c
pan_section_pack(job.cpu, COMPUTE_JOB, PAYLOAD, cfg) {
    cfg.workgroup_size_x = info->block[0];
    cfg.workgroup_size_y = info->block[1];
    cfg.workgroup_size_z = info->block[2];

    cfg.workgroup_count_x = info->grid[0]; // or from indirect
    cfg.workgroup_count_y = info->grid[1];
    cfg.workgroup_count_z = info->grid[2];

    cfg.task_increment = 1;    // Chrome uses 256, Panfrost uses 1
    cfg.task_axis = MALI_TASK_AXIS_Z;  // Chrome uses X, Panfrost uses Z

    // Allow merging only if no variable shared memory
    cfg.allow_merging_workgroups =
        cs->info.cs.allow_merging_workgroups &&
        info->variable_shared_mem == 0;

    // Emit shader environment inline
    cfg.compute.resources = panfrost_emit_resources(batch, MESA_SHADER_COMPUTE);
    cfg.compute.shader = shader_ptr;
    cfg.compute.thread_storage = batch->tls.gpu;
    cfg.compute.fau = batch->push_uniforms[MESA_SHADER_COMPUTE];
    cfg.compute.fau_count = DIV_ROUND_UP(batch->nr_push_uniforms[MESA_SHADER_COMPUTE], 2);
}
```

### Step 3: Set up the Job Header
```c
// The header is populated by the job chaining infrastructure
// Type is set to MALI_JOB_TYPE_COMPUTE = 4
// Dependencies are set based on the job graph
// Next pointer links to subsequent jobs in the chain
```

---

## Section 7: Companion Fragment Job (Type 9) Layout

For the triangle, we need a Fragment Job after the Compute Job.

From `v9.xml` aggregate `Fragment Job` (align=128):

```
Offset  Section                  Type
------  -------                  ----
0x00    Header                   Job Header (32 bytes)
0x20    Payload                  Fragment Job Payload (8 bytes)
```

### Fragment Job Payload (32 bytes = 8 words):
```
Word  Byte Offset  Field                    Size(bits)  Bit Pos      Type
-----  ----------  -----                    ----------  -------      ----
0      0x20        Bound Min X              12          0:0          uint
0      0x20        Bound Min Y              12          0:16         uint
1      0x24        Bound Max X              12          1:0          uint
1      0x24        Bound Max Y              12          1:16         uint
1      0x24        Tile render order        3           1:28         TileRenderOrder
1      0x24        Has Tile Enable Map      1           1:31         bool
2-3    0x28        Framebuffer pointer      64          2:0          address (lower 6 bits = type)
4-5    0x30        Tile Enable Map pointer  64          4:0          address
6      0x34        Tile Enable Map Stride   8           6:0          uint
7      0x38        (padding)                —           —            —
```

Total Fragment Job = 32 (header) + 32 (payload) = **64 bytes**.

### Chrome's Fragment Job:
```
Control word: 0x00010012  → Type = (0x12 >> 1) & 0x7F = 9 (Fragment), Index = 1
Payload:
0x20: 00 00 00 00 00 00 00 00   → Bounds = 0, no tile enable map
0x28: 81 fb d8 ff 5e 00 00 00   → Framebuffer = 0x5effd8fb81 (lower 6 bits = type tag)
                                   Actual FBD = 0x5effd8fb80
```

---

## Section 8: Atom Submission for Compute → Fragment Pipeline

Using our 72-byte `kbase_atom_mtk` struct:

```c
// Atom 1: Compute Job (Type 4)
atoms[0].jc = compute_job_gpu_va;      // Points to 128-byte Compute Job descriptor
atoms[0].core_req = 0x4e;              // CS + CF + COHERENT_GROUP (Chrome's value)
atoms[0].atom_number = 1;
atoms[0].jobslot = 1;                  // Compute goes to jobslot 1
atoms[0].frame_nr = 1;

// Atom 2: Fragment Job (Type 9)
atoms[1].jc = fragment_job_gpu_va;     // Points to 64-byte Fragment Job descriptor
atoms[1].core_req = 0x49;             // FS + CF + COHERENT_GROUP
atoms[1].atom_number = 2;
atoms[1].jobslot = 0;                  // Fragment goes to jobslot 0
atoms[1].pre_dep[0].atom_id = 1;       // Wait for Compute atom
atoms[1].pre_dep[0].dep_type = 1;      // DATA dependency
atoms[1].frame_nr = 1;
```

---

## Section 9: Resource Table Layout

The `Resources` pointer in Shader Environment points to a descriptor table.
From Panfrost's resource emission (`panfrost_emit_resources`):

```
Offset  Descriptor
------  ----------
0x00    Sampler descriptor 0 (if any samplers)
0x20    Sampler descriptor 1 ...
...
0xXX    Texture descriptor 0 (if any textures)
...
0xXX    Attribute descriptor 0 (vertex inputs)
...
0xXX    Image descriptor 0 (if any images)
...
0xXX    End tag (0 = null descriptor)
```

Each descriptor is typically 16 or 32 bytes. For a minimal compute shader
with no textures/samplers, the resource table can be very small — just
attribute buffers and an end tag.

The resource table format is described by the `Descriptor Header` struct
in v9.xml and uses `Descriptor Type` enum values:

| Value | Type |
|-------|------|
| 0 | Sampler |
| 1 | Texture |
| 2 | Attribute |
| 3 | Depth/Stencil |
| 4 | Shader |

---

## Section 10: Minimal Compute Job for Triangle Vertex Processing

To render a triangle, we need:

### Memory Layout (all SAME_VA allocated):
```
GPU VA      Content
-------     -------
+0x000      Compute Job descriptor (128 bytes)
+0x080      Fragment Job descriptor (64 bytes)
+0x100      Local Storage descriptor (8 bytes)
+0x140      Resource table (minimal, ~64 bytes)
+0x180      FAU buffer (push uniforms, ~64 bytes)
+0x200      Vertex Shader ISA binary (compiled Valhall ISA)
+0x400      Vertex data buffer (3 vertices × position)
+0x600      Framebuffer Descriptor (FBD)
+0x800      Render Target / Color Buffer
+0xA00      Tiler Heap (if needed)
```

### Compute Job Construction:
1. **Job Header**: Type=4, Index=1, Next=0 (or point to Fragment Job)
2. **Workgroup**: size=1×1×1, count=1×1×1, task_axis=Z
3. **Shader Env**: Point to vertex shader ISA, resource table, FAU, TLS
4. **Vertex Data**: 3 vertices in attribute buffer via resource table

### Fragment Job Construction:
1. **Job Header**: Type=9, Index=2, Dependency 1 points to atom 1
2. **Payload**: Bounds = framebuffer dimensions, Framebuffer pointer → FBD
3. **FBD**: Width, Height, format=RGBA8, render target → color buffer

---

## Summary: What We Need to Build

| Component | Status | Difficulty |
|-----------|--------|------------|
| Job Header construction | ✅ Known format | Easy |
| Compute Payload (workgroup dims) | ✅ Known format | Easy |
| Shader Environment structure | ✅ Known format | Easy |
| kbase_atom_mtk submission | ✅ Working (72-byte struct) | Done |
| **Vertex Shader ISA binary** | ❌ Need Valhall ISA compiler | **Hard** |
| Resource table (attributes) | ⚠️ Format known, content TBD | Medium |
| FAU buffer (push uniforms) | ⚠️ Format known, content TBD | Medium |
| Local Storage / TLS | ⚠️ Format known, minimal for compute | Easy |
| Fragment Job + FBD | ✅ Partially working | Medium |
| Color buffer + display | ⚠️ Need to map to display | Medium |

### The Remaining Hard Problem: Valhall ISA
The only truly hard remaining piece is the **Vertex Shader ISA**. We need a
compiled Valhall binary that transforms 3 vertices. Options:

1. **EGL Capture**: Use `ioctl_spy.so` to capture Chrome's compiled shader ISA
2. **Bifrost Compiler**: Use Mesa's Bifrost compiler to compile a minimal vertex shader
3. **Hand-craft ISA**: Manually encode a minimal Valhall compute kernel
   (very tedious but possible for trivial cases)

The EGL capture approach is most reliable — it gives us known-working ISA.
