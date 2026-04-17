# Mali-G77 Shader Replay Progress

**Date:** 2026-04-17

## Summary

Successfully captured **both** compute and fragment shader ISA binaries from vkmark, and replayed the compute shader with a SUCCESS result. Fragment shader replay fails with `INSTR_INVALID_ENC` (0x59) — the ISA binary contains embedded GPU VA pointers from vkmark's address space that need handling.

## What Works

### Compute Shader Replay ✅
- Captured 4048-byte compute/vertex shader ISA from vkmark
- Built a Compute Job (Type 4) descriptor with proper Shader Environment
- Submitted as atom with `core_req=0x4a` (CS + CF + COHERENT_GROUP)
- **GPU returned event code 0x1 (SUCCESS)**
- `exception_status` changed from 0 → 1, confirming hardware execution
- This is the first time a real Valhall shader binary has been executed via our bare-metal pipeline

### Enhanced ioctl_spy ✅
- Added fragment shader ISA capture by following DCD → Shader pointer chain
- Captures fragment FAU, TLS, resources, and ISA from the DCD's Shader Environment (offsets 0x60-0x78)
- Changed capture dir to `~/mali_capture/` for Termux write access

## What Fails

### Fragment Shader Replay ❌ (Event 0x59 = INSTR_INVALID_ENC)
The fragment shader ISA (496 bytes) contains **embedded GPU VA pointers** from vkmark's SAME_VA address space (`0x5effe1xxxx` range):

| ISA Offset | Embedded Value     | Likely Purpose |
|------------|-------------------|----------------|
| 0x20       | `0x5effe1b940`    | Sampler/texture descriptor |
| 0x48       | `0x5effe1ffe0`    | Internal state |
| 0x100      | `0x5effe20000`    | Texture/buffer |
| 0x140      | `0x5effe1b940`    | Same as 0x20 |
| 0x1D8      | `0x5effe1fa00`    | Resource |
| 0x1E8      | `0x5effe1fd01`    | Resource (with tag bit) |

These are baked into the shader binary's constant/literal pool. The GPU tries to execute them as instructions or dereference them, causing INSTR_INVALID_ENC.

The fragment FAU also contains a pointer: `FAU[0] = 0x5effe1b940`.

### Standalone Fragment (core_req=0x003) ❌ (Event 0x4003 = JOB_INVALID)
The simpler `core_req=0x003` pattern from our earlier WRITE_VALUE-style fragment tests doesn't work here because the job descriptor format is different (Type 9 with full FBD chain).

## Captured Files (v2 — with fragment ISA)

From `~/mali_capture/` via enhanced spy:

| File | Size | Description |
|------|------|-------------|
| `002_atom1_compute_shader_isa.bin` | 4048 | Compute/vertex shader ISA ✅ |
| `002_atom1_compute_fau.bin` | 32 | Compute FAU (4 entries) |
| `002_atom1_compute_resources.bin` | 32 | Compute resource table |
| `002_atom1_compute_thread_storage.bin` | 32 | Compute TLS descriptor |
| `002_atom2_frag_shader_isa.bin` | 496 | **Fragment shader ISA** 🆕 |
| `002_atom2_frag_shader_dcd.bin` | 128 | Fragment Draw Call Descriptor |
| `002_atom2_frag_fbd.bin` | 256 | Framebuffer Descriptor |
| `002_atom2_frag_fau.bin` | 8 | Fragment FAU (1 entry = pointer) |
| `002_atom2_frag_resources.bin` | 64 | Fragment resource table |
| `002_atom2_frag_tls.bin` | 32 | Fragment TLS descriptor |

## New Progress: Fragment ISA Relocation Pattern ✅

Comparing **five independent fragment shader captures** (`002`, `004`, `006`, `008`, `010`) shows the fragment ISA is not arbitrary junk — it contains a **small fixed set of relocations** whose relationships are stable across runs.

### Stable embedded-pointer offsets in fragment ISA

