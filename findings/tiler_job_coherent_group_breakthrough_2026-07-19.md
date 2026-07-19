# TILER_JOB Breakthrough — Complete Triangle Pipeline Journey

**Date**: 2026-07-19
**Status**: TILER_JOB completes (0x1 DONE), Fragment JC still fails (0x58)

## Summary

This document chronicles the complete journey to get a TILER_JOB (Type 7) running on the MTK r49 kbase kernel, from initial `0x58` DATA_INVALID faults to successful job completion, and the remaining Fragment JC issue.

## The core_req Journey

| core_req | Bits | Tiler Event | Fragment Event | Meaning |
|----------|------|-------------|----------------|---------|
| `0x001` | FS | `0x58` | `0x58` | DATA_INVALID — wrong slot |
| `0x004` | TILER | `0x44` | — | AFFINITY_FAULT — slot found but affinity failed |
| `0x00C` | T+CS | `0x44` | — | Same as TILER-only |
| `0x104` | T+COHERENT | `0x4003` | — | **First change** — job accepted, stopped at protection boundary |
| `0x008` | CS | `0x4003` | — | Same result on different slot |
| **`0x04E`** | **P\|T\|CS\|C** | **`0x1 DONE`** | `0x58` | 🎉 **Breakthrough — TILER_JOB completes!** |

Bits: P=PROTECTED_MODE_SWITCH (1), T=TILER (2), CS=Compute/Vertex (3), C=COHERENT_GROUP (6)

## Event Code 0x4003 Decoded

From `mali_base_jm_kernel.h`:
```
0x4003 = 0x4000 (MTK flag) | 0x03 (BASE_JD_EVENT_STOPPED)
```

- `0x03` = `BASE_JD_EVENT_STOPPED` — kernel-internal, normally never reaches userspace
- UAPI: *"STOPPED: Can't be seen by userspace, becomes TERMINATED, DONE or JOB_CANCELLED"*

MTK returns this internal code OR'd with `0x4000` signaling the job was stopped by a protection boundary check. The `PROTECTED_MODE_SWITCH` bit tells the kernel the atom's memory is in an allowed protection zone.

## Key Bit Definitions

```
BASE_JD_REQ_PROTECTED_MODE_SWITCH  = 0x02  (bit 1)
BASE_JD_REQ_T                      = 0x04  (bit 2)  — TILER slot
BASE_JD_REQ_CS                     = 0x08  (bit 3)  — Compute/Vertex slot
BASE_JD_REQ_FS                     = 0x01  (bit 0)  — Fragment slot
BASE_JD_REQ_COHERENT_GROUP         = 0x40  (bit 6)

Chrome captured atom: 0x4E = 0x02 | 0x04 | 0x08 | 0x40
```

## Breakthrough Sequence

### 1. Batch Submit → Sequential Standalone
The 3-atom batch submit was rejected by the kernel (Atom 0 got `0x58` while standalone got `0x1`). Fixed by submitting 3 sequential standalone `JOB_SUBMIT` calls with `drain_events` between each.

### 2. pre_dep Field Fix
The atoms' `pre_dep` fields referenced `atom_id=0` and `atom_id=1` which don't exist in single-atom (`nr_atoms=1`) submits. Fixed by clearing `dep_type=0`.

### 3. Job Header Reinitialization
The GPU writes back exception status to job header words 0-3 during execution. After a diagnostic run, the second submit sees stale exception data and faults `0x58`. Fixed by reinitializing the first 32 bytes before each subsequent submit:

```c
memset(vt_local, 0, 32);
vt_local[4] = (1u << 0) | (7u << 1);   /* Type=7, job valid */
*(uint64_t *)(vt_local + 6) = 0;        /* Next = 0 */
```

### 4. Diagnostics Removed
The frag-only and tiler-only diagnostics (which ran before the sequential chain) were removed to eliminate any state corruption. The Fragment JC still got `0x58`, proving the diagnostics weren't the cause.

### 5. Polygon List Dump
After the TILER_JOB completes (0x1 DONE), the polygon list at `OFF_SCRATCH_POLYLIST` was dumped:

```
polygon list after TILER_JOB (256 bytes):
  0x000: 0x000b6f80000b6f90  ← Non-zero! Tiler produced output!
  0x008-0x0f8: all zeros
```

The tiler DID bin the triangle and write valid polygon output — a single polygon descriptor. The fragment still gets `0x58` despite valid polygon data.

### 6. Standalone Fragment JC (tiler=active) → 0x58 🎯 ISOLATED!

To definitively isolate the root cause, the Fragment JC was submitted **standalone** with the tiler pointer **SET** in the MFBD (no TILER_JOB ran before it, no chain, no dependencies):

| Test | Config | Result | Implication |
|------|--------|--------|-------------|
| Fragment standalone (tiler=NULL) | MFBD+0x38 = 0 | `0x1 DONE` ✅ | Fragment works iterating tiles directly |
| Fragment standalone (tiler=active) | MFBD+0x38 = tiler_ctx | `0x58` ❌ | MFBD config with tiler=active is the root cause |
| Fragment in chain (tiler=active) | After TILER_JOB + flush | `0x58` ❌ | Same as standalone — not a chain issue |

