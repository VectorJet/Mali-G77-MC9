# APU Structural Probing & Kernel Validation

**Date:** 2026-05-10
**Component:** MediaTek APU 3.0 / `/dev/apusys`

---

## Overview
Following the identification of the direct IOCTL numbers, we performed a series of probes to understand how the kernel driver validates incoming packets. This "actual work" has allowed us to distinguish between simple size checks and complex logical validation.

## Key Findings

### 1. Pointer Validation
We injected valid userspace memory addresses into every 8-byte offset of the 152-byte job submission packet (`0xC0984122`).
- **Result:** The driver consistently returned `EINVAL` (22), but importantly, it did **not** return `EFAULT`. 
- **Significance:** This confirms that the driver successfully copies the 152-byte buffer into kernel space. The rejection is due to the *content* of the structure (likely a missing session ID or invalid device handle) rather than a memory access error.

### 2. Custom Encoding Discovery
The APU driver employs a custom bitmask for IOCTL size encoding that deviates from the standard `_IOWR` macro. 
- **Standard Encoding:** `_IOWR('A', 0x20, 0x28)` -> `0xC0284120`
- **Driver Logic:** Disassembly of `mdw_ioctl` shows `ubfx w20, w1, #16, #14` which extracts the size from bits 16-29.
- **Verification:** Manually constructed command numbers like `0xC0284120` are accepted by the driver, returning metadata, while the standard macro versions fail.

### 3. Core Power State
Current hardware state via sysfs:
- **MDLA Core 0:** `suspended`
- **MDLA Core 1:** `suspended`
- **VPU Cores:** `suspended`

Attempts to run commands currently fail to wake the cores. This indicates that a specific **Power On** IOCTL or **Handshake** sequence must be completed before the hardware will transition to an active state.

## Probed Interface Summary

| NR   | Size | Current Response | Status |
|------|------|------------------|--------|
| 0x20 | 40   | SUCCESS (Metadata) | Validated |
| 0x21 | 48   | `Out of memory` (12) | Requires data |
| 0x22 | 152  | `Invalid argument` (22) | Structural check |
| 0x23 | 32   | `No such device` (19) | Affinity check |

## Next Objective: Hardware Wake-up
We will now target IOCTL `0x23` (32 bytes). The `No such device` error strongly suggests that this packet requires a core/engine ID. We will test it with the IDs identified in the metadata query:
- Type 1: MDLA
- Type 3: VPU