| ISA Offset | Captured Value Pattern | Stable Relationship |
|------------|------------------------|---------------------|
| `0x20`     | `0x5eff??5940` / `0x5eff??b940` | exactly `FAU[0]` |
| `0x48`     | `0x5eff??9fe0` / `0x5eff??ffe0` | `fragment_shader_isa + 0x20` |
| `0x100`    | `0x5eff??a000` / `0x5eff??0000` | `fragment_shader_isa + 0x40` |
| `0x140`    | same as `0x20` | exactly `FAU[0]` |
| `0x1D8`    | `0x5eff??9a00` / `0x5eff??fa00` | `fragment_fau_ptr - 0x700` |
| `0x1E8`    | `0x5eff??9d01` / `0x5eff??fd01` | `fragment_fau_ptr - 0x3ff` (tagged low bit) |

This is important because it means the fragment shader likely **can be relocated mechanically** instead of requiring full ISA disassembly.

### Cross-capture invariants

Across all v2 captures:
- `ISA[0x20] == ISA[0x140] == FAU[0]`
- `ISA[0x48] == DCD.shader_isa + 0x20`
- `ISA[0x100] == DCD.shader_isa + 0x40`
- `ISA[0x1D8] == DCD.fau - 0x700`
- `ISA[0x1E8] == DCD.fau - 0x3ff`

So the fragment failure is narrower than first thought:
- not “random embedded pointers everywhere”
- but a **known six-site relocation set**, plus the external object referenced by `FAU[0]`

### What this implies

The replay path should patch at least:
1. `DCD+0x60/+0x68/+0x70/+0x78` — already done
2. `FAU[0]` — still points at vkmark VA today
3. Fragment ISA words at offsets `0x20`, `0x48`, `0x100`, `0x140`, `0x1D8`, `0x1E8`

That gives us a concrete **Option A'**:
- allocate a small “fragment aux” block in replay memory
- repoint `FAU[0]` to it
- patch the six relocation sites using the stable formulas above
- rerun and see whether `INSTR_INVALID_ENC` turns into a different, more informative fault or actual pixel output

## Architecture Understanding

### Valhall Shader ISA Structure
The captured ISA is **not** a flat instruction stream. It's a structured binary with:
- **Clause headers** at 64-byte boundaries (16 bytes header + instructions)
- **Embedded literal/constant pools** containing absolute GPU VA pointers
- **Relocation-like entries** that reference textures, samplers, and internal driver state

The compute shader ISA (4048 bytes) also has embedded `0x5e...` pointers but they happen to be in unused clauses or the shader execution path doesn't dereference them — hence it succeeds.

The fragment shader ISA (496 bytes) is smaller and the GPU apparently hits the embedded pointers during execution, causing the invalid encoding fault.

### DCD (Draw Call Descriptor) Layout
The DCD is a 128-byte structure at FBD+0x18. It contains:
- **+0x00**: Config flags (0x228)
- **+0x04**: Scissor bounds (0xffff = full screen)
- **+0x1C**: Depth value (1.0f)
- **+0x28**: Color buffer GPU VA pointer
- **+0x44**: FAU count (1 entry)
- **+0x60**: Resources pointer (Shader Environment)
- **+0x68**: Fragment Shader ISA pointer
- **+0x70**: Thread Storage pointer
- **+0x78**: FAU pointer

### FBD Layout (256 bytes)
- **+0x00-0x07**: Local Storage config (TLS size=1)
- **+0x08**: Commit pages
- **+0x10**: TLS base pointer (needs patching)
- **+0x18**: DCD pointer (needs patching)
- **+0x20-0x2F**: Tiler config (dimensions, heap pointer)
- **+0x80**: Framebuffer parameters (width/height encoded)

## Memory Layout (replay_triangle.c)