**This definitively proves the issue is NOT about:**
- Chain submission mechanism (batch vs standalone)
- Stale polygon list (diagnostic vs fresh)
- Missing cache flush (L2 not visible)
- Job header contamination (header reinit fixed this)
- Stale diagnostic state (removed entirely)

**The issue IS the MFBD configuration when the tiler pointer is non-NULL.** `build_scratch_fbd()` was designed for tiler=NULL (iterating tiles directly) and the GPU enforces different validation rules when a real tiler context is wired in.

## MFBD params[3] Fix

Based on v9.xml genxml analysis and Panfrost reference, `params[3]` (MFBD word 11) was fixed:

| Field | Bits | Old Value | New Value | Meaning |
|-------|------|-----------|-----------|---------|
| Effective Tile Size | 9-12 | **8** | **0** | Was 4096×4096 (!), now 16×16 tiles matching tiler hierarchy |
| Render Target Count | 19-22 | **0** | **1** | Was 0 (no RTs!), now matches Panfrost `MAX2(fb->rt_count, 1)` |
| Color Buffer Allocation | 24-31 | 1 | 1 | Unchanged (correct for 16×16 RGBA8) |

**Result**: `0x58` remained unchanged — the fix was necessary but not sufficient.

## Full MFBD/DCD/RT Dump Analysis (Before Fragment JC)

All three structures were dumped right before the Fragment JC submit to verify correctness:

### MFBD (0x80 bytes)
| Offset | Value | Field | Status |
|--------|-------|-------|--------|
| 0x000 | 0x0000000000000001 | Pre Frame 0=Always, Pre Frame 1=Never, Post=Never | ✅ |
| 0x010 | gva + SAMPLELOC | Sample Locations pointer | ✅ |
| 0x018 | gva + OFF_SHADER_DCD | Frame Shader DCDs pointer | ✅ |
| 0x020 | (15,15)-(15,15) | Width=15, Height=15 (16×16 fb), Bounds | ✅ |
| 0x028 | 0x01080080000f000f | params[3]=0x01080080 (ETS=0, RT count=1, CB alloc=1) | ✅ |
| 0x030 | 0x00010000 | Z Internal Format=D24 | ✅ |
| 0x038 | non-NULL | Tiler pointer SET (tiler context) | ✅ |

### Frame Shader DCD (128 bytes at OFF_SHADER_DCD)
| Offset | Value | Field | Status |
|--------|-------|-------|--------|
| 0x000 | 0x0001ffff00000043 | Flags=(1|1|1<<6), SampleMask=0xFFFF, RT mask=0x1 | ✅ |
| 0x018 | 0x3f80000000000000 | Min Z=0, Max Z=1.0f | ✅ |
| 0x028 | depth_addr | Depth/stencil descriptor pointer | ✅ |
| 0x030 | 0x0000006ddec1f041 | Blend count=1, Blend pointer | ✅ |
| 0x038 | 0 | Occlusion = 0 | ✅ |
| 0x068 | shader_program_addr | SHADER_PROGRAM descriptor | ✅ |
| 0x070 | tls_addr | TLS descriptor | ✅ |
| 0x078 | 0 | FAU = 0 | ✅ |

### RT Descriptor (64 bytes at OFF_SCRATCH_RT)
| Offset | Value | Field | Status |
|--------|-------|-------|--------|
| 0x000 | 0x8688829904000000 | Internal Format=R8G8B8A8, swizzle=RGBA | ✅ |
| 0x020 | gva + color_off | Color buffer base address | ✅ |
| 0x028 | 0x40 | Stride = 64 (16×4) | ✅ |
| 0x030 | 0xFF0000FF | Clear color = RED | ✅ |

**All three dumps confirm everything is correctly configured.** No fields changed between initial build and submission time.

## Panfrost Reference DCD Comparison

Researched `pan_cmdstream.c` and `pan_desc.c` to compare DCD setup between tiler-active and tiler-NULL modes.

**Key finding: The DCD is NOT set up differently between modes.**

Evidence:
1. `panfrost_prepare_fs_state()` sets DCD flags based on shader properties (early Z, coverage, blend, MSAA) — no tiler branching
2. `panfrost_emit_frag_shader()` merges compiled shader partial RSD with runtime state — no tiler conditions
3. The Draw/DCD struct in v9.xml has NO tiler-related field
4. Tiler status is controlled ENTIRELY through the MFBD Tiler pointer at `MFBD+0x38`

## DCD Flags Fix (Round 1)

Based on Panfrost reference and panvk Vulkan backend research, the DCD flags were updated to match what the reference driver emits for fragment shader rendering:

