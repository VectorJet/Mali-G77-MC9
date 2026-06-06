# MediaTek APU 3.0 Control Suite

This directory contains pure C tools for interacting directly with the MediaTek APU 3.0 hardware via `/dev/apusys`. These tools bypass proprietary libraries.

## Tool Manifest

### 1. `power_ctrl.c`
- **Purpose:** Wakes/Suspends the MDLA and VPU hardware cores.
- **Verification:** Check `/sys/devices/platform/soc/soc:apusys_power/soc:apusys_power:APUMDLA/.../runtime_status`.

### 2. `mem_mgmt.c`
- **Purpose:** Allocates hardware-visible DMA-BUFs and maps them to userspace.
- **Key Logic:** Uses IOCTL `0xC0304121`.

### 3. `direct_handshake.c`
- **Purpose:** Executes the multi-call handshake sequence (`IOCTL 0x20`) to query hardware metadata and session IDs.

### 4. `job_submit.c` (Apex)
- **Purpose:** Orchestrates the full sequence (Handshake -> Power -> Alloc -> Submit) to trigger hardware execution.
- **Packet Size:** 152 bytes.

### 5. `fuzzer.c`
- **Purpose:** Low-level IOCTL fuzzer for identifying new driver capabilities.

## Quick Start (Device)
```bash
# Compile
gcc -o job_submit job_submit.c

# Run as root
su -c ./job_submit
```