```
Offset     Content                    Size
0x0000     Compute Job (Type 4)       128 bytes
0x0080     Fragment Job (Type 9)       64 bytes
0x0100     TLS descriptor              32 bytes
0x0140     TLS scratch                4096 bytes
0x1140     Resources (compute)         32 bytes
0x1180     FAU (compute)               32 bytes
0x1200     Fragment DCD               128 bytes
0x1280     Fragment FAU                32 bytes
0x1300     Fragment resources          64 bytes
0x1340     Fragment TLS desc           32 bytes
0x1380     Fragment TLS scratch       4096 bytes
0x3000     Compute Shader ISA         4096 bytes
0x4000     Fragment Shader ISA        4096 bytes
0x5000     FBD                         256 bytes
0x6000     Color buffer (32×32)       4096 bytes
0x7000     Tiler heap scratch         4096 bytes
```

## Next Steps

### Option A: Patch embedded pointers in fragment ISA
The `0x5e...` values are now known to live at **six stable offsets** and follow fixed relocation formulas. The minimum patch set is:
- `ISA[0x20]  = new_fau0_target`
- `ISA[0x48]  = new_frag_isa + 0x20`
- `ISA[0x100] = new_frag_isa + 0x40`
- `ISA[0x140] = new_fau0_target`
- `ISA[0x1D8] = new_frag_fau - 0x700`
- `ISA[0x1E8] = new_frag_fau - 0x3ff`
- `FAU[0]     = new_fau0_target`

What remains unknown is the **contents** expected at `new_fau0_target`, not the relocation pattern itself.

### Option B: Capture a simpler fragment shader
Use a trivially simple Vulkan shader (e.g., solid color output, no textures) that produces an ISA without embedded texture/sampler pointers.

### Option C: Write minimal Valhall ISA by hand
Construct a minimal fragment shader in raw Valhall ISA that just outputs a constant color. Valhall `ATEST` + `BLEND` instructions could write a solid color without needing any descriptor pointers.

### Option D: Full job chain capture
Instead of reconstructing the job chain from parts, capture the **entire** job descriptor memory (compute job + fragment job + all pointed-to structures) as one contiguous block, then replay it with a single base address relocation.

## Test Result: Mechanical Relocation Patch Attempt ❌

Implemented relocation patching in `src/kbase/replay_triangle.c` and tested on device via Termux.

### Applied patches
Replay now rewrites:
- `FAU[0]`
- `ISA+0x20`
- `ISA+0x48`
- `ISA+0x100`
- `ISA+0x140`
- `ISA+0x1D8`
- `ISA+0x1E8`

using the stable cross-capture formulas and a fresh SAME_VA auxiliary block.

### Observed result
Device run still reports:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### Conclusion from this test
Mechanical relocation alone is **not sufficient**. The remaining issue is likely one of:
1. `FAU[0]` must point to a **specific captured descriptor/state block**, not just valid memory
2. the fragment resources table also embeds a dependent pointer/descriptor that must be rebuilt
3. there are additional relocation semantics inside the fragment binary beyond the six obvious pointer literals
4. the replayed fragment job is still missing some state normally produced by an earlier tiler/vertex stage

This narrows the problem nicely: we ruled out the simple “just repoint the six addresses” hypothesis.

## New Progress: Captured FAU[0] Target + Auxiliary Window ✅

Extended `src/kbase/ioctl_spy.c` to capture:
- `atomN_frag_fau0_target.bin` — 512 bytes at the block referenced by `FAU[0]`
- `atomN_frag_aux_window.bin` — 4 KB window around `frag_fau - 0x800`

Compiled the updated `ioctl_spy.so` **on Termux itself** and tested it by running:
- `DISPLAY=:0 LD_PRELOAD=/data/data/com.termux/files/home/ioctl_spy.so vkmark -s 64x64`

The new captures were successfully produced on device for multiple frames (`002`, `004`, `006`, `008`, `010`).

### What the new captures show
For frame `002`:
- `frag_fau0_target` is **not zero data**; its first 16 bytes are:
  - `0x0000007f00004300`
  - `0x0000005effe1b483`
