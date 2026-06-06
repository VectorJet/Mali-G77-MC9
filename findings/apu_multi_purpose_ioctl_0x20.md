# APU Multi-Purpose Configuration (IOCTL 0x20)

**Date:** 2026-05-10
**Component:** MediaTek APU 3.0 / `/dev/apusys`
**Interface:** `0xC0284120` (40 bytes)

---

## Overview
We have identified that IOCTL `0x20` is not a simple one-time handshake. It is a multi-purpose query and configuration interface that the proprietary library calls multiple times during initialization. Each call appears to register or query a different aspect of the hardware session.

## Observed Sequence
Analysis of the `strace -v` logs from `test_apu_mdw` shows the following sequence on the same file descriptor:

### 1. Handshake / Version Check
- **Input:** All zeros (40 bytes).
- **Output:** `02 00 00 00 ... 1E 00 00 00 ... 0C 00 00 00`
- **Suspected Meaning:** Queries the driver for the number of available engines and general APU version.

### 2. Engine Specific Query (MDLA)
- **Input:** `u32[0]=1`, `u32[4]=1` (at offset 16).
- **Output:** `01 00 00 00 ... 02 00 00 00 ... 30 78 31 35 35 35 36 (ASCII: 0x15556)`
- **Suspected Meaning:** Specifically queries MDLA core capabilities.

### 3. Engine Specific Query (VPU)
- **Input:** `u32[0]=1`, `u32[4]=3` (at offset 16).
- **Output:** Similar metadata for the Vision Processing unit.

## Discovery: Session Tracking
The 152-byte job submission (`0x22`) requires a pointer to a descriptor at offset 24.
- **The "Key":** The kernel likely links the `fd` and the calling process to the first pointer it sees in a successful configuration call.
- **The "Trap":** If we attempt to submit a job using a pointer that hasn't been "seen" by the kernel in a previous config/query call, it returns `EINVAL`.

## Next Step: Sequential Initialization
We will update our standalone test to replicate the *entire* sequence of `0x20` calls before attempting the `0x22` submission. This will "prime" the kernel session with our userspace memory addresses.
