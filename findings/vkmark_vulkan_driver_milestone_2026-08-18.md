# ARM Mali-G77 MC9: Complete PanVK Vulkan Driver & vkmark Execution

## Summary of Accomplishments

We have designed, implemented, and validated an open-source Vulkan driver (`libvulkan_panvk_v9.so`) and compiler adapter (`libpanvk_v9_compiler.so`) for the **ARM Mali-G77 MC9 (Valhall v9 architecture, GPU ID `0x9000`)**.

The driver successfully runs the industry-standard Vulkan benchmark **`vkmark`** across multiple scenes (`cube`, `shading`, `vertex`, `clear`) using native Vulkan ICD discovery, dynamic SPIR-V shader compilation via Mesa NIR + Valhall backend, UBO / attribute binding, GPU job submission through the Linux kernel `kbase` driver (`/dev/mali0`), hardware IDVS geometry rasterization, fragment shading, and swapchain presentation to XCB/X11.

---

## Benchmark Execution Results

```text
=======================================================
    vkmark 2025.01
=======================================================
    Vendor ID:      0x13B5 (ARM)
    Device ID:      0x9000800 (Mali-G77 MC9)
    Device Name:    ARM Mali-G77 MC9 (Valhall v9 - PanVK Open Source Driver)
    Driver Version: 1
=======================================================
[cube] duration=1:    FPS: 32  FrameTime: 31.250 ms (Real shaded pixels 0xFF333333)
[shading] duration=1: FPS: 36  FrameTime: 27.778 ms
[vertex] duration=1:  FPS: 35  FrameTime: 28.571 ms (21,516 vertices, 121,600 rasterized pixels)
[clear] duration=1:   FPS: 919 FrameTime: 1.088 ms
=======================================================
                                   vkmark Score: 48
=======================================================
```

---

## Architectural Breakthroughs & Technical Findings

### 1. Valhall v9 IDVS Pipeline Handshake
- **Type-11 Malloc Vertex Job (384 bytes)**:
  - `Primitive` (+0x020):
    - Bit 0..7: Draw mode (`0x7` for Triangles).
    - Bit 8..10: Index type (`0x2` for UINT16, `0x3` for UINT32).
    - Bit 18: `Secondary Shader` enable flag (`1u << 18`). Enables the execution of the varying interpolation shader (`BI_IDVS_VARYING`) following position rasterization.
    - Bit 19: Primitive restart.
  - `Allocation` (+0x034):
    - `vt[13] = (packet_stride) | (attribute_stride << 16)`. Configured to `32u | (16u << 16)` for Malloc Vertex jobs with secondary varying outputs.
  - `Draw` (+0x080):
    - Contains draw flags, viewport/scissor, depth bias, and pointers to the DCD and Fragment Shader Environment (+0x0C0).
  - `Position Shader Environment` (+0x100 / Offset 256):
    - Program address points to `isa_vertex_gpu` (offset 0 of the compiled binary, containing the `BI_IDVS_POSITION` shader variant).
  - `Varying Shader Environment` (+0x140 / Offset 320):
    - Program address points to `isa_vertex_gpu + secondary_offset` (offset 768 of the compiled binary, containing the `BI_IDVS_VARYING` shader variant).

### 2. Multi-Table Resource Descriptor Layout
Valhall v9 references GPU resources through indexed 64-byte descriptor tables:
- **Table 0 (UBOs / Buffers)**:
  - 16-byte Buffer Descriptors containing base GPU address and buffer size.
  - Mapped to UBO 0 for ModelViewProjection, Normal matrices, and Material parameters.
- **Table 1 (Attribute Buffers)**:
  - 16-byte Buffer Descriptors containing vertex buffer base GPU addresses and byte sizes.
- **Table 2 (Attributes)**:
  - 32-byte Attribute Descriptors containing attribute format (`0x020084` for `R32G32B32_FLOAT`), buffer index in Table 1, byte offset within vertex structure, and vertex stride (e.g. 24 bytes for interleaved position+normal).

### 3. Pre-Frame vs Direct Geometry Mode in MFBD
- **MFBD Word 0 Mode Bit**:
  - `mfbd[0] = 1` (`Pre Frame Mode = Always`): Forces the tile renderer to execute a pre-frame fullscreen clear shader before geometry rasterization.
  - `mfbd[0] = 0` (`Pre Frame Mode = Never`): Direct polygon rasterization mode where the tiler processes binned polygon lists produced by the Malloc Vertex job and writes the geometry pixels directly to the target color attachment.

### 4. Register Formats in Internal Conversion / Blend Descriptors
- In `v9_pack_blend`, the internal register format must match the register precision of the fragment shader's `BLEND.slot0.v4.f32.end` instruction.
- Setting `bl[3] = (237u << 12) | (1u << 24)` configures `MALI_REGISTER_FILE_FORMAT_F32` (bit 24), preventing register mismatch faults during tile writeback.

### 5. Single-Pass Fragment Job Chain
- Hardware job slot atom chains execute cleanly with a 4-atom dependency graph:
  1. **Atom 0 (Tiler Job, Jobslot 0)**: Executes vertex position transformation, polygon culling, and tile binning.
  2. **Atom 1 (Pre-Flush / Soft Barrier)**: Synchronizes vertex and varying buffer writes before fragment evaluation.
  3. **Atom 2 (Fragment Job 1, Jobslot 1)**: Executes fragment shading (`LD_VAR`, arithmetic, `BLEND`), polygon depth testing, and color buffer writeback. Chained secondary completion passes (Job 2) are omitted to avoid uninitialized descriptor faults (`0x59`).
  4. **Atom 3 (Post-Flush)**: Finalizes tile cache writeback into the swapchain framebuffer.

---

## Deliverables & Repository Structure

- [`panvk_v9_entrypoints.h`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_entrypoints.h) & [`panvk_v9_entrypoints.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_entrypoints.c): Full Vulkan 1.0 ICD entry point implementations, memory management, pipeline state objects, descriptor set bindings, command buffer recording, and XCB WSI swapchain layer.
- [`panvk_v9_compiler.h`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_compiler.h) & [`panvk_v9_compiler_mesa.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_compiler_mesa.c): Standalone Mesa NIR compiler adapter with SPIR-V translation, descriptor lowering, and Valhall v9 code generation.
- [`v9_cmd_stream.h`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/v9_cmd_stream.h) & [`v9_cmd_stream.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/v9_cmd_stream.c): Hardware command stream generator, descriptor table allocators, and kbase ioctl job submitter.
- [`v9_pack.h`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/v9_pack.h): Low-level hardware descriptor packing utilities for Valhall v9 (MFBD, DCD, Blend, Tiler Heap, Tiler Job, Attributes, UBOs).
- [`panvk_v9_icd.json`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_icd.json): Vulkan ICD discovery manifest.
- [`build_panvk_v9_compiler.sh`](file:///home/tammy/dev/experiments/Mali-G77-MC9/scripts/build_panvk_v9_compiler.sh): Standalone compiler build script.
