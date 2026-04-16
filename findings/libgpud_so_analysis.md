# libgpud.so Analysis - The Real RE Target

## Summary
**330KB is NOT a full GPU driver** - it's a thin command stream builder. The stack architecture is:

```
┌─────────────────────────────────────────────────────────────────┐
│ libGLES_mali.so (54MB)                                         │
│   - GL/VK API entrypoints                                       │
│   - Shader compiler (LLVM-based)                                │
│   - State management                                            │
└──────────────────────────┬──────────────────────────────────────┘
                           ↓ calls into
┌─────────────────────────────────────────────────────────────────┐
│ libgpud.so (330KB) - PRIMARY RE TARGET                          │
│   - Command stream building                                     │
│   - Descriptor generation                                       │
│   - 335 exported gpud* functions                                │
└──────────────────────────┬──────────────────────────────────────┘
                           ↓ ioctl via /dev/mali0
┌─────────────────────────────────────────────────────────────────┐
│ Mali Kernel Driver (mali.ko / built-in)                         │
│   - /dev/mali0 (major 10, minor 113) - MediaTek variant?       │
└─────────────────────────────────────────────────────────────────┘
```

## libgpud.so - Key Statistics

| Metric | Value |
|--------|-------|
| File size | 330KB |
| Exported functions | 335 (all gpud*) |
| Ioctl calls | 0 (!) |
| Dependencies | Android graphics stack only |

## Wait - Zero ioctl() calls?!

```
$ strings libgpud.so | grep -c "ioctl"
0
$ strings libgpud.so | grep "/dev/mali"
(nothing)
```

**Hypothesis**: libgpud.so doesn't use standard ioctl() - it likely uses:
- `write()` to `/dev/mali0` 
- Memory-mapped I/O (mmap)
- Or: communication via another kernel API (binder, syscalls)

This means the actual UAPI might be through file descriptors or shared memory.

## Exported Functions (335 total)

### Vulkan (gpudVk*)
- `gpudVkEnumeratePhysicalDevices`
- `gpudVkCreateBuffer`, `gpudVkAllocateMemory`, `gpudVkCreateSemaphore`
- `gpudVkAllocateCommandBuffers`, `gpudVkGetDeviceQueue`
- `gpudVkCmdSetViewport`, `gpudVkCmdEndRenderPass`, `gpudVkCmdCopyImage2`

### OpenGL (gpudGl*)
- `gpudGlGenFramebuffers`, `gpudGlBufferData`, `gpudGlVertexAttribPointer`
- `gpudGlUseProgram`, `gpudGlClearDepthf`, `gpudGlPolygonOffset`

### Debug/Dump
- `gpudVkDumpShaderModule`, `gpudGlDumpFramebuffer`, `gpudGlDumpTexImage`
- `gpudVkDumpBindPipelineShaders`, `gpudDumpShaderCompiler`

### Hacks (workarounds?)
- `gpudGlHackInvalidateSubFramebuffer`, `gpudGlHackDepthMask`
- `gpudGlHackUseProgram`, `gpudGlHackCompileShader`
- `gpudGlHackVertexAttribPointer`, `gpudGlHackUniformui`
- `gpudVkHackGetPhysicalDeviceProperties`

### Misc
- `gpudMiscDisableCRC`, `gpudMiscDisableCOW`
- `gpudSfDumpScreenshot`, `gpudEglCreateContext`

## The "Hack" Functions Are Significant

The `gpudGlHack*` and `gpudVkHack*` functions are tiny stubs (4 bytes each = 1 aarch64 `ret` instruction):
- Literally empty - not actual fixes
- **The NAMES are the value** - they map to bugs/workarounds MediaTek implemented
- Each name = a specific bug they patched at driver level instead of fixing properly

### Documented Hack Functions (RE Gold)

| Function | Bug/Workaround |
|----------|----------------|
| `gpudGlHackDepthMask` | Depth mask state bug |
| `gpudGlHackInvalidateSubFramebuffer` | Invalidate sub-fb bug |
| `gpudGlHackUseProgram` | Program binding race condition |
| `gpudGlHackCompileShader` | Shader compilation race |
| `gpudGlHackVertexAttribPointer` | Vertex attrib pointer state |
| `gpudGlHackUniformui` | Uniform update ordering |
| `gpudVkHackGetPhysicalDeviceProperties` | Physical device props quirk |

**Cross-reference with Panfrost**: These map directly to Panfrost quirk flags in `pan_quirks.c` - each "hack" is a potential GPU-specific behavior to emulate.

## Dependencies
```
android.hardware.graphics.common-V5-ndk.so
android.hardware.graphics.allocator-V2-ndk.so
libcutils.so, libutils.so, liblog.so
libnativewindow.so, libgralloc_extra.so
libc++.so, libc.so, libm.so
```

## Device Node
- `/dev/mali0` (major **10**, minor 113)
- Major 10 = `MISC_MAJOR` in Linux - miscellaneous device class
- ARM's stock Mali driver uses major 240 - **MTK registered as misc device = custom fork signal**
- Standard path: `mmap()` shared ring buffer → `write()` commands → signal GPU

## Next Steps for RE
1. Load libgpud.so in Ghidra - 330KB is manageable
2. Focus on `gpudVkCmdDraw` - the hottest path
3. Trace callgraph downward until you hit the mmap buffer write
4. That write site = command stream format = what Panfrost needs to generate
5. The mmap offset + command word structure at write site = crown jewel

## Communication Pattern (Hypothesis)

```
open(/dev/mali0) → get fd
mmap(fd) → get shared ring buffer in userspace
write commands directly into ring buffer
signal GPU via write() or eventfd
```

- Zero ioctls = mmap-based ring buffer communication (confirmed by analysis)
- Modern GPU drivers avoid per-draw ioctls - too slow
- Command stream lives in mapped memory, GPU reads directly

**Verification blocked**: No strace on device. Alternative: analyze libgpud.so in Ghidra for `mmap`, `mmap64`, `open` syscalls.

## The Crown Jewel
If `arm.mali.perfetto.trace` triggers, it dumps command streams from libgpud.so - no RE needed. Need to find the right env var combo to trigger it on this MediaTek build.

---

# Appendix: Strace Attempt (Failed)

Attempted to verify mmap hypothesis via strace:
```
adb shell su -c "strace -e mmap,openat -p $(pidof surfaceflinger)"
```
**Result**: No strace binary on device. Alternative approaches:
- Root + compile strace for arm64
- Use Ghidra to analyze syscalls in libgpud.so