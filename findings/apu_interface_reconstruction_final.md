# APU Low-Level Interface Reconstruction

**Date:** 2026-05-10
**Device:** Mali-G77-MC9 (MediaTek MT6893)
**Component:** MediaTek APU 3.0 Hardware Driver
**Status:** **RECONSTRUCTED**

---

## Overview
We have successfully mapped the direct hardware control interface for the MediaTek APU 3.0. By bypassing the proprietary `libapu_mdw.so`, we have established a pure C workflow for managing the AI accelerator's lifecycle.

## The Reconstructed Control Chain

### 1. Hardware Wake-up (`IOCTL 0x23`)
- **Raw Command:** `0xC0204123` (32 bytes)
- **Operation:** Triggers the kernel power management logic.
- **Payload:** 
    - `u32[0] = 0` (Power Op)
    - `u32[1] = 1` (Device: MDLA) or `3` (Device: VPU)
- **Result:** Successfully transitions the core from `suspended` to `active` in sysfs.

### 2. Memory Lifecycle (`IOCTL 0x21`)
- **Raw Command:** `0xC0304121` (48 bytes)
- **Operation:** Allocates hardware-visible memory via the DMA-BUF framework.
- **Payload:**
    - `u32[0] = 0` (Alloc Op)
    - `u32[6] = Size` (e.g., 4096)
- **Result:** Kernel returns a standard File Descriptor (FD) in `u32[0]`. We have verified this FD can be `mmap`'d for direct CPU R/W access.

### 3. Job Submission (`IOCTL 0x22`)
- **Raw Command:** `0xC0984122` (152 bytes)
- **Operation:** Submits a task list to the APU scheduler.
- **Structural Discoveries:**
    - **Offset 24 (0x18):** Expects a userspace pointer to a descriptor structure.
    - **Offset 104 (0x68):** Memory handle / FD location.
    - **Status Reporting:** The kernel overwrites fields starting at Offset 8 with result codes (e.g., `0x06` for invalid task).

## Technical Achievement
The "Actual Work" of probing the live kernel driver has revealed that the MediaTek APU driver is a hybrid:
- It uses standard Linux mechanisms (**DMA-BUF**, **DeviceTree**) for resources.
- It uses a proprietary, bit-packed **IOCTL** scheme for control.
- It tracks "Sessions" by userspace pointer addresses, a pattern identical to the ARM Mali GPU driver.

## Next Step: Integrated Task Execution
We are now implementing a single "Apex" test program that orchestrates this entire chain to perform a verified "No-Op" execution on the MDLA hardware.
