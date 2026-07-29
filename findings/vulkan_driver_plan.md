# Implementation Plan: Open-Source Vulkan Driver Roadmap for Mali-G77 (Valhall v9)

## 🎯 Executive Summary

With the successful resolution of **Atom 2 Fragment JC (`0x58` fault eliminated)** and end-to-end empirical verification of **Polygon-List Mode rendering (`0x01` flags producing 256/256 green pixels)** on hardware `/dev/mali0`, we now have the essential hardware reverse-engineering foundation required to design and build an open-source Vulkan driver for ARM Mali-G77 MC9 (Valhall v9).

This document details a multi-phase architectural roadmap modeled after Mesa's **PanVK** (for ARM Mali Midgard/Bifrost/Valhall) and **Turnip** (for Qualcomm Adreno).

---

## 🏗 System Architecture & Driver Stack

```mermaid
graph TD
    subgraph Vulkan API Layer [Vulkan Application / Benchmark / VKMark]
        App[Vulkan App / VKMark / Games]
    end

    subgraph Mesa PanVK Driver Core [Mesa src/panfrost/vulkan/]
        VK_API[Vulkan Entry Points (vkCreateInstance, vkCreateDevice, etc.)]
        CMD_REC[Command Buffer Recorder (panvk_cmd_buffer.c)]
        PASS_MGR[RenderPass / Dynamic Rendering Manager]
        PIPELINE[Pipeline Compiler & State Builder (panvk_v9_pipeline.c)]
        WSI[Window System Integration (X11 / Wayland / Android ANativeWindow)]
    end

    subgraph Compiler & Genxml Layer [Mesa src/panfrost/lib/ & compiler/]
        VALHALL_COMPILER[NIR -> Valhall ISA Shader Compiler]
        GENXML[Valhall v9 GenXML Descriptor Encoders (MFBD, DCD, TJ, JC)]
    end

    subgraph Winsys / Kbase Shim [Mesa src/panfrost/winsys/kbase/]
        KBASE_WS[Mali kbase Winsys Shim (kbase_drm.c / kbase_ioctl.c)]
        BO_MGR[Buffer Object & GPU VA Manager (panfrost_bo.c)]
    end

    subgraph Kernel & Hardware [/dev/mali0 & GPU HW]
        KBASE_DRIVER[Linux Kernel mali_kbase Driver]
        MALI_HW[ARM Mali-G77 MC9 GPU Hardware (Tiler + Fragment + CS)]
    end

    App --> VK_API
    VK_API --> CMD_REC
    VK_API --> PIPELINE
    VK_API --> WSI
    PIPELINE --> VALHALL_COMPILER
    CMD_REC --> GENXML
    GENXML --> KBASE_WS
    KBASE_WS --> KBASE_DRIVER
    KBASE_DRIVER --> MALI_HW
```

---

## 📋 Proven Hardware Contracts (What We Have Verified)

| Hardware Component | Proven Value / Structure | Significance for Driver |
| :--- | :--- | :--- |
| **Fragment Queue Routing** | `core_req = 0x041` (`BASE_JD_REQ_FS \| COHERENT`) | Maps Fragment atoms to Fragment HW Queue (fixes `0x58`) |
| **Tiler Queue Routing** | `core_req = 0x04E` (`PROTECTED \| TILER \| CS \| COHERENT`) | Maps Tiler atoms to Tiler HW Queue |
| **MFBD Flags** | `flags = 0x01` (`Bit 0 = 1, Bit 7 = 0`) | Enables Polygon List Hardware Execution |
| **TILER_JOB Linkage** | `se + 10 = sp_addr` (Fragment Shader Program) | Embeds Shader Program VA into binned primitive descriptors |
| **DCD Descriptors** | `dcd[0] = 0x228, dcd[1] = 0xFFFF, 1ULL \| blend_addr` | Configures Frame Shader weak early z & blend pointers |
| **Tiler Heap & Context** | 32-byte `TILER_HEAP` + 192-byte `TILER_CONTEXT` | Enables 2-pass binning and hardware polygon list generation |
| **Cache Flushing** | Atom Type 3 (`fl[4] = 6`, `fl[8..9] = 0xFFFFFFFF`) | Flushes GPU L2 cache to CPU system RAM |

---

## 🚀 Multi-Phase Driver Development Plan (Option A: Direct kbase Winsys)

### Phase 1: Standalone Winsys Shim & BO Allocator (`src/driver/kbase_winsys`)
**Goal**: Create a lightweight Mesa-compatible `winsys` library in C that wraps `mali_kbase` ioctls and manages GPU Virtual Address allocations.

---

### Phase 2: Descriptor Encoder & Command Buffer Generator (`src/driver/v9_builder`)
**Goal**: Abstract Valhall v9 descriptor generation (`MFBD`, `DCD`, `TILER_CTX`, `TILER_HEAP`, `TILER_JOB`, `BLEND`, `RT`) into clean C builder APIs.

---

### Phase 3: Minimal Mesa Vulkan Driver Shell (`src/driver/vulkan`)
**Goal**: Wire the builder into Mesa's Vulkan entry points to implement `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, and `VkCommandBuffer`.

---

### Phase 4: NIR -> Valhall Shader Compiler & Pipeline State (`src/driver/compiler`)
**Goal**: Extend Mesa's `bifrost`/`valhall` compiler backend to compile Vulkan SPIR-V shader code into Valhall v9 machine instructions.

---

### Phase 5: WSI & Hardware Presentation (VKMark / Android / Wayland)
**Goal**: Integrate Window System Integration (WSI) for displaying rendered frames on screen.
