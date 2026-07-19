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
| Panfrost DCD research | ✅ No diff | Still 0x58 | DCD is same for both tiler modes |
| Color output | None | 0 changed | No fragment output at all |

## Remaining Problem

The Fragment JC gets `0x58` DATA_INVALID when the tiler pointer is SET in the MFBD, even though:
- The TILER_JOB completed successfully (0x1 DONE)
- The tiler wrote a valid polygon list entry (non-zero first qword)
- MFBD params[3] is now correct (Panfrost-matched)
- All three dumps (MFBD, DCD, RT descriptor) show correct values
- Panfrost reference confirms DCD setup is same for both modes

The issue must be in the **polygon list iteration path** — the Fragment JC reads the polygon list and tries to iterate tiles from it. The first qword `0x000b6f80000b6f90` is non-zero but may have a format issue that causes the GPU to dereference an invalid address.

### Most promising lead: Fragment JC MFBD pointer bit 0

The Fragment JC's MFBD pointer is `(gva + OFF_SCRATCH_MFBD) | 0x01`. Bit 0 of the MFBD address may control tile iteration mode (0=iterate all tiles, 1=follow polygon list). On this Valhall variant, the encoding might differ from Bifrost.```cpp
*(uint64_t *)(jc + 10) = (gva + OFF_SCRATCH_MFBD) | 0x01;  // current: always bit 0 = 1
```

## Next Steps

1. Try Fragment JC MFBD bit 0 = 0 (clear bit 0) — the tiler pointer at MFBD+0x38 already indicates tiler is active, so bit 0 may be redundant or inverted
2. Dump Fragment JC header before submission to compare MFBD pointer encoding between `build_scratch_fbd()` and `build_triangle_mode()`
3. Run with tiler=NULL but using the EXACT same MFBD to confirm the fragment pipeline works when only the tiler pointer differs
