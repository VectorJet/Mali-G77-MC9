# Direct APU Memory Allocation Success

**Date:** 2026-05-10
**Status:** **MEMORY GAINED**
**Mechanism:** DMA-BUF over `/dev/apusys`

---

## Breakthrough: Direct Memory Allocation
We have successfully identified the parameters for the APU memory management IOCTL (`0xC0304121`). This allows us to allocate hardware-backed memory buffers without using any proprietary MediaTek libraries.

### 1. Memory IOCTL Payload
Through strace analysis and targetted probing, we identified the following 48-byte structure layout for allocation:

| Offset | Field | Value Used | Description |
|--------|-------|------------|-------------|
| 0      | `op`  | `0`        | Suspected: `ALLOC` |
| 24     | `size`| `4096`     | Requested buffer size in bytes |

### 2. Execution Result
Running the allocation probe resulted in a successful kernel response:
- **Return Code:** `0` (Success)
- **Output:** The first 4 bytes of the buffer were overwritten with `0x00000004`.
- **Interpretation:** The value `4` is a newly created **File Descriptor** (DMA-BUF handle). This matches the behavior seen in strace where a subsequent `ioctl` was performed on `fd 4`.

## Significance
This confirms that the MediaTek APU driver uses the standard Linux **DMA-BUF** framework. This is excellent for reverse engineering because:
- We can `mmap` the returned FD to get a CPU-side pointer.
- We can pass this FD to other drivers (like the GPU) for zero-copy sharing.
- It provides a stable, kernel-enforced handle for the 152-byte job submission packet.

## Next Steps
- **Mapping Verification:** Implement a test that `mmap`s the returned FD and verifies data persistence.
- **Job Integration:** Use the allocated memory handle as the command buffer in a 152-byte job submission (`0xC0984122`).
- **Core Orchestration:** Combine the **Power Wake-up** and **Memory Allocation** into a single sequence to attempt a "No-Op" hardware execution.