| Bit(s) | Field | Old Value | New Value | Reference |
|--------|-------|-----------|-----------|-----------|
| 6 | Allow primitive reorder | **1** | **0** | Panfrost never sets this — can reorder primitives in ways incompatible with polygon list |
| 4-5 | ZS update operation | **0** | **2** (STRONG_EARLY) | Matches Panfrost arch>=6 and Vulkan `MALI_PIXEL_KILL_STRONG_EARLY` |
| 20 | Shader modifies coverage | **0** | **1** | Matches Panfrost arch>=6 and Vulkan path |
| 2-3 | Pixel kill operation | 0 | **0 (still missing)** | Panfrost always sets this (WEAK_EARLY, FORCE_EARLY, or from earlyzs.kill) |

**Result**: `0x58` remained unchanged — DCD flags are not the root cause.

## Panfrost Vulkan Backend (panvk) Research

### Tiler is ALWAYS active in Vulkan
Unlike the GLES path where `tiler_ctx->midgard.disable` can be set, the Vulkan backend always allocates and wires a tiler context — there is no "tiler=NULL" path at all.

### DCD flags in panvk (panvk_vX_cs.c:730-748)
```c
// All FOUR DCD flags are always set explicitly:
cfg.properties.allow_forward_pixel_to_kill = ...     // from shader
cfg.properties.pixel_kill_operation = earlyzs.kill;  // NEVER 0!
cfg.properties.zs_update_operation = earlyzs.update; // NEVER 0!
cfg.properties.allow_forward_pixel_to_be_killed = true;
```

### Critical remaining difference: `pixel_kill_operation`

| Path | pixel_kill | zs_update | Source |
|------|-------------|-----------|--------|
| Meta clear | `WEAK_EARLY` (1) | `WEAK_EARLY` (1) | panvk_vX_meta_clear.c |
| Meta copy | `FORCE_EARLY` (3) | `STRONG_EARLY` (2) | panvk_vX_meta_copy.c |
| Draw | `earlyzs.kill` | `earlyzs.update` | panvk_vX_cs.c |

With our shader (no depth writes, alpha=1.0, no discard path issues), the expected `pixel_kill_operation` would be `WEAK_EARLY` (1). We currently leave it at 0 (PIXEL_KILL_NONE).

### All remaining DCD differences from Panfrost

| Bits | Field | Our Value | Panfrost/Vulkan |
|------|-------|-----------|-----------------|
| 0 | Allow fwd pixel to kill | 1 | from shader |
| 1 | Allow fwd pixel to be killed | 1 | true |
| **2-3** | **Pixel kill operation** | **0** | **≥1 (always set)** |
| 4-5 | ZS update operation | 2 | earlyzs.update |
| 6 | Allow primitive reorder | 0 | not set |
| 20 | Shader modifies coverage | 1 | true |

## Current Status

| Component | Status | Event | Key Insight |
|-----------|--------|-------|-------------|
| TILER_JOB standalone | ✅ COMPLETE | 0x1 DONE | Needs 0x4E core_req |
| TILER_JOB in chain | ✅ COMPLETE | 0x1 DONE | Needs header reinit after diagnostic runs |
| Cache Flush | ✅ COMPLETE | 0x1 DONE | Always worked |
| Fragment JC (tiler=NULL) | ✅ COMPLETE | 0x1 DONE | Works when iterating tiles directly |
| Fragment JC (tiler=active) | ❌ DATA_INVALID | 0x58 | **Fails even with valid polygon list** |
| MFBD params[3] fix | ✅ Applied | Still 0x58 | ETS=0, RT count=1 fix didn't help |
| MFBD/DCD/RT dumps | ✅ All correct | Still 0x58 | All fields match Panfrost reference |
| Panfrost DCD research | ✅ No diff | Still 0x58 | DCD flags not root cause |
| DCD flags fix (round 1) | ✅ Applied | Still 0x58 | Cleared prim_reorder, added zs_update, shader_cov |
| panvk Vulkan research | ✅ Done | Still 0x58 | pixel_kill_operation still 0 (only remaining diff) |
| Color output | None | 0 changed | No fragment output at all |

## Remaining Problem

The Fragment JC gets `0x58` DATA_INVALID when the tiler pointer is SET in the MFBD. All structures have been verified correct:
- MFBD params[3] ✅ Panfrost-matched
- DCD flags ✅ Panfrost/Vulkan-matched (except pixel_kill_operation)
- RT descriptor ✅ Correct format/stride/address
- Frame Shader DCD ✅ All pointers and flags verified

**Everything checks out against the reference.** The issue must be in the polygon list iteration path — the Fragment JC reads the polygon list entries and either:
1. The format is incompatible (first qword `0x000b6f80000b6f90` may encode invalid tile count or pointer)
2. The Fragment JC's MFBD pointer bit 0 may select between tile-iteration and polygon-list mode
3. The tiler heap descriptor or tiler context has a subtle field mismatch

## Next Steps

1. Add `pixel_kill_operation=1` (bits 2-3 = WEAK_EARLY) to the DCD — the only remaining DCD difference from Panfrost
2. Try Fragment JC MFBD pointer bit 0 = 0 — clear bit 0 in the Fragment JC's MFBD address
3. Dump full polygon list (4096 bytes) to decode the tiler's output format
