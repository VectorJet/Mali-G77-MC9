# Chrome Valhall Hardware Job Analysis

**Date:** 2026-04-15

## The Chrome Capture
We successfully injected `ioctl_spy.so` into the live Google Chrome process. When Chrome rendered WebGL, we captured the exact hardware job submissions sent to the Mali-G77 driver.

Chrome submits a 3-atom batch to the kernel:
- **Atom 0**: Wait/Dependency setup (Type 9, core_req=0x1) - Not the main drawing job.
- **Atom 1**: The Vertex / Compute Job (Type 4, core_req=0x4e)
- **Atom 2**: The Fragment Job (Type 9, core_req=0x1)

### Analyzing Atom 1: The Compute / Vertex Job (Type 4)
The control word is `0x00010008`
- `[7:1]`: Job Type = 4 (`Compute`)
- `[31:16]`: Index = 1

Chrome uses a Compute Job (`Type 4`) to process vertices instead of the huge 384-byte `Malloc Vertex Job` (`Type 11`). Let's look at the payload starting at `+0x20`:
```text
0020: 00 00 00 80 00 81 00 00  01 00 00 00 01 00 00 00
0030: 01 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0040: 00 00 00 00 04 00 00 00  00 00 00 00 00 00 00 00
0050: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0060: c8 e5 d8 ff 5e 00 00 00  00 e0 ff ff 5e 00 00 00
0070: 40 e6 d8 ff 5e 00 00 00  00 e7 d8 ff 5e 00 00 00
```

From Panfrost's `Compute Payload` spec:
- `+0x20`: Workgroup Sizes
- `+0x60`: `Shader Environment` Pointer -> `0x5effd8e5c8`
- `+0x68`: `FAU` (Thread Storage/Uniforms) Pointer -> `0x5effffe000`
- `+0x70`: `Resources` Pointer -> `0x5effd8e640`
- `+0x78`: `Thread Storage` Pointer -> `0x5effd8e700`

### Analyzing Atom 2: The Fragment Job (Type 9)
The control word is `0x00010012`
- `[7:1]`: Job Type = 9 (`Fragment`)
- `[31:16]`: Index = 1

The payload starting at `+0x20` points to the Framebuffer:
```text
0020: 00 00 00 00 00 00 00 00  81 fb d8 ff 5e 00 00 00
```
- `+0x20`: Bounds (Min/Max X/Y) = 0
- `+0x28`: Framebuffer Pointer = `0x5effd8fb81` (The `1` at the end means `Type = 1`).
So the FBD is located at `0x5effd8fb80`.

### The Framebuffer Descriptor (FBD)
```text
0000: 01 00 00 00 00 00 00 00  00 00 01 00 00 00 00 00
0010: 00 40 fc ff 5e 00 00 00  00 fa d8 ff 5e 00 00 00
0020: 00 00 01 00 00 00 00 00  00 00 01 00 00 90 03 00
```
- `+0x10`: Sample Locations -> `0x5efffc4000`
- `+0x18`: Frame Shader DCDs -> `0x5effd8fa00`
- `+0x28`: Width, Height, Bounds, Render Target Count
- `+0x80`: Render Target 0
```text
0080: 00 00 00 04 98 00 88 86  00 00 00 00 00 00 00 00
```
- Internal format and writeback parameters.

## What this means for us
Chrome does not use the massive 384-byte `Malloc Vertex Job`! Instead, it handles vertex processing by simply dispatching a standard `Compute Job` (Type 4, 128 bytes), which is much easier to construct! The Compute Job writes the transformed vertices to memory, and then the Tiler or Fragment jobs read them.

This vastly simplifies our path to the Triangle. We just need to reconstruct this exact `Compute Job -> Fragment Job` sequence with our own pointers.
