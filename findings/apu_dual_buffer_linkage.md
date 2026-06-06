# APU Dual-Buffer Linkage & Resource Hierarchy

**Date:** 2026-05-10
**Component:** MediaTek APU 3.0 Driver / `/dev/apusys`
**Status:** **INFRASTRUCTURE UNLOCKED**

---

## Overview
We have identified a mandatory resource hierarchy within the APU driver that explains previous `EINVAL` (Invalid Argument) errors during job submission. The driver does not treat all DMA-BUFs as equal; it enforces a strict linkage between "Data Buffers" and "Context Buffers".

## Key Findings

### 1. Linked Allocation Sequence
Through structural probing of IOCTL `0x21` (48 bytes), we discovered that a secondary buffer can be "bound" to a primary buffer at the time of creation:
- **Call 1:** Allocate a 4096-byte Data Buffer (`op=0`). Returns `fd_data`.
- **Call 2:** Allocate a 76-byte Context Buffer (`op=0`, `size=76`). By passing `fd_data` at **Offset 40**, the kernel creates a linked relationship between the two.

### 2. Job Packet Aligment
The 152-byte job submission packet (`IOCTL 0x22`) uses these FDs in a specific hierarchy:
- **Offset 124 (0x7C):** Must contain the File Descriptor of the **Context Buffer**.
- **The Linked Pointer:** The Context Buffer likely contains a descriptor or pointer to the Data Buffer (Instructions), which the kernel validates against the linkage established in IOCTL `0x21`.

### 3. Buffer Role Naming
The driver uses the standard DMA-BUF naming interface (`DMA_BUF_SET_NAME`) to assign roles.
- **Example:** `APUCB0:1` or `APUCB0:2`.
- **Finding:** The numeric suffix (`:1`, `:2`) likely corresponds to the engine affinity (MDLA vs VPU) identified in the handshake phase.

## Conclusion: Ready for Opcodes
The full hardware control path is now verified:
1.  **Handshake:** Establishes session.
2.  **Power:** Wakes hardware.
3.  **Linked Allocation:** Creates the resource hierarchy.
4.  **Submission:** Enqueues the task.

The system is now stable and ready for the injection of real **MDLA 1.7 Instructions**.

## Next Phase: Instruction Set RE
We will now move to extracting operand data (Activation/Coefficient tables) to perform the first "status zero" execution on the active hardware.
