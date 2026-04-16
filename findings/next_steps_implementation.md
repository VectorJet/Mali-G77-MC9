# Next Steps - From RE to Implementation

## Current Status

**RE Phase: COMPLETE**

| Component | Status |
|-----------|--------|
| libGLES_mali.so (54MB) | Shader compiler - not needed |
| libgpud.so (330KB) | RE target - command builder |
| mali_kbase (kernel) | Open source - fully documented |
| /dev/mali0 | Standard UAPI |
| Job chain format | Public in ARM headers + Panfrost |

**Kernel**: No Panfrost (`CONFIG_DRM_PANFROST=n`)
**Device**: No Mesa installed - using MediaTek blob

## What Needs to Be Built

### Critical Architecture Difference Discovered

**Panfrost uses DRM:**
- `DRM_IOCTL_PANFROST_*` via `/dev/dri/card0`
- Kernel driver: `drivers/gpu/drm/panfrost` (CONFIG_DRM_PANFROST)

**MediaTek Mali uses Mali kbase:**
- `KBASE_IOCTL_*` via `/dev/mali0` (major 10)
- Kernel driver: `drivers/gpu/arm/mali_kbase` (stock ARM driver)

**The device has:**
- `/dev/mali0` - Mali GPU (kbase ioctls)
- `/dev/dri/card0` - Display only (DSI panel), not GPU

**This means:**
- Panfrost's kmod backend won't work directly - it expects DRM
- Option A (Panfrost) needs new kmod backend for kbase
- Option B/C (libgpud.so) is actually closer to working

### Revised Approaches

```
Option A: Port Panfrost with NEW kmod backend
  - Panfrost Vulkan backend (valhall) ✓
  - NEW pan_kmod backend for kbase ioctls ✗
  - ~Harder
  
Option B: Use libgpud.so + Panfrost job chains
  - libgpud.so for memory/ioctl handling ✓  
  - Replace only job chain builder with Panfrost's pan_jc.c
  - ~Medium difficulty
  
Option C: Full custom Vulkan driver (Turnip model)
  - Android HAL stubs (from mesa-turnip patch) ✓
  - Custom kbase backend using ARM's ioctl UAPI ✓
  - Panfrost job chain format as reference
  - ~More work but full control
```
1. Build Mesa aarch64 for Android with Panfrost + Valhall
2. Whitelist GPU ID 0x09000800 in pan_device.c
3. LD_PRELOAD to override libGLES_mali.so
4. Fix issues as they appear
```

### Option B: Minimal Stub Driver

```
1. open(/dev/mali0)
2. ioctl(KBASE_IOCTL_VERSION_CHECK) 
3. ioctl(KBASE_IOCTL_SET_FLAGS)
4. ioctl(KBASE_IOCTL_MEM_ALLOC) ← get GPU VA
5. Write job chain (copy Panfrost's format)
6. ioctl(KBASE_IOCTL_JOB_SUBMIT)
```

### Option C: Hybrid (Recommended)

```
1. Use libgpud.so for everything except job chain generation
2. Replace only the job chain builder with Panfrost's code
3. This keeps MTK's memory/buffer management while using open job format
```

## Key Files to Reference

| File | Purpose |
|------|---------|
| `refs/mali-kernel/.../mali_kbase_ioctl.h` | Full ioctl UAPI |
| `refs/mali-kernel/.../mali_base_kernel.h` | base_jd_atom_v2 struct |
| Panfrost `pan_jc.c` | Job chain emitter (Mesa source) |
| `libgpud.so` | MTK's implementation (RE target) |

## Immediate Action

The fastest path is Option A:
1. Clone Mesa, enable Panfrost + Valhall
2. Add GPU ID 0x09000800 (G77 r0p1)
3. Build for aarch64 Android
4. Test with simple GL app via LD_PRELOAD

---

# IMPLEMENTATION STARTED

## src/kbase/kbase_device_test.c - Week 1

**Result: ALL I/OCTLs WORKING**

```
=== Mali kbase Device Test ===
[OK] Opened /dev/mali0 (fd=3)
[OK] VERSION_CHECK: user=11.13, kernel=11.13
[OK] SET_FLAGS: context created
[OK] DDK Version: K:r49p1-03bet0(GPL)

=== All ioctls working! ===
Device ready for GPU operations.
```

**Verified:**
- `/dev/mali0` opens successfully
- `KBASE_IOCTL_VERSION_CHECK` - kernel UAPI 11.13 matches our expectations
- `KBASE_IOCTL_SET_FLAGS` - context created
- `KBASE_IOCTL_GET_DDK_VERSION` returns "K:r49p1-03bet0" - matches libGLES_mali.so

This confirms the UAPI mapping is correct.

---

# Week 2: Job Submit Test

**Result: EINVAL on JOB_SUBMIT**

```
=== Mali kbase Job Type Test ===
[OK] GPU VA: 0x41000
[OK] mmap: 0x7cb0400000

Trying job types:
[FAIL] DEP (core_req=0x0): ret=-1
[FAIL] FS (core_req=0x1): ret=-1
[FAIL] CS (core_req=0x2): ret=-1
[FAIL] T (core_req=0x4): ret=-1
[FAIL] FS+T (core_req=0x5): ret=-1
[FAIL] DEP(jc=0): ret=-1
```

**Analysis:**
- Device opens, VERSION_CHECK, SET_FLAGS all work
- MEM_ALLOC, mmap work
- Every job type returns EINVAL with no dmesg error
- Likely cause: atom struct layout mismatch or job header format

**What's working:**
- Device open ✓
- Version handshake ✓
- Context creation ✓
- Memory allocation ✓
- GPU VA mapping ✓

**What's failing:**
- Job submission (atom format mismatch)

**Next:**
- Check exact atom struct alignment
- Try matching libgpud.so's job header format exactly
- Or check if MTK kernel expects different create_flags