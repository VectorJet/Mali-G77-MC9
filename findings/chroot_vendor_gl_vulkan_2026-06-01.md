# Chroot Vendor GL/Vulkan Probe - 2026-06-01

## Result

The Debian chroot can execute raw Mali kbase jobs through `/dev/mali0`, and it can launch Android/bionic GPU test binaries when Android root paths are exposed with symlinks.

Verified from the chroot:

```text
/tmp/test_gpu_works:
Renderer path: raw /dev/mali0 kbase ioctls
Result: WRITE_VALUE changed 0xDEADBEEF to 0xCAFEBABE

sudo /data/local/tmp/egl_dumper:
Using /vendor/lib64/egl/mt6893/libGLES_mali.so
Renderer: Mali-G77 MC9
Center pixel: ff 00 00 ff
```

## Current GL/Vulkan Status

Debian Mesa is installed, including Panfrost userspace, but it does not bind to this GPU:

- `glxinfo -B` on `DISPLAY=:0` reports `llvmpipe`.
- `vulkaninfo --summary` reports only `llvmpipe`.
- Forcing `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/panfrost_icd.json` fails to enumerate a GPU.

The reason is architectural: stock Mesa Panfrost expects a DRM render/GPU node. This device exposes the display controller as `/dev/dri/card0` (`mediatek-drm`) and exposes the Mali GPU through ARM kbase at `/dev/mali0`.

## Android Vendor Path

The vendor driver stack is Android/bionic:

- `/vendor/lib64/egl/mt6893/libGLES_mali.so`
- `/vendor/lib64/hw/mt6893/vulkan.mali.so`
- `/vendor/lib64/libgpud.so`

`libGLES_mali.so` depends on Android services/libraries such as binder, nativewindow, gralloc, ion, ged, dmabufheap, bionic `libc++`, and `libc`. It cannot be loaded directly into Debian/glibc processes.

## Reproduction

Expose Android root paths inside the chroot:

```bash
bash scripts/setup_android_vendor_links.sh
```

Build and run the vendor EGL pbuffer test:

```bash
DEVICE_USER=u0_a371 bash scripts/run_egl_dumper_vendor.sh
sudo /data/local/tmp/egl_dumper
```

## Next Work

For normal Debian GL/Vulkan apps, the next implementation step is a bridge or shim:

1. Android-side renderer process using the vendor EGL/Vulkan stack.
2. Debian-side GL/Vulkan frontend that forwards commands or uses a higher-level protocol.
3. Longer-term alternative: Mesa Panfrost winsys/UAPI work to target `/dev/mali0` kbase instead of DRM render nodes.

## Bridge Proof

Implemented a minimal bridge:

- Android/bionic server: `src/kbase/tools/mali_egl_bridge_server.c`
- Debian/glibc client: `src/kbase/chroot/mali_egl_bridge_client.c`
- Runner: `scripts/run_mali_egl_bridge.sh`
- Stop helper: `scripts/stop_mali_egl_bridge.sh`

The server runs as root from `/data/local/tmp`, loads `/vendor/lib64/egl/mt6893/libGLES_mali.so`, creates a pbuffer EGL context, listens on `/data/local/tmp/mali_egl_bridge.sock`, clears the pbuffer to the requested color, and returns a `glReadPixels` result.

Verified output:

```text
mali-egl-bridge ready
uid=0 euid=0 renderer=Mali-G77 MC9 lib=/vendor/lib64/egl/mt6893/libGLES_mali.so socket=/data/local/tmp/mali_egl_bridge.sock
requested rgba: 0.250 0.500 0.750 1.000
gpu pixel rgba: 64 128 191 255
```

This proves a Debian process can drive vendor-Mali GL work through an Android-side process, even though Debian Mesa itself still falls back to llvmpipe.

## OpenGL Loader Diagnosis

Normal Debian OpenGL still does not use Mali:

```text
DISPLAY=:0 glxinfo -B
OpenGL renderer string: llvmpipe (LLVM 19.1.7, 128 bits)
Accelerated: no
```

The blocker is not `DISPLAY`; `DISPLAY=:0` is correct. The blocker is that Mesa has no GPU render node to bind:

```text
/dev/dri/card0 -> mediatek-drm display controller
/dev/mali0     -> ARM kbase Mali-G77
```

Stock Debian Mesa Panfrost expects a DRM GPU/render node such as `/dev/dri/renderD*`. This kernel exposes the Mali GPU as the Android/ARM kbase misc device `/dev/mali0`, so Mesa falls back to llvmpipe.

## GL Bridge Shim Proof

Added an experimental preload shim:

- `src/kbase/chroot/libgl_mali_bridge.c`
- `src/kbase/chroot/test_gl_bridge.c`
- `scripts/build_gl_bridge_shim.sh`
- `scripts/run_gl_bridge_proof.sh`

This is not a complete OpenGL implementation. It proves the feasible path: a glibc process can call GL-like entrypoints and have work serviced by the Android vendor EGL bridge.

Verified:

```text
LD_PRELOAD=/tmp/libgl_mali_bridge.so /tmp/test_gl_bridge
vendor: Mali bridge
renderer: Mali-G77 MC9 via Android vendor EGL bridge
version: OpenGL bridge proof 0.1
pixel: 51 102 153 255
```

`glxinfo` remains llvmpipe because GLX/GLVND dispatch and Mesa renderer queries still create/query a Mesa context. Making arbitrary GLX apps use Mali requires one of:

1. A real GLX/EGL/GLES frontend library that implements enough API surface and forwards to the Android-side vendor EGL server.
2. A Mesa winsys/UAPI backend for this kbase `/dev/mali0` kernel driver.
3. A remote-rendering layer such as VirGL-like command transport backed by the Android vendor stack.

## GLES/EGL Library Shim

Implemented the first usable frontend-shim step:

- `src/kbase/chroot/libegl_mali_bridge.c`
- `src/kbase/chroot/libgl_mali_bridge.c`
- `src/kbase/chroot/test_gles_bridge.c`
- `scripts/run_gles_bridge_shim.sh`

The build creates:

```text
/tmp/mali-bridge-lib/libEGL.so.1
/tmp/mali-bridge-lib/libGLESv2.so.2
```

The EGL frontend tracks display initialization, one compatible config, bounded
pbuffer/context objects, per-thread current EGL state, and OpenGL ES API binding.
It implements the pbuffer-oriented EGL 1.x calls commonly used by headless GLES
applications:

- `eglGetDisplay`
- `eglInitialize`
- `eglGetConfigs`
- `eglChooseConfig`
- `eglGetConfigAttrib`
- `eglCreatePbufferSurface`
- `eglCreateContext`
- `eglMakeCurrent`
- `eglGetCurrentDisplay`
- `eglGetCurrentContext`
- `eglGetCurrentSurface`
- `eglQuerySurface`
- `eglQueryContext`
- `eglSurfaceAttrib`
- `eglBindAPI`
- `eglQueryAPI`
- `eglSwapBuffers`
- `eglSwapInterval`
- `eglWaitClient`
- `eglWaitGL`
- `eglWaitNative`
- `eglDestroySurface`
- `eglDestroyContext`
- `eglReleaseThread`
- `eglTerminate`
- `eglGetError`
- `eglQueryString`
- `eglGetProcAddress`

Verified a normal Debian/glibc EGL/GLES-linked process can load those libraries with `LD_LIBRARY_PATH` and route work through the Android Mali bridge. The current test covers a textured GLES2 triangle plus offscreen rendering: shader source/compile and source retrieval, shader/program info-log plumbing, program link/use, attached-shader and active-variable reflection, attrib lookup, dynamic GLES symbol lookup through `eglGetProcAddress`, integer and object-state queries, scalar/vector/array/matrix uniform upload and readback, chunked VBO upload, buffer sub-data, element-buffer upload, sampler uniforms, texture allocation/update/mipmap generation, blend/depth/raster state, framebuffer and renderbuffer attachment, framebuffer completeness, indexed draw, rectangular pixel readback, and object deletion.

```text
LD_LIBRARY_PATH=/tmp/mali-bridge-lib /tmp/test_gles_bridge
egl: 1.5 vendor=Mali bridge EGL
gl vendor: Mali bridge
gl renderer: Mali-G77 MC9 via Android vendor EGL bridge
offscreen pixel: 0 128 255 255
rectangular readback: red green blue white
scissor/color-mask: yellow green
center pixel: 255 0 255 255
```

This is the first concrete path for making chroot OpenGL ES use the GPU. It is still intentionally minimal: it supports EGL pbuffer setup and enough GLES2 calls for client-memory triangle rendering:

