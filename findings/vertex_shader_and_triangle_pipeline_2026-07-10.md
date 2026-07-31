# Vertex Shader & Triangle Pipeline — Current Status & Next Step

**Date:** 2026-07-10
**Device:** Mali-G77-MC9 (MediaTek MT6893)
**Driver:** kbase r49 (UK Version 11.46, MTK frame_nr extension)

---

## Current Status Summary

### ✅ What Works

| Component | Status | Notes |
|-----------|--------|-------|
| Fragment shader (hand-crafted) | ✅ | 7-instruction Valhall shader, 16×16 and 64×64 100% green/red |
| MFBD + RT0 descriptors | ✅ | Single render target, RGBA8, correct swizzle/format |
| DCD (Draw/Renderer State) | ✅ | Flags 0/1, depth/stencil, blend, TLS, resources, SHADER_PROGRAM |
| SHADER_PROGRAM descriptor | ✅ | Stage=Fragment, register allocation=32Per, helpers=1 |
| Blend descriptor | ✅ | Fixed-function REPLACE on RGBA8 UNORM |
| Depth/stencil descriptor | ✅ | Depth always pass, no stencil |
| Local Storage (TLS) | ✅ | Zero TLS, valid backing VA, NO_WORKGROUP_MEM |
| Cache Flush Job (Type 3) | ✅ | L2 Clean + Shader Core LS + JM + Tiler Clean |
| Tiler Context + Heap | ✅ | 192-byte TILER_CONTEXT + 256 KiB heap backing |
| GPU_EX shader allocation | ✅ | Separate executable page for shader binaries |
| 256×256 tile writeback | ⚠️ | ~24% affected tiles — racing across 9 cores, not fixed by tiler ctx |

### 📚 Reference Files Now Available (from refs/)

| File | What We Have |
|------|-------------|
| `refs/mesa-Panfork-android/.../mali_kbase_jm_ioctl.h` | Official VERSION_CHECK, JOB_SUBMIT, POST_TERM ioctls |
| `refs/mesa-Panfork-android/.../mali_base_jm_kernel.h` | `base_jd_atom` (64 B), core_req flags, event codes |
| `refs/panfork/src/panfrost/lib/genxml/v9.xml` | **Malloc Vertex Job** (384 B at +0x5A0), Primitive, Draw, Shader Environment structs |
| `refs/panfork/src/panfrost/compiler/valhall/test/assembler-cases.txt` | Valhall ISA encodings including STORE.i128, STORE.i32, LD_VAR |
| `refs/panfork/src/panfrost/lib/kmod/pan_kmod.h` | `pan_kmod_ops` vtable (15 methods) — blueprint for kbase backend |

---

## The Geometry Gap — Why We Don't Have a Triangle

Our `shader_fbd` mode runs a **Pre Frame Shader** (MFBD word 0 = 1). This is NOT a real triangle — it runs the fragment shader once per pixel across the full framebuffer, as if every tile was covered by a polygon. There is **no vertex shader, no geometry, no tiling pass**.

A real triangle needs:

### 1. Malloc Vertex Job (Type 11, 384 bytes at +0x5A0 in v9.xml)

Structure from `refs/panfork/src/panfrost/lib/genxml/v9.xml`:

| Offset | Bytes | Section | Purpose |
|--------|-------|---------|---------|
| 0x000 | 32 | **Job Header** | Type=11, Next=0, Barrier, Index |
| 0x020 | 16 | **Primitive** | Draw mode=Triangles, Index type=U32, Index count=3 |
| 0x030 | 4 | **Instance Count** | 1 |
| 0x034 | 4 | **Allocation** | Varying buffer size |
| 0x038 | 48 | **Tiler Pointer** | → TILER_CONTEXT (we have this!) |
| 0x068 | 8 | **Scissor** | Full framebuffer scissor |
| 0x070 | 8 | **Primitive Size** | Primitive size pointers |
| 0x078 | 8 | **Indices** | Index buffer GPU VA |
| 0x080 | 128 | **Draw** | Draw parameters, vertex count, vertex shader env pointer |
| 0x100 | 64 | **Position Shader Environment** | Vertex shader ISA binding |
| 0x140 | 64 | **Varying Shader Environment** | Varying shader binding |

### 2. Valhall Vertex Shader ISA

We need a vertex shader that:
1. Reads vertex ID (implicit from the draw/primitive)
2. Outputs `gl_Position` for a full-screen triangle:
   - vid 0 → (-1, -1, 0, 1)
   - vid 1 → (3, -1, 0, 1)
   - vid 2 → (-1, 3, 0, 1)
3. Uses `STORE.i128.slot0.end` to write the position

