# APU Control Chain Reconstruction Summary

**Date:** 2026-05-10
**Status:** **INFRASTRUCTURE COMPLETE**
**Device:** MediaTek APU 3.0

---

## Accomplishments
We have successfully implemented a standalone C infrastructure that manages the entire lifecycle of an APU hardware task, bypassing all proprietary blobs for everything except the final opcode encoding.

### 1. Reconstructed Sequence
Our current `apu_apex_test` successfully executes the following:
- **Session Prime:** Multi-call Handshake sequence (`IOCTL 0x20`) returns valid versioning.
- **Hardware Wake:** Power IOCTL (`IOCTL 0x23`) successfully transitions MDLA/VPU cores to `active`.
- **Resource Management:** Successfully allocates multiple DMA-BUFs (`IOCTL 0x21`) and names them using the standard kernel interface.
- **Structural Mapping:** Successfully matched the 152-byte job submission structure (`IOCTL 0x22`).

### 2. The "Role" of Buffer Naming
Discovery: The proprietary library uses `DMA_BUF_SET_NAME` to tag buffers as `APUCB0:x`. This suggests the kernel driver or the APU scheduler uses these strings to resolve buffer roles during task validation.

## The Final Hurdle: Secondary Descriptor
The `0x22` submission packet contains a userspace pointer at offset 24. 
- **Finding:** The kernel driver validates the *content* of the memory pointed to by this address.
- **Symptom:** Providing a zero-initialized buffer results in `EINVAL`.
- **Requirement:** This buffer must contain a "Subcommand Descriptor" (likely including opcodes, input/output tensors, and core affinity).

## Direct Action: Final Payload Extraction
We are now performing a final disassembly of `libapu_mdw.so` focusing exclusively on the `addSubCmd` and `packSubCmd` methods. This will provide the binary template for the secondary descriptor, completing the direct hardware control suite.
