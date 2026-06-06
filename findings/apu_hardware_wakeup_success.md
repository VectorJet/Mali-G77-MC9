# APU Hardware Wake-up Success

**Date:** 2026-05-10
**Status:** **ACTIVE**
**Core:** MediaTek Deep Learning Accelerator (MDLA)

---

## Breakthrough: Triggering Core Power-On
We have successfully transitioned the APU cores from a `suspended` state to an `active` state using direct IOCTL communication. This is a critical step, as the driver rejects all job submissions while the hardware is suspended.

### 1. Power IOCTL Mapping
- **IOCTL:** `0xC0204123` (32 bytes)
- **Successful Payload:** 
    - Offset 0 (Operation): `0` (Suspected: Power On/Enable)
    - Offset 4 (Device Type): `1` (MDLA) or `3` (VPU)

### 2. Verification via Sysfs
Immediately following the execution of the probe, the hardware power status reflected the change:
```bash
# Before:
cat /sys/devices/platform/soc/soc:apusys_power/soc:apusys_power:APUMDLA/.../runtime_status
> suspended

# After apu_power_probe:
cat /sys/devices/platform/soc/soc:apusys_power/soc:apusys_power:APUMDLA/.../runtime_status
> active
```

## Memory Descriptor Analysis (IOCTL 0x21)
Probing the 48-byte memory management interface revealed:
- **Type 3 Operation:** Triggers `ENOMEM` (Out of memory) even with empty buffers.
- **Interpretation:** This is likely the `memAlloc` or `memImport` path. The `ENOMEM` error suggests the driver is attempting to fulfill a request (e.g., allocating a kernel tracking struct) but is failing due to invalid size or missing flags in other fields of the 48-byte packet.

## Conclusion
We now have the ability to wake the AI hardware at will. The remaining hurdle is providing a valid memory handle in the 152-byte job packet. We will now focus on successfully importing a DMA-BUF into the APU session using the 48-byte interface.
