# APU Session Context & Pointer Validation

**Date:** 2026-05-10
**Component:** MediaTek APU 3.0 / `/dev/apusys`

---

## Overview
We have achieved bit-perfect matching of the 152-byte job submission packet (`0x22`). However, the driver continues to return `EINVAL` (Invalid Argument) for our manually constructed packets. This indicates that the kernel is performing rigorous validation of the userspace pointers contained within the structure.

## Key Discovery: The 76-byte "Session Header"
Analysis of the `strace` logs from the proprietary `libapu_mdw.so` shows a second, highly specific memory allocation call:
- **IOCTL:** `0xC0304121` (48 bytes)
- **Payload:** `u32[6] = 76` (Size 0x4C)
- **Result:** Returns a new handle (e.g., FD 5).

### Interpretation
The size 76 bytes is non-standard for a command buffer but is common for a **Session Context** or **Command Header**. The kernel likely:
1.  Allocates this 76-byte buffer.
2.  Uses it to track the "state" of the current submission.
3.  Rejects any `0x22` job submission that does not point back to a correctly initialized 76-byte context.

## Discovery: Explicit Handle Linkage
In the successful trace, the 152-byte packet contains:
- **Offset 124:** The FD of the *second* allocation (the 76-byte one).
- **Offset 24:** A pointer to a userspace structure that likely contains the FD of the *first* allocation (the 4096-byte instruction buffer).

## Next Step: Replicating the Dual-Handle Sequence
We will update our test suite to:
1.  Allocate a 4096-byte buffer.
2.  Allocate a 76-byte buffer.
3.  Place the 76-byte handle at offset 124 of the job packet.
4.  Submit to the kernel.
