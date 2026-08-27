# Handoff Documentation: Mali-G77 MC9 (Valhall v9) Vulkan Driver

## Current State & Achievements

1. **Hardware GPU Acceleration Verified on `/dev/mali0`**:
   - **Tiler Engine (Atom 0)**: Hardware polygon binner dynamically bins primitives across **805 to 850 active tiles** on Mali-G77 MC9.
   - **Fragment Engine (Atom 1)**: Hardware fragment shaders execute across all 16x16 tiles on 9 shader cores (`event_code=0x1`).
   - **L2 Cache Flush (Atom 2)**: Hardware memory barrier and cache sync verified on Job Slot 1 (`event_code=0x1`).
   - Kernel Driver IOCTL submission rate: **~75 hardware submissions/sec** verified via `strace`.

2. **3D Visual Presentation**:
   - `vkmark --winsys xcb -b cube` renders the colored, shaded, rotating 3D cube inside the Termux-X11 desktop window at **20–30 FPS**.
   - Integrated per-frame depth buffer management, back-face culling, and Phong diffuse lighting.
   - 100% opaque pixel presentation (`pixel | 0xFF000000u`) configured in `vkQueuePresentKHR` to prevent compositor background leakage.

---

## Key Files & Code Locations

| File | Purpose |
| :--- | :--- |
| [`src/vulkan/v9_pack.h`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/v9_pack.h) | Valhall v9 hardware descriptors (Tiler Job, Primitive offsets, RT0 16x16 tiled block format, MFBD). |
| [`src/vulkan/v9_cmd_stream.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/v9_cmd_stream.c) | Command stream submission, 3D vertex processing, depth buffer management, atom submission pipeline. |
| [`src/vulkan/panvk_v9_entrypoints.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/vulkan/panvk_v9_entrypoints.c) | Vulkan 1.0/1.1 API entrypoints, UBO dynamic binding, XCB/X11 WSI surface presentation. |
| [`src/driver/kbase_winsys.c`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/driver/kbase_winsys.c) | Direct kernel interface for `/dev/mali0` (`KBASE_IOCTL_MEM_ALLOC`, `KBASE_IOCTL_JOB_SUBMIT`, SAME_VA). |
| [`findings/vkmark_3d_cube_rendering_and_hardware_tiler_2026-08-18.md`](file:///home/tammy/dev/experiments/Mali-G77-MC9/findings/vkmark_3d_cube_rendering_and_hardware_tiler_2026-08-18.md) | Detailed findings on hardware tiler descriptor fixes and 3D rendering. |
| [`findings/eden_winlator_vulkan_driver_architecture_and_perf_2026-08-27.md`](file:///home/tammy/dev/experiments/Mali-G77-MC9/findings/eden_winlator_vulkan_driver_architecture_and_perf_2026-08-27.md) | Full architectural roadmap & performance plan for Eden & Winlator. |

---

## Device Workflow & Build Commands

### 1. Compile & Deploy Driver on Device
```bash
scp -P 8022 src/driver/kbase_winsys.c src/driver/pan_kmod_kbase.c src/vulkan/v9_pack.h src/vulkan/v9_cmd_stream.h src/vulkan/v9_cmd_stream.c src/vulkan/panvk_v9_entrypoints.c u0_a375@localhost:/data/data/com.termux/files/home/ && \
ssh -p 8022 u0_a375@localhost "cd /data/data/com.termux/files/home && \
    gcc -Wall -O2 -fPIC -shared -o libvulkan_panvk_v9.so \
        kbase_winsys.c pan_kmod_kbase.c v9_cmd_stream.c panvk_v9_entrypoints.c \
        -lX11 -lxcb -ldl -lm -pthread"
```

### 2. Run vkmark Cube Benchmark in Termux-X11
```bash
ssh -p 8022 u0_a375@localhost "su -c 'cd /data/data/com.termux/files/home && DISPLAY=:0 VK_ICD_FILENAMES=/data/data/com.termux/files/home/panvk_v9_icd.json LD_LIBRARY_PATH=/data/data/com.termux/files/home:/data/data/com.termux/files/usr/lib /data/data/com.termux/files/usr/bin/vkmark --winsys xcb -b cube:duration=15'"
```

### 3. Capture Screen & Verify Visuals
```bash
ssh -p 8022 u0_a375@localhost "su -c 'DISPLAY=:0 /data/data/com.termux/files/home/x11_screenshot /data/data/com.termux/files/home/screen.bmp && chmod 666 /data/data/com.termux/files/home/screen.bmp'" && \
scp -P 8022 u0_a375@localhost:/data/data/com.termux/files/home/screen.bmp . && \
python3 -c "from PIL import Image; Image.open('screen.bmp').save('screen.png')"
```

---

## Next Steps for Continuing Agent

1. **Integrate into Mesa PanVK (`refs/mesa-Panfork-android` & `refs/panfork-termux`)**:
   - Add Valhall v9 (`panvk_v9`) build target in `src/panfrost/vulkan/meson.build`.
   - Wire `kbase_winsys.c` into Mesa's `pan_kmod` backend to replace DRM render node dependencies.
2. **Valhall NIR Compiler Integration**:
   - Connect `nir_to_valhall` to compile SPIR-V vertex and fragment shaders dynamically.
   - Execute vertex shaders directly on the 9 Mali-G77 GPU cores instead of CPU matrix math.
3. **Descriptor Indexing & Buffer Device Address**:
   - Implement `VK_KHR_descriptor_indexing` and `VK_KHR_buffer_device_address` required by Eden and DXVK.
4. **AFBC (ARM Framebuffer Compression)**:
   - Enable AFBC in `RT0` descriptors to reduce DRAM bandwidth and improve framerates.
