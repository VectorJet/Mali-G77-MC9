# APU (NPU) Initial Breakthrough & Memory Success

**Date:** 2026-05-08
**Device:** Mali-G77-MC9 (MediaTek MT6893 / Dimensity 1200)
**Component:** MediaTek APU 3.0 (AI Processing Unit)

---

## Overview
Following the successful reverse engineering of the Mali GPU, we have applied similar methodologies to the chipset's AI accelerator. We have confirmed that the "APU" in MediaTek terminology is the functional equivalent of an NPU (Neural Processing Unit).

## Key Discoveries

### 1. Hardware Verification
- **Chipset:** MT6893 contains a hexa-core **APU 3.0**.
- **Kernel Driver:** Managed by the `apusys` module (`/dev/apusys`).
- **Device Nodes:** 
    - `/dev/apusys` (Main interface)
    - `/dev/apusys_reviser` (Likely for MMU/Address translation management)
- **Sub-Engines:** The APU is a multi-core system containing:
    - **MDLA** (MediaTek Deep Learning Accelerator)
    - **VPU** (Vision Processing Unit)
    - **EDMA** (Enhanced Direct Memory Access)

### 2. Software Stack
- **Direct Library:** `/vendor/lib64/libapusys.so` (C++ API, prone to namespace/linker issues).
- **C Wrapper:** `/vendor/lib64/libapu_mdw.so` (**SUCCESS**). This library provides a stable C-style interface (`apusysSession_*`) that bypasses C++ mangling complexities and is easier to use from standalone tests.
- **Frameworks:** MediaTek's **Neuropilot** stack and `libneuron_runtime.so` use these libraries to accelerate NNAPI and TFLite.

### 3. Breakthrough: Memory Management
We have successfully implemented a standalone C test (`src/kbase/tests/test_apu_mdw.c`) that demonstrates full control over the APU memory lifecycle:
- **Session Init:** Successfully created instances via `apusysSession_createInstance`.
- **Allocation:** Successfully allocated APU-accessible buffers via `apusysSession_memAlloc`.
- **CPU Access:** Confirmed that memory allocated via the APU driver is shared with the CPU. We successfully wrote `0xDEADBEEF` to the buffer and verified the value remained intact.

## Command Execution Status
We successfully constructed an APU command chain:
1. `apusysSession_createCmd`
2. `apusysCmd_createSubcmd` (Successfully targeted MDLA engine)
3. `apusysSession_cmdBufAlloc` (Allocated the instruction buffer)
4. `apusysSubCmd_addCmdBuf` (Linked the buffer to the engine)
5. `apusysCmd_build` (**OK**)
6. `apusysCmd_run` (**Error -5**)

**Status:** The hardware accepts the submission but returns an error because the command buffer is currently empty (filled with zeros).

## Next Steps
- **Reverse Command Format:** Analyze `libapu_mdw.so` and `libneuron_runtime.so` to determine the instruction/opcode header format.
- **Trace Real Workloads:** Use `strace` on `neuralnetworks_hal_service_shim_mtk` (PID 1448) to capture a valid hardware command packet.
- **Write Logic:** Fulfill a simple "No-Op" or "Copy" task on the MDLA engine.
