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

## Current Status

| Component | Status | Event | Key Insight |
|-----------|--------|-------|-------------|
| TILER_JOB standalone | ✅ COMPLETE | 0x1 DONE | Needs 0x4E core_req |
| TILER_JOB in chain | ✅ COMPLETE | 0x1 DONE | Needs header reinit after diagnostic runs |
| Cache Flush | ✅ COMPLETE | 0x1 DONE | Always worked |
| Fragment JC (tiler=NULL) | ✅ COMPLETE | 0x1 DONE | Works when iterating tiles directly |
| Fragment JC (tiler=active) | ❌ DATA_INVALID | 0x58 | **Fails even with valid polygon list** |
| Color output (no diagnostics) | None | 0 changed | No fragment output at all |

## Remaining Problem

The Fragment JC gets `0x58` DATA_INVALID when the tiler pointer is SET in the MFBD, even though:
- The TILER_JOB completed successfully (0x1 DONE)
- The tiler wrote a valid polygon list entry
- The polygon list is NOT empty
- The flush ensures L2 writeback is visible

The fragment works perfectly when tiler=NULL (iterates all tiles directly). This strongly suggests the MFBD configuration (`build_scratch_fbd`) needs different parameters for tiler-active mode — specifically the `params[3]` field (effective_tile_size, format flags) and possibly the pre/post frame mode configuration.

## Next Steps

1. Adjust MFBD `params[3]` for tiler-active mode — compare against Panfrost reference driver emission
2. Try different MFBD pre/post frame modes when consuming a polygon list vs iterating tiles
3. Dump the full MFBD right before Fragment JC submit and compare against known-working captured job