- `glGetString`
- `glCreateShader`
- `glShaderSource`
- `glCompileShader`
- `glGetShaderiv`
- `glGetShaderInfoLog`
- `glGetShaderSource`
- `glGetShaderPrecisionFormat`
- `glReleaseShaderCompiler`
- `glShaderBinary` for bounded payloads
- `glCreateProgram`
- `glAttachShader`
- `glDetachShader`
- `glBindAttribLocation`
- `glGetAttribLocation`
- `glLinkProgram`
- `glValidateProgram`
- `glGetProgramiv`
- `glGetProgramInfoLog`
- `glGetAttachedShaders`
- `glGetActiveUniform`
- `glGetActiveAttrib`
- `glGetUniformfv`
- `glGetUniformiv`
- `glUseProgram`
- `glGetIntegerv` for scalar and multi-value state
- `glGetFloatv`
- `glGetBooleanv`
- `glIsEnabled`
- `glIsBuffer`
- `glIsTexture`
- `glIsFramebuffer`
- `glIsRenderbuffer`
- `glIsProgram`
- `glIsShader`
- `glGetBufferParameteriv`
- `glGetTexParameteriv`
- `glGetRenderbufferParameteriv`
- `glGetVertexAttribiv`
- `glGetVertexAttribfv`
- `glGetVertexAttribPointerv`
- `glGetFramebufferAttachmentParameteriv` with vendor-to-bridge object-name translation
- `glGenBuffers`
- `glBindBuffer`
- `glBufferData`
- `glBufferSubData`
- `glGetUniformLocation`
- `glUniform1i`
- `glUniform2i`
- `glUniform3i`
- `glUniform4i`
- `glUniform1f`
- `glUniform2f`
- `glUniform3f`
- `glUniform4f`
- `glUniform1iv`
- `glUniform2iv`
- `glUniform3iv`
- `glUniform4iv`
- `glUniform1fv`
- `glUniform2fv`
- `glUniform3fv`
- `glUniform4fv`
- `glUniformMatrix2fv`
- `glUniformMatrix3fv`
- `glUniformMatrix4fv`
- `glGenTextures`
- `glActiveTexture`
- `glBindTexture`
- `glTexParameteri`
- `glTexParameterf`
- `glTexParameterfv`
- `glTexParameteriv`
- `glGetTexParameterfv`
- `glTexImage2D` for small RGBA/UNSIGNED_BYTE uploads
- `glTexSubImage2D` for small RGBA/UNSIGNED_BYTE updates
- `glCompressedTexImage2D` for bounded payloads
- `glCompressedTexSubImage2D` for bounded payloads
- `glCopyTexImage2D`
- `glCopyTexSubImage2D`
- `glGenerateMipmap`
- `glGenFramebuffers`
- `glBindFramebuffer`
- `glFramebufferTexture2D`
- `glCheckFramebufferStatus`
- `glDeleteFramebuffers`
- `glGenRenderbuffers`
- `glBindRenderbuffer`
- `glRenderbufferStorage`
- `glFramebufferRenderbuffer`
- `glDeleteRenderbuffers`
- `glVertexAttribPointer` for client float arrays
- `glEnableVertexAttribArray`
- `glDisableVertexAttribArray`
- `glViewport`
- `glEnable`
- `glDisable`
- `glBlendFunc`
- `glBlendColor`
- `glBlendEquation`
- `glBlendEquationSeparate`
- `glBlendFuncSeparate`
- `glDepthFunc`
- `glClearDepthf`
- `glDepthMask`
- `glDepthRangef`
- `glScissor`
- `glColorMask`
- `glCullFace`
- `glFrontFace`
- `glStencilFunc`
- `glStencilOp`
- `glStencilMask`
- `glStencilFuncSeparate`
- `glStencilMaskSeparate`
- `glStencilOpSeparate`
- `glClearStencil`
- `glLineWidth`
- `glPixelStorei`
- `glPolygonOffset`
- `glSampleCoverage`
- `glHint`
- `glFlush`
- `glVertexAttrib1f` / `glVertexAttrib1fv`
- `glVertexAttrib2f` / `glVertexAttrib2fv`
- `glVertexAttrib3f` / `glVertexAttrib3fv`
- `glVertexAttrib4f` / `glVertexAttrib4fv`
- `glClearColor`
- `glClear`
- `glDrawArrays`
- `glDrawElements` with element-array-buffer offsets
- `glFinish`
- `glReadPixels`
- `glGetError`
- `glDeleteBuffers`
- `glDeleteTextures`
- `glDeleteProgram`
- `glDeleteShader`

The current `glVertexAttribPointer` path supports VBO offsets and a compatibility fallback for client float arrays. Buffer uploads and RGBA/UNSIGNED_BYTE readback are split into bounded protocol chunks rather than truncated or synthesized. The EGL shim resolves bridge GLES functions through `eglGetProcAddress`, tracks pbuffer dimensions and context versions, and reports current EGL objects correctly. Query coverage includes typed scalar/multi-value state, object validity, enabled capabilities, buffer size, texture parameters, renderbuffer dimensions, full vertex-attrib state, framebuffer attachments, shader source, attached shaders, active uniforms/attributes, and uniform values. Vendor object names are translated back to bridge IDs.

Every core function declared by the Debian GLES2 `gl2.h` is exported by the
bridge `libGLESv2.so.2`; the header-to-export audit reports no missing symbols.
Compressed texture and shader-binary payloads remain bounded by the fixed
4096-byte request payload.

## X11 window presentation

The chroot EGL frontend now supports `EGL_WINDOW_BIT`,
`eglCreateWindowSurface`, and `eglSwapBuffers` for 64x64 X11 windows on
`DISPLAY=:0`. Rendering still executes in the Android vendor Mali pbuffer.
Swap completion reads that GPU-rendered buffer through the bridge, flips it,
and presents it with `XPutImage`.

The visible-window test reports:

```text
egl window: 1.5
renderer: Mali-G77 MC9 via Android vendor EGL bridge
window pixel: 255 0 0
```

This proves GPU-backed GLES rendering can be displayed in the chroot X server.
It is currently a readback presentation path, not a zero-copy native Android
window path. Remaining large-scope work is dynamic surface sizing, eliminating
per-frame CPU readback, robust multi-process/context isolation, streamed large
binary uploads, and a desktop OpenGL/GLX frontend rather than only
EGL/OpenGL ES 2.