- the wider aux window contains the **entire known fragment replay cluster**:
  - DCD-like pointers at offsets around `0x2e0..0x2f8`
  - the fragment ISA pointer region
  - the exact embedded ISA literals reappearing later in the window:
    - `+0x6e0 = 0x5effe1b940` (`FAU[0]`)
    - `+0x708 = 0x5effe1ffe0`
    - `+0x7c0 = 0x5effe20000`
    - `+0x800 = 0x5effe1b940`
    - `+0x898 = 0x5effe1fa00`
    - `+0x8a8 = 0x5effe1fd01`

This strongly suggests the fragment shader is part of a **larger captured state blob**, not a standalone relocatable ISA.

## Test Result: Replay Fragment State Cluster ❌

Updated `src/kbase/replay_triangle.c` to stop treating the fragment shader as a standalone binary.

### New replay strategy
The replay now loads:
- `frag_aux_window.bin` (4 KB) at a dedicated cluster base
- `frag_fau0_target.bin` (512 B) into a dedicated aux-target block

and reconstructs the fragment state using the observed cluster-relative layout from frame `002`:
- cluster DCD at `cluster + 0x280`
- cluster ISA at `cluster + 0x6c0`
- cluster FAU at `cluster + 0x800`
- cluster resources pointer at `cluster + 0x7c4`

Then it patches:
- DCD color / resources / ISA / TLS / FAU pointers
- the six stable ISA relocations
- `FAU[0]` to the loaded aux-target block

### On-device Termux test result
Compiled and ran on Termux itself.

Observed result:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### What this means
Even replaying the **captured fragment state cluster** is still not enough. So the missing piece is likely one of:
1. more external state referenced from inside `frag_fau0_target`
2. additional nearby memory beyond the 4 KB aux window
3. some requirement that this fragment state be produced by a real preceding tiler/vertex path rather than copied into memory after the fact
4. another pointer-tagging / descriptor-format rule not yet reconstructed

## New Progress: FAU[0] Target Page + Internal Self-References ✅

Extended capture again to gather more of the closure around `FAU[0]`:
- `frag_fau0_target_page.bin` — full 4 KB page containing the `FAU[0]` target
- `frag_fau0_ptr1.bin` — 256-byte block pointed to by `FAU0_target[1]`
- `frag_big_window.bin` — 16 KB window around the fragment aux cluster

### Important structure discovered
The `FAU[0]` target is **not** an isolated 512-byte blob. It lives inside a page at approximately:
- old page base: `0x5effe1b000`
- target address: `0x5effe1b940` (`page + 0x940`)

That page contains many **self-references back into the same page**:
- `0x...b040`
- `0x...b100`
- `0x...b280`
- `0x...b400`
- `0x...b440`
- `0x...b7c8`
- `0x...b840`
- `0x...b900`

This means `FAU[0]` depends on a **page-local object graph**, not just a single target pointer.

## Test Result: Replay with FAU[0] Full Page Relocation ❌

Updated `src/kbase/replay_triangle.c` again to:
- allocate more SAME_VA space (20 pages)
- load `frag_fau0_target_page.bin`
- relocate all 64-bit words inside that page that point back into the original page range `0x5effe1b000..0x5effe1bfff`
- set `FAU[0] = new_page_base + 0x940`
- keep using the fragment cluster replay for DCD/ISA/FAU/resources

### On-device Termux test result
Compiled and ran on Termux itself.

Observed result is still:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### What this rules out
We have now ruled out all of these as sufficient fixes by themselves:
1. patching only the six obvious ISA relocations
2. patching `FAU[0]` to dummy valid memory
3. replaying the 4 KB fragment cluster
4. replaying the full `FAU[0]` page with internal self-references relocated

## New Progress: Recursive Pointer-Closure Capture ✅

Chose **Option 1** and extended `src/kbase/ioctl_spy.c` to recursively capture pointer targets from the fragment closure.

### New recursive capture behavior
When a fragment FAU is seen, the spy now:
1. captures `frag_fau0_target` and the full containing page
2. scans the first 8 qwords of `frag_fau0_target`
3. scans the entire 4 KB `frag_fau0_target_page`
4. captures up to 16 unique readable target pages plus 256-byte heads for the individual pointer sites

