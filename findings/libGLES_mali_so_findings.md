# Mali-G77 libGLES_mali.so Analysis Findings

## 1. Exported Symbols (First 50)

The binary exports a mix of Vulkan, OpenCL, and initialization functions:

| Symbol Type | Examples |
|-------------|----------|
| Vulkan (gpudVk*) | `gpudVkCreateBuffer`, `gpudVkCmdDraw`, `gpudVkCmdBindPipeline`, `gpudVkCmdPipelineBarrier` |
| OpenCL | `clBuildProgram`, `clGetProgramBuildInfo` |
| Initialization | `gpudInitialize`, `gpudIsEnabled` |
| Debug/Dump | `gpudVkDumpFramebuffer`, `gpudIsVkFramebufferDumpEnabled` |
| Android | `__android_log_print` (from liblog) |

**Note**: This is a dispatcher/wrapper library - actual GPU rendering goes through `libgpud.so`.

---

## 2. Hidden Debug/Trace Environment Variables

**Key findings:**

| Category | Variables |
|----------|-----------|
| Mali-specific | `MALI_, BASE_MALI_, ARM_MALI_` |
| Tracing | `ACTIVITY_TRACE_FW`, `ACTIVITY_TRACE_GPU`, `arm.mali.perfetto.trace` |
| Debug/Verbose | `AS_SECURE_LOG_FILE`, `asan-debug*` |
| Kernel/DDI | Many `BASE_MALI_UKU_*` version/status flags |

The blob contains extensive debugging infrastructure including:
- Perfetto tracing (`arm.mali.perfetto.trace`)
- Framebuffer dump controls (`gpudIsVkFramebufferDumpEnabled`)
- AddressSanitizer support (`asan-debug*`)
- Secure logging (`AS_SECURE_LOG_FILE`)

---

## 3. Dependencies

**Android Graphics Stack:**
- `android.hardware.graphics.allocator-V2-ndk.so` - Buffer allocation
- `android.hardware.graphics.mapper@4.0.so` - Buffer mapping
- `libgralloc*.so` (3 variants) - Memory management
- `libgpud.so` - **Core GPU driver** (key dependency)
- `libgpu_aux.so` - GPU auxiliary functions
- `libion.so` - ION memory allocator
- `libbinder_ndk.so` - IPC
- `liblog.so` - Android logging

**System:**
- `libc.so`, `libc++.so`, `libm.so`, `libdl.so`, `libz.so`

---

## 4. Build Info / Version Strings

| Info | Value |
|------|-------|
| Vendor | MediaTek (vendor/mediatek) |
| Hardware | Mali-G77 (mali_avalon) |
| Version | **r49p1** ("1.5 Valhall-r49p1-03bet0") |
| UKU Version | See UKU flags below |
| Build System | LLVM (multiple `llvm::` symbols) |
| SDK | Android NDK |
| Path | `vendor/mediatek/proprietary/hardware/gpu_mali/mali_avalon/r49p1/product/gles/` |

---

## 5. UKU Flags (Kernel UAPI Version)

UKU = User Kernel UAPI - defines the ioctl interface to Mali kernel driver:

| Flag | Purpose |
|------|---------|
| `BASE_MALI_UKU_DDK_HWVER` | Hardware version base |
| `BASE_MALI_UKU_DDK_HWVER_MAJOR` | Major version |
| `BASE_MALI_UKU_DDK_HWVER_MINOR` | Minor version |
| `BASE_MALI_UKU_DDK_STATUS_START/END` | Status range |

These strings define the exact ioctl interface between userspace (`libgpud.so`) and the Mali kernel driver (`/dev/mali0`).

---

## Summary

- **Purpose**: This is a Vulkan/OpenCL dispatch library for MediaTek's Mali-G77 (r49p1)
- **Key dependency**: `libgpud.so` handles actual GPU commands
- **Debug capabilities**: Extensive trace/log infrastructure; may accept environment variables for debugging
- **Platform**: Android 12+ (graphics allocator V2, mapper 4.0)
- **Device**: `/dev/mali0` - Mali kernel driver device node
- **UKU**: r49p1 defines ioctl interface to kernel driver