From assembler-cases.txt, the `STORE` instruction format for vertex output is:
```
STORE.i128.slot0.end @r4:r5:r6:r7, ^r0, offset:0
Encoding: 40 00 00 38 08 44 61 78
```
This stores 4 consecutive registers (r4-r7 = vec4 position) to the vertex output stream.

### 3. Submission Chain

Current plan from tiler_context_phaseA finding:
```
Malloc Vertex (Type 11) → Cache Flush (Type 3) → Fragment (Type 9)
```
As either:
- A single atom with `next_job_ptr` chain (speculative — needs testing)
- Two atoms with slot dependency: compute on slot 1 → fragment on slot 0

---

## Next Immediate Step

**The single most impactful next step is: Write a minimal Valhall vertex shader and build the Malloc Vertex Job in `replay_egl_triangle.c`.**

Concretely:

### Step A: Add vertex shader ISA binary

From the STORE.i128 encoding in assembler-cases.txt and the LD_VAR encodings, construct a vertex shader that:
1. Reads the implicit vertex ID (available via `LD_VAR_SPECIAL` or hardcoded per-vertex data)
2. Computes screen-space position
3. Stores it with `STORE.i128.slot0.end`

For a quick win, use a compute-shader-based vertex approach (Type 4 = Compute) instead of the full Malloc Vertex Job (Type 11). Chrome was observed using Type 4 jobs for vertex processing — this avoids the complexity of the 384-byte Malloc Vertex aggregate.

### Step B: Build the Malloc Vertex Job descriptor

Add to `replay_egl_triangle.c`:
- `OFF_VERTEX_JC` at e.g. 0xD700
- `build_vertex_job()` populating the Primitive section, Tiler Pointer → existing TILER_CONTEXT, Position Shader Environment → vertex ISA + SHADER_PROGRAM
- `build_triangle_pipeline()` that chains vertex → fragment

### Step C: Submit as 2-atom batch

```
Atom 0 (slot 1, core_req=CS|CF): Vertex/Tiler job
Atom 1 (slot 0, core_req=FS|CF, pre_dep[0]=atom_id=1): Fragment job
```

### Alternative: Use a Compute job (Type 4) for vertex processing

If the Malloc Vertex Job (Type 11) proves too complex initially, Chrome's approach of using Type 4 Compute jobs for vertex processing is a viable fallback. This pairs a compute shader (that writes vertex data to memory) with the tiler and fragment jobs.

---

## Key Unknowns

1. **Vertex position store encoding**: The exact `LD_VAR_SPECIAL` for reading vertex ID and the address register setup for `STORE.i128` in vertex context need verification from the Panfrost compiler source (`bi_lower_*`, `bifrost_compile.c`).
2. **Allocation field in Malloc Vertex Job**: The varying buffer allocation size — can be 0 for minimal shader with no varyings, or needs to match the fragment shader's varying inputs.
3. **Indices vs non-indexed**: For a 3-vertex triangle, we can use non-indexed drawing (Index count = 3, no Index buffer).

## Files Referenced

- `refs/panfork/src/panfrost/lib/genxml/v9.xml` — All descriptor struct definitions
- `refs/panfork/src/panfrost/compiler/valhall/test/assembler-cases.txt` — ISA encodings
- `refs/panfork/src/panfrost/compiler/bifrost/bifrost_compile.c` — How Panfrost emits vertex shaders
- `src/kbase/replay/replay_egl_triangle.c` — Current test harness

---

## Update: Type-11 IDVS position path working (2026-07-31)

The Vulkan path now compiles vertex shaders as Valhall IDVS and can submit a
384-byte Type-11 Malloc Vertex job. The first hardware attempt returned `0x58`
because the Local Storage descriptor was at `0xE0A0`, which violates its
64-byte alignment requirement. Moving it to `0xE100` made the complete
Tiler → Flush → Fragment → Post-Flush sequence return `0x1` repeatedly.

With vkmark's real `light-basic.vert`, UBO, position attribute, 24-byte vertex
stride, and 21,516 vertices, the position-only path produces 121,600 non-zero
green pixels in an 800×600 target. This proves the compiled position shader,
attribute descriptors, transformed geometry, integrated tiler, polygon list,
fragment engine, readback, and XCB presentation path all work together.

Run the proven path with:

```sh
PANVK_EXPERIMENT_MV11_POSITION=1 PANVK_REQUIRE_COMPILER=1 vkmark --winsys xcb -b vertex:duration=2
```

Secondary IDVS varying execution also completes without a GPU fault when
enabled with `PANVK_EXPERIMENT_MV11_VARYING=1`, but the real fragment shader
currently produces an all-zero target. Varying allocation/interpolation
linkage is therefore the next isolated blocker; position-only remains the
default Type-11 experiment.
