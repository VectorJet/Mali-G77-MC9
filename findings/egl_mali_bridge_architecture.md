# Mali EGL Bridge Architecture and Performance Ceilings

## Overview
The `mali_egl_bridge` provides a mechanism for rendering OpenGL ES graphics on a headless Debian chroot while utilizing the Android host's Mali-G77 MC9 GPU hardware. This is accomplished by forwarding EGL and GLES commands over a Unix Domain Socket from a lightweight shim (`libEGL.so` / `libGLESv2.so`) loaded in Debian to an Android-side server (`mali_egl_bridge_server`) running as root.

The most performance-critical aspect of this architecture is the pixel transport pipeline: moving the rendered framebuffer from the GPU's memory back to the Debian chroot for X11 presentation.

## Evolution of the Pixel Transport Pipeline

### 1. The Naive Socket Path
Initially, pixel data was extracted via `glReadPixels` on the Android side and sent back to Debian in 64KB chunks over the Unix Domain Socket.
- **Debian Client:** Blocks on socket reads to accumulate the full framebuffer.
- **Android Server:** Issues `glReadPixels` into a heap buffer, chunks it, and writes it over the socket.
- **Performance:** For a 1080x2231 window (9.6MB), this required over 150 socket roundtrips per frame. Performance was abysmal (slideshow framerates around 1-3 FPS) due to the massive IPC overhead.

### 2. The `mmap` Zero-Copy IPC Path
To eliminate socket chunking, we introduced a shared memory backing file (`/data/local/tmp/mali_bridge_pixels.bin`):
- **Android Server:** Maps the file via `mmap`, passes the mapped pointer directly to `glReadPixels`, and sends a single 16-byte socket acknowledgment containing dimensions.
- **Debian Client:** Maps the same file, avoiding the socket data transfer entirely.
- **Performance Gain:** Removed all socket chunking overhead.

### 3. CPU Pixel Swizzle Optimization
Xorg requires `BGRA` pixels and an inverted Y-axis (top-down), while OpenGL outputs bottom-up `RGBA`. This mandated a CPU pass on the Debian side.
- **Scalar Byte-Loop:** The initial byte-by-byte swizzle loop was slow.
- **32-Bit Bitwise Swizzle:** Combining the read and write into `uint32_t` bitwise shifts significantly improved CPU time.
- **NEON Intrinsics:** We finalized the CPU loop using ARM NEON SIMD intrinsics (`vld4q_u8` / `vst4q_u8`) to process 16 pixels at a time, performing an in-register RGBA -> BGRA swap.
- **Performance Gain:** Pushed the CPU swizzle step down to an estimated <0.5ms per frame. Framerates climbed to ~16-20 FPS.

## The Fundamental Ceiling

Despite moving pixel data over `mmap` and hyper-optimizing the swizzle loop, the framerate remains hard-capped at around ~16-20 FPS.

This is because the current architecture relies on a **blocking `glReadPixels` roundtrip**.

1. **GPU Stall:** `glReadPixels` inherently forces the Mali GPU pipeline to stall and sync.
2. **Socket Latency:** The Debian client must send the "read pixels" command over the socket, the Android daemon must wake up, issue the `glReadPixels` blocking call to the driver, wait for the GPU, and send the acknowledgment back to Debian.
3. **Synchronous IPC:** Even though the *data* payload avoids the socket, the control flow is entirely synchronous and block-based.

This fixed latency of the GPU flush combined with the Unix socket context switch defines our absolute ceiling.

## Paths Beyond the Ceiling

To achieve 60 FPS, the `glReadPixels` synchronization must be eliminated.

1. **DMA-BUF Export/Import:**
   The Android daemon allocates buffers via `Gralloc`/`ION`. By extracting the underlying file descriptors (via `AHardwareBuffer` or direct gralloc APIs) and passing them over the socket using `SCM_RIGHTS`, the Debian client could import them as EGL images or raw DRM/KMS buffers. This would provide true zero-copy presentation without any `glReadPixels` stalling.

2. **Native X11 Server on Android:**
   Running an X Server (like Termux:X11) that directly implements EGL/DRI3 on top of Android's Bionic environment would bypass the need for a bridge socket entirely.

3. **CSF/kbase Zero-Copy Paths:**
   Bypassing EGL and communicating directly with the `mali_kbase` driver to submit command streams (which we are exploring in the `mdla` and `apu` projects) would allow us to map GPU memory directly into our chroot process space, achieving native zero-copy.