### Termux build/test
As requested, the binary was compiled **on Termux itself**:
- copied `ioctl_spy.c` to `/data/data/com.termux/files/home/`
- built `ioctl_spy.so` with Termux `clang`
- ran `vkmark` under `LD_PRELOAD` with outputs in `/data/data/com.termux/files/home/`

### New captures produced on device
For frame `002` we now have additional recursive artifacts such as:
- `002_atom2_frag_fau0_page0.bin`
- `002_atom2_frag_fau0_page_page0.bin` … `page7.bin`
- `002_atom2_frag_fau0_page_ptr46.bin`
- `002_atom2_frag_fau0_page_ptr139.bin`
- `002_atom2_frag_fau0_page_ptr146.bin`
- `002_atom2_frag_fau0_page_ptr162.bin`
- `002_atom2_frag_fau0_page_ptr172.bin`
- `002_atom2_frag_fau0_page_ptr275.bin`
- `002_atom2_frag_fau0_page_ptr285.bin`
- `002_atom2_frag_fau0_page_ptr497.bin`

### What the recursive captures reveal
One especially interesting node is `frag_fau0_page_ptr275` (captured from qword index 275 in the FAU0 page). Its 256-byte head contains another structured object referencing:
- `0x5effe1c080`
- `0x5effe1eb88`
- `0x5effe1ed00`
- `0x5effe1b040`
- `0x5effe1ed20`
- `0x5effe1e500`

So the fragment closure definitely extends beyond just:
- the immediate 4 KB cluster
- the FAU0 target blob
- the FAU0 containing page

There is a **second-tier graph** of state objects under the FAU0 page.

## Test Result: Replay with Second-Tier Page (`e1e000`) ❌

Continued Option 1 by teaching `src/kbase/replay_triangle.c` to load and relocate a selected second-tier page from the recursive closure.

### New replay logic
Replay now also loads:
- `002_atom2_frag_fau0_page_page5.bin`

This page corresponds to the old GPU VA range around:
- `0x5effe1e000 .. 0x5effe1efff`

The new code now:
- allocates 24 pages total
- maps the FAU0 page at a dedicated replay page
- maps the second-tier `e1e000` page at another replay page
- relocates pointers in the FAU0 page from old `e1b000` → new page
- relocates pointers in the FAU0 page from old `e1e000` → new second-tier page
- relocates pointers in the second-tier page back into both replayed page ranges
- keeps fragment-cluster replay + ISA relocation patching on top

### On-device Termux test result
Built on Termux itself and ran as root.

Observed result is still:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### What this adds to our understanding
We now know that even replaying:
- the fragment cluster
- the FAU0 target page
- and one real second-tier state page (`e1e000`)

is still not enough.

That strongly suggests one of two things:
1. multiple second-tier / third-tier pages must be replayed together as a larger closure
2. the fragment pipeline depends on state generated dynamically by the original driver / tiler path, not just static memory snapshots

## New Progress: Multi-Page Recursive Closure Replay ❌

Improved the capture tooling first:
- `src/kbase/ioctl_spy.c` now emits **named page captures by original GPU VA**, e.g.
  - `002_atom2_frag_fau0_page_page_5effe1e000.bin`
  - `002_atom2_frag_fau0_page_page_5efffc1000.bin`
  - `002_atom2_frag_fau0_page_page_5efffc4000.bin`
  - `002_atom2_frag_fau0_page_page_5efffe2000.bin`
  - `002_atom2_frag_fau0_page_page_5efffe6000.bin`
  - `002_atom2_frag_fau0_page_page_5effffe000.bin`
  - `002_atom2_frag_fau0_page_page_5effe3b000.bin`

This made it practical to replay a larger, explicitly identified closure instead of guessing which anonymous `pageN` file mapped to which old GPU VA.

### New replay logic in `replay_triangle.c`
The replay now loads and relocates **eight fragment-related pages together**:
- `e1b000`  (`frag_fau0_target_page`)
- `e1e000`
- `fc1000`
- `fc4000`
- `fe2000`
- `fe6000`
- `ffe000`
- `e3b000`

