# Mali Kernel Driver & Sysfs Analysis

## GPU Hardware Info

```
$ cat /sys/class/misc/mali0/device/gpuinfo
Mali-G77 9 cores r0p1 0x09000800
```

| Field | Value |
|-------|-------|
| GPU | Mali-G77 (Valhall) |
| Cores | 9 |
| Hardware Revision | r0p1 |
| Device ID | 0x09000800 |

## Key Finding: Software vs Hardware Versions

- **r49p1** = Software/Driver version (in libGLES_mali.so strings)
- **r0p1** = Hardware revision (from kernel sysfs)

These are separate - r49p1 is the MediaTek driver build, r0p1 is the actual silicon revision.

## Device Node Confirmation
```
/dev/mali0 -> major 10, minor 113 (MISC_MAJOR)
```

MediaTek uses misc device class (major 10) not standard Mali (major 240).

## Strace Attempt Results

Attempted to strace surfaceflinger:
- strace binary exists: `/data/data/com.termux/files/usr/bin/strace`
- `ptrace(PTRACE_SEIZE, 1216): Operation not permitted` - SELinux blocks ptrace on system_server/surfaceflinger

Alternative approaches:
1. Use `ltrace` on a simple GL app (less restricted)
2. Use Ghidra to analyze libgpud.so syscalls statically
3. Hook `/dev/mali0` with LD_PRELOAD library

## Sysfs Structure (MediaTek Mali)

```
/sys/class/misc/mali0/
├── device/
│   ├── 13000000.mali/  (device path)
│   ├── core_mask       (per-core power control)
│   ├── gpuinfo         (GPU info - readable!)
│   ├── power/
│   ├── mempool/
│   └── devfreq/       (DVFS control)
└── driver -> ../../bus/platform/drivers/mali
```

The sysfs entries show this is the standard Mali kernel driver with MediaTek integration. The driver module is at `drivers/gpu/arm/mali_kbase` with platform bus integration.

## Correlating with libgpud.so

- libgpud.so talks to `/dev/mali0` 
- mmap-based communication (no ioctls)
- ring buffer in mapped memory
- GPU ID `0x09000800` maps to Valhall G77

Cross-reference:
- Mesa Panfrost: Valhall GPU ID = `0x0900` family
- ARM Mali-G77 = First generation Valhall