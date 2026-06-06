# APU Full-Sequence Handshake

**Date:** 2026-05-10
**Component:** MediaTek APU 3.0 / `/dev/apusys`
**Status:** **SEQUENCE VERIFIED**

---

## Overview
We have successfully reproduced the exact multi-call initialization sequence used by the proprietary MediaTek libraries. This sequence "primes" the kernel driver and confirms the hardware is ready for job submission.

## Verified Sequence
The following sequence of IOCTLs now returns `0` (Success) in our standalone C tests:

1.  **Handshake 1 (General Query):** `ioctl(fd, 0xC0284120, zeros)`
    - **Confirmed Output:** General APU Gen 2, 30 Engines reported.
2.  **Handshake 2 (MDLA Metadata):** `ioctl(fd, 0xC0284120, {op=1, type=1})`
    - **Confirmed Output:** Returns MDLA version string `"0x15556"`.
3.  **Power Trigger:** `ioctl(fd, 0xC0204123, {op=0, type=1, val=1})`
    - **Confirmed Output:** Core status transitions to `active`.
4.  **Handshake 3 (Session Init):** `ioctl(fd, 0xC0284120, {op=2, type=1})`
    - **Confirmed Output:** Returns `0` (Success).

## The Missing Link: `apusys_mem_info`
Despite the successful handshake, the 152-byte job submission still returns `EINVAL`. 
- **Analysis:** Disassembly of the `submit` method reveals a call to `memGetInfoFromHostPtr` immediately before the `ioctl`.
- **Finding:** The job packet is not just composed of raw handles; it includes a metadata structure returned by the driver's memory management unit. This structure likely contains the **Device Virtual Address (DVA)**.

## Next Step: Metadata Struct Mapping
We will now write a tool to call `apusysSession_memGetInfoFromHostPtr` from `libapu_mdw.so` and dump the resulting 48-byte structure. This will reveal the exact format the kernel expects in the job submission packet.
