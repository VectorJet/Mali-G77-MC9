# APU Direct IOCTL Breakthrough

**Date:** 2026-05-10
**Device:** Mali-G77-MC9 (MediaTek MT6893)
**Interface:** `/dev/apusys`
**Status:** **SUCCESS**

---

## Overview
We have successfully achieved direct communication with the MediaTek APU 3.0 kernel driver, bypassing all proprietary userspace libraries (`libapusys.so`, `libapu_mdw.so`). This allows for low-level hardware control and job submission similar to our previous GPU breakthroughs.

## Key Discoveries

### 1. Custom IOCTL Encoding
The APU driver uses a non-standard bit-packing for its IOCTL commands. While it uses the magic byte `'A'` (0x41), the size field is shifted and masked in a way that standard Linux headers like `_IOWR` do not produce valid results.
- **Magic:** `0x41`
- **Size Bits:** 16-29
- **NR Bits:** 0-7

### 2. Core Control Interface
Through exhaustive probing on the device, we have identified the exact structure sizes for the primary control IOCTLs:

| Command (NR) | Raw IOCTL (Hex) | Size (Bytes) | Purpose (Suspected) |
|--------------|-----------------|--------------|---------------------|
| **0x20**     | `0xC0284120`    | 40           | **Handshake / Metadata Query** |
| **0x21**     | `0xC0304121`    | 48           | Memory Mapping / Import |
| **0x22**     | `0xC0984122`    | 152          | **Job Submission** |
| **0x23**     | `0xC0204123`    | 32           | Power Control / Sync |

### 3. Successful Handshake Execution
Executing `ioctl(fd, 0xC0284120, buf)` returns a 40-byte response containing architectural metadata:
- **Sample Output:** `02 00 00 00 00 00 00 00 1E 00 00 00 0C 00 00 00 02 00 00 00 ...`
- The driver successfully populates this buffer, confirming the process has appropriate permissions and the command is valid.

## Structural Implications
The 152-byte job submission packet (`0x22`) is the most critical. Its size suggests a complex descriptor that likely contains:
- Command buffer pointers (Device Virtual Addresses).
- Core affinity masks (MDLA vs VPU).
- Memory dependency handles.

## Next Steps
- **Address Probing:** Systematically populate the 152-byte buffer with known valid memory addresses and observe which offsets trigger kernel validation (e.g., `Bad address` vs `Invalid argument`).
- **Power Trigger:** Attempt to use the 32-byte Power IOCTL (`0x23`) to wake the MDLA cores from their `suspended` state.
