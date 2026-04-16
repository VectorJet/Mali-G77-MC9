# Mali-G77 (Valhall) Triangle Rendering Progress

**Date:** 2026-04-15

## Goal: "THE TRIANGLE"

To render a triangle on the Mali-G77 hardware, we must assemble a complete Valhall rendering pipeline. Unlike Bifrost, Valhall uses highly aggregated job descriptors and complex pointer tagging.

## The Valhall Rendering Pipeline

A standard Valhall drawing pipeline requires two jobs:
1. **Malloc Vertex Job (Type 11)**: A massive 384-byte job that executes the Vertex Shader, automatically allocates memory for varyings, and feeds the resulting vertices directly into the Tiler.
2. **Fragment Job (Type 9)**: A 64-byte job that reads the binned tiles produced by the Vertex Job and executes the Fragment Shader to color the pixels.

### Step 1: The Malloc Vertex Job (Type 11)
The `Malloc Vertex Job` is structured as an aggregate of multiple sections:
- `+0x00`: **Job Header** (32 bytes)
- `+0x20`: **Primitive** (Draw Mode, Offsets)
- `+0x30`: **Instance Count**
- `+0x34`: **Allocation** (For varying buffers)
- `+0x38`: **Tiler Pointer** (Points to the `Tiler Context` descriptor)
- `+0x68`: **Scissor Array Pointer**
- `+0x70`: **Primitive Size Array Pointer**
- `+0x78`: **Indices Pointer** (If using indexed drawing)
- `+0x80`: **Draw Parameters** (Vertex Count, etc.)
- `+0x100`: **Position Shader Environment** (Points to Vertex Shader ISA and resources)
- `+0x140`: **Varying Shader Environment** (Points to Varying Shader ISA)

### Step 2: The Tiler Context & Heap
The `Tiler Pointer` must point to a 64-byte aligned `Tiler Context`.
- The `Tiler Context` specifies the Framebuffer dimensions and points to a `Tiler Heap`.
- The `Tiler Heap` is a memory buffer where the GPU temporarily stores the binned polygon lists before the Fragment Job consumes them.

### Step 3: The Fragment Job (Type 9)
The `Fragment Job` points to a `Framebuffer Descriptor`:
- The **Framebuffer Descriptor** contains the dimensions, clear color, and a pointer to the **Render Target** (our Color Buffer).
- The Fragment Job automatically coordinates with the Tiler output to shade the bins.

## Next Action Items
1. Construct the `Tiler Heap` and `Tiler Context` descriptors in memory.
2. Construct the `Shader Environment` for a trivial vertex shader (e.g., hardcoded triangle coordinates) and a trivial fragment shader (e.g., solid red).
3. Assemble the `Malloc Vertex Job` and `Fragment Job` and submit them as a 2-atom batch!
