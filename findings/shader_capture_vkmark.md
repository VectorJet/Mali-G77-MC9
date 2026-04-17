# Mali-G77 Shader Capture from vkmark

**Date:** 2026-04-17

## Summary

Successfully captured Valhall shader ISA binaries from vkmark Vulkan benchmark running on Mali-G77! This is the breakthrough we needed for triangle rendering.

## Capture Method

1. Installed `glmark2` from Termux repo (provides vkmark)
2. Set wrap property for termux-x11: `setprop wrap.com.termux.x11 LD_PRELOAD=/data/data/com.termux/files/home/ioctl_spy.so`
3. Ran vkmark with Mali GPU: `vkmark -s 256x256`
4. Captured files in `/data/local/tmp/mali_capture/`

## Captured Files

Each frame produces multiple captures:

| File | Size | Description |
|------|------|-------------|
| `*_compute_shader_isa.bin` | 4048 bytes | Vertex/Compute shader ISA |
| `*_compute_fau.bin` | 32 bytes | Fast Access Uniforms |
| `*_compute_resources.bin` | 32 bytes | Resource table |
| `*_compute_thread_storage.bin` | 32 bytes | Local storage descriptor |
| `*_frag_fbd.bin` | 256 bytes | Framebuffer Descriptor |
| `*_frag_shader_dcd.bin` | 128 bytes | Fragment Shader DCD |

## Shader ISA Analysis

The captured Compute shader ISA (4048 bytes):
```
0000: 1800 0080 0000 0000 0010 0000 7f00 0000
0010: 0000 0000 0000 0000 0000 0000 0000 0000
...
0040: 2801 0090 0038 0000 8011 0000 7f00 0000
```

This appears to be Valhall vertex/geometry processing shader.

## Next Steps

Use these captured binaries to construct a working triangle:
1. Use captured compute shader ISA for vertex processing
2. Use captured fragment shader DCD for pixel shading
3. Use FBD structure as template for our color buffer
4. Replay with our job submission code, updating pointers to our buffers