For each loaded page, the code relocates pointers to every other loaded page, preserving the old cross-page VA graph inside the replay buffer.

### On-device Termux test result
Compiled on Termux itself and ran as root.

Observed result remains:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### What this now rules out
We have now ruled out as sufficient:
1. six-site ISA relocation patching
2. dummy `FAU[0]`
3. fragment cluster replay alone
4. `FAU[0]` page replay alone
5. one second-tier page (`e1e000`)
6. a broader **eight-page recursive closure** covering the main named targets discovered so far

## Key Insight
The compute shader **works**. The fragment problem persists even after replaying a substantial, explicitly named pointer closure. That pushes the likely root cause toward one of:
- still-missing driver state outside the captured closure,
- descriptor/tag semantics not preserved by raw memory replay,
- or a requirement for state to be produced by a real upstream tiler/driver path rather than snapshotted memory.

## New Progress: Captured Contiguous Fragment Arena Pages ✅

To push beyond pointer-followed islands, `src/kbase/ioctl_spy.c` was extended again to sweep a **contiguous page neighborhood** around the fragment FAU area.

### What changed
For each fragment job, the spy now captures readable pages from roughly:
- `center_page - 8 * 4KB`
- through
- `center_page + 8 * 4KB`

with filenames keyed by the original GPU VA, e.g.:
- `002_atom2_frag_arena_page_5effe18000.bin`
- `002_atom2_frag_arena_page_5effe19000.bin`
- ...
- `002_atom2_frag_arena_page_5effe28000.bin`

This captures the **original local arena around the fragment FAU / resources / ISA region** instead of only following explicit pointers.

### Termux build/test
As requested, this was compiled **on Termux itself**:
- `clang -shared -fPIC -O2 -Wall -Wextra -o ioctl_spy.so ioctl_spy.c -ldl -llog`

and tested by rerunning `vkmark` with `LD_PRELOAD` on-device.

### Why this matters
The new arena sweep shows that for frame `002`, the fragment state occupies a dense local VA neighborhood from about:
- `0x5effe18000` to `0x5effe28000`

That means the next replay attempt does **not** need to guess isolated pages only from pointer targets. We can now try reconstructing a much more faithful **contiguous local fragment arena** in replay memory.

## Test Result: Replay Full Local Fragment Arena (17 pages) ❌

Implemented the next arena-based replay step in `src/kbase/replay_triangle.c`.

### New replay strategy
Instead of stitching together only selected secondary pages, replay now loads the **entire contiguous local fragment arena** captured around the fragment FAU region for frame `002`:
- old VA range: `0x5effe18000 .. 0x5effe28fff`
- total: **17 pages**

The new code:
- allocates 28 pages total in replay memory
- loads all 17 captured arena pages into a contiguous replay arena
- relocates all page-local references from each old arena page to the corresponding replay page
- computes `FAU[0]` from the original old target offset (`0x5effe1b940`) into the new arena base
- keeps the fragment cluster replay plus six-site ISA relocation patching on top

### On-device Termux test result
Built on Termux itself and ran as root.

Observed result remains:
- Compute atom: `0x1` (**SUCCESS**)
- Fragment atom: `0x59` (**INSTR_INVALID_ENC**)
- Standalone fragment fallback: `0x4003` (**JOB_INVALID**)
- Color buffer: unchanged

### What this rules out
This is the strongest replay attempt so far. It rules out the hypothesis that failure was simply caused by missing nearby pages inside the local fragment arena. Even with the **full 17-page contiguous local arena** replayed and internally relocated, the fragment shader still faults with `INSTR_INVALID_ENC`.

## Key Insight
The compute shader **works**. The fragment failure now appears to require something beyond the local captured arena and beyond the previously identified pointer closure. The most plausible remaining causes are:
1. state outside the captured local arena and pointer-followed closure
2. driver/runtime descriptor semantics that are not preserved by raw memory relocation
3. state that must be produced dynamically by the real upstream tiler/driver path

