# libgpud.so Static Triage - 2026-04-15

## Artifact

- File: [refs/libgpud.so](/home/tammy/dev/experiments/Mali-G77-MC9/refs/libgpud.so)
- Source path on device: `/vendor/lib64/libgpud.so`
- Size: `330168` bytes
- SHA-256: `c7b9588c2e088692f395a310ecbfbce085e0f8dbd96089c63885e1d35046098a`

## Basic ELF facts

- ELF64 shared object
- AArch64
- Android API level target: 35
- Stripped, but still has a useful dynamic symbol table

## Why this matters

The live Chrome capture now shows real `JOB_SUBMIT`, `MEM_ALLOC`, `0x19`, and
`0x1b` activity. The remaining gap is where the real `jc` GPU virtual addresses
come from. `libgpud.so` is now the most likely place to answer that because it
sits between framework APIs and the kbase UAPI.

## Key static observations

### 0. `libgpud.so` does not import `ioctl` as a normal PLT symbol

Checks against:
- dynamic symbol table
- dynamic relocation table
- PLT exports

did **not** show a normal `ioctl@plt` import.

Implication:
- kbase access is likely routed through another internal library path or helper
- static RE should not assume a simple direct `bl ioctl@plt` pattern
- property-driven behavior and helper wrappers may be more informative than a
  naive search for `ioctl`

### 1. `libgpud.so` imports Android logging directly

Dynamic imports include:
- `__android_log_print`
- `property_get`
- `property_get_bool`
- `sync_wait`
- several `AHardwareBuffer_*` entry points

This is important because:
- it confirms `libgpud.so` already has its own debug plumbing
- it likely mediates imported/shared memory paths, not just simple heap allocs

### 2. The dynamic symbol table is still informative

Despite the binary being stripped, the exported symbol names show a broad wrapper
layer, including:
- `gpudEglCreateContext`
- `gpudEglCreateWindowSurface`
- `gpudVkAllocateMemory`
- `gpudVkBindImageMemory2`
- `gpudVkAllocateCommandBuffers`
- `gpudVkCmdClearAttachments`
- `gpudVkCreateBuffer`

This strongly suggests `libgpud.so` is not just a low-level ioctl shim. It is a
high-level instrumentation / mediation layer for GL, EGL, SurfaceFlinger, and
Vulkan-facing operations.

### 3. Strings indicate explicit job-run tracking

Interesting embedded strings include:
- `vendor.debug.gpud.run.job.dump`
- `@%s: ### detect run job: PID=%d TID=%d Counter=%u Job=%s Protected=%d`
- `vendor.debug.gpud.vk.allocate.memory.dump`
- `vendor.debug.gpud.vk.allocate.memory.clear`
- `vendor.debug.gpud.vk.map.memory.dump`

This matters because:
- `libgpud.so` appears to know about "run job" events at a semantic level
- there may be runtime properties that enable deeper vendor-side dumps without
  reverse engineering every internal function first
- Vulkan memory allocation debugging is already present in the blob, which lines
  up with the unresolved high/tagged GPU VA ranges seen in live Chrome submits

### 4. AHardwareBuffer imports likely matter for the unresolved `jc` ranges

Imports include:
- `ANativeWindowBuffer_getHardwareBuffer`
- `AHardwareBuffer_lock`
- `AHardwareBuffer_unlock`
- `AHardwareBuffer_getNativeHandle`
- `AHardwareBuffer_acquire`
- `AHardwareBuffer_release`

Combined with the live traces, this supports the working hypothesis that some
real job descriptors or related metadata may be sourced from imported/shared
memory paths rather than the simple `MEM_ALLOC -> mmap -> low GPU VA` path we
have already decoded.

### 5. `gpudInitialize` is property-heavy

Disassembly of `gpudInitialize` at `0x23fd0` shows repeated calls to:
- `property_get@plt`
- `atoi@plt`
- string copy helpers
- process-name helpers

Implication:
- a significant amount of GPUD behavior is runtime-configurable
- vendor debug toggles may be enough to expose more job/memory details without
  reversing every internal control path first
- this aligns with strings such as `vendor.debug.gpud.run.job.dump`

### 6. `gpudVkAllocateMemory` has real debug and memory-manipulation logic

Disassembly of `gpudVkAllocateMemory` at `0x32680` shows:
- logging via `__android_log_print@plt`
- logic that walks allocation-related linked structures
- conditional fill/clear/randomize behavior over allocation buffers
- explicit handling consistent with import/export and Vulkan allocation metadata

Implication:
- `gpudVkAllocateMemory` is not a thin pass-through wrapper
- this function family is a promising static-analysis target for understanding
  imported/shared memory paths that may later feed the unresolved high/tagged
  `jc` GPU VA ranges

## Working hypothesis

The low `MEM_ALLOC` results like `gpu_va=0x41000` seen in wrapped Chrome are real,
but they are probably not the main source of the high/tagged `jc` values used by
successful vendor submits.

More plausible sources for those `jc` values now include:
- imported DMA-BUF / AHardwareBuffer backed memory
- memory established earlier than the wrapped capture window
- GPU-only descriptor pools managed behind a higher-level control structure

## Recommended next steps

1. Search the disassembly for call sites that invoke `ioctl` and classify the
   constants used around them.
2. Search for references to `vendor.debug.gpud.run.job.dump` and related property
   strings to see whether vendor-side debug output can be turned on directly.
3. Focus especially on code paths near:
   - `gpudVkAllocateMemory`
   - `gpudEglCreateWindowSurface`
   - `gpudInitialize`
   - any code handling `AHardwareBuffer_*`
4. Cross-correlate any pointer-bearing helper around `0x1b` with imported/shared
   memory structures rather than assuming it is a raw descriptor pointer.