## Simpler-Fragment Pivot: Initial Results ⚠️

Started exploring simpler fragment paths instead of scaling closure replay further.

### Attempt 1: Vendor EGL/GLES triangle dumper
Created `src/kbase/egl_dumper_vendor.c` to try loading the vendor Mali GLES/EGL implementation directly and render a trivial solid-color triangle.

Result on Termux:
- compile succeeded on-device
- runtime failed with Android linker namespace restrictions:
  - `dlopen vendor mali failed: ... not accessible for the namespace "(default)"`

This is blocked only for binaries launched inside the Termux app context.

Updated path forward:
- push `src/kbase/egl_dumper_vendor.c` to `/data/local/tmp/`
- compile it there as a standalone binary with Termux `clang`
- run `/data/local/tmp/egl_dumper` via `su -c` from an adb shell or root shell

That root-launched `/data/local/tmp` binary should execute outside the Termux app linker namespace, allowing `dlopen("/vendor/lib64/libGLES_mali.so", ...)` to succeed and making the minimal 3-vertex solid-color shader dump viable again.

### Follow-up: direct `LD_PRELOAD` spy injection succeeded
Ran the standalone root binary with:
- `LD_PRELOAD=/data/local/tmp/ioctl_spy.so /data/local/tmp/egl_dumper`

Observed on the real vendor driver:
- one intercepted `JOB_SUBMIT` containing 4 atoms
- **Compute atom** captured:
  - Job Type `4`
  - shader ISA at `0x5effffe000`
  - captured:
    - `compute_shader_isa.bin` (4048 bytes)
    - `compute_fau.bin` (32 bytes)
    - `compute_thread_storage.bin` (32 bytes)
    - `compute_resources.bin` (32 bytes)
- **Fragment atom** captured:
  - Job Type `9`
  - FBD at `0x5effe9e180`
  - DCD at `0x5effe9e000`
  - fragment shader ISA at `0x5effe9e440`
  - captured:
    - `frag_fbd.bin` (256 bytes)
    - `frag_shader_dcd.bin` (128 bytes)
    - `frag_shader_isa.bin` (496 bytes)
    - `frag_fau.bin` (8 bytes)
    - `frag_tls.bin` (32 bytes)
    - `frag_resources.bin` (64 bytes)
    - extra FAU closure / arena pages

Important nuance:
- this minimal vendor GLES path **did not expose a separate Type 7 tiler atom** in the intercepted submit
- so the direct-preload route successfully captured the compute + fragment side of the pipeline, but not the full explicit "holy trinity" as a 4/7/9 atom trio

Artifacts were pulled into:
- `captured_shaders/egl_dumper_root_preload_2026-04-17/`

### Attempt 2: vkmark `clear` benchmark capture
Ran:
- `vkmark -s 64x64 -b clear:duration=1`

under `ioctl_spy.so` on-device.

Observed capture pattern:
- fragment jobs are submitted
- `frag_fbd`, `frag_shader_dcd`, and `frag_tls` are captured
- **no fragment shader ISA / FAU / resources are present**

This suggests `clear` uses a special clear-style fragment path without a normal shader environment.

### Attempt 3: Replay clear-style fragment job
Implemented `src/kbase/replay_clear.c` to replay a clear-style fragment job using the captured `clear` benchmark descriptor shape.

On-device Termux test result:
- with `core_req=0x003` → event `0x4003` (**JOB_INVALID**)
- with `core_req=0x049` → event `0x59` (**INSTR_INVALID_ENC**)
- color buffer unchanged in both cases

### What this means
The simpler-fragment pivot is promising conceptually, but the first two practical routes are not yet enough:
1. direct vendor GLES capture is blocked by linker namespace restrictions
2. vkmark `clear` captures a nonstandard fragment path that is not trivially replayable either

At this point, the most promising next step is probably to capture a **minimal Vulkan triangle pipeline** (trivial vertex + solid-color fragment) using the standard Vulkan loader path that Termux apps can access, rather than relying on vkmark's more complex scenes or Android-blocked GLES vendor loading.
