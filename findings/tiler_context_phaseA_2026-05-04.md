# Tiler Context infrastructure — Phase A of the geometry path

## What works now

We can build and wire a valid v9 `TILER_HEAP` descriptor + `TILER_CONTEXT`
into the FBD. Toggle with `SHADER_TILER=1` on top of the existing
`shader_fbd[_64|_256]` modes. Job runs to event code `0x1` (DONE) on
all framebuffer sizes, and 16×16 / 64×64 still produce 100 % shader
output. The 256×256 flaky-tile-writeback issue is not affected (see
[shader_fbd_256_tile_writeback_2026-05-03.md](shader_fbd_256_tile_writeback_2026-05-03.md))
— but at least we now know the tiler context being NULL was not the
cause.

## Layout of new structures

In our 1 MiB SAME_VA `MEM_ALLOC` (flags `0x200F`):

| Offset       | Bytes  | Purpose                       |
|--------------|--------|-------------------------------|
| `0xD500`     | 32     | `TILER_HEAP` descriptor       |
| `0xD600`     | 192    | `TILER_CONTEXT`               |
| `0x80000`    | 262144 | Tiler heap backing (256 KiB)  |

## TILER_HEAP descriptor (32 bytes / 8 dwords)

From v9.xml `Tiler Heap`:

```
word 0  =  Type=Buffer(9)        bits 0..3
        | Buffer type=Tiler heap(2)  bits 4..7
        | Chunk size=256 KiB(0)      bits 8..9
        | Partitioning=Dynamic(0)    bits 10..11
word 1  =  Size in bytes (4 KiB-aligned, we use 256 KiB = 0x40000)
words 2/3 = Base   = gva + OFF_TILER_HEAP_BACKING
words 4/5 = Bottom = Base
words 6/7 = Top    = Base + Size
```

## TILER_CONTEXT (192 bytes, 64-byte aligned)

```
words 0/1   = Polygon List      (NULL — populated by tiler at runtime)
word 2      = Hierarchy Mask=1  (bit 0 set)
            | Sample Pattern=Single-sampled (default)
word 3      = (FB Width-1) | ((FB Height-1) << 16)
word 4      = Layer count-1 = 0 (single layer)
word 5      = padding
words 6/7   = Heap pointer = gva + OFF_TILER_HEAP_DESC
words 8..15 = Weights (zero)
words 16..31= State   (zero — initialized by GPU)
```

## FBD wiring

The FBD's `Tiler` field is at FBP word 14 = `MFBD + 0x20 + 24` =
`MFBD+0x38`. We write the 64-bit context address there. With the
context NULL the field stays zero and the GPU is happy with
Pre-Frame-Shader-only rendering. Setting it requires the descriptor
above to be valid; otherwise we get `DATA_INVALID_FAULT (0x58)`.

## Things that fault if you misconfigure

| Symptom                                           | Cause                                            |
|---------------------------------------------------|--------------------------------------------------|
| `0x58 DATA_INVALID_FAULT`                         | Tiler ptr set but heap descriptor zeroed         |
| `0xC0 TRANSLATION_FAULT` at random VA             | Empty Tile Read/Write Enable without ZS/CRC      |
| `0xC0 TRANSLATION_FAULT` at small VA              | `BASE_MEM_CACHED_CPU` flag without MSYNC pre-job |

## Code

[`build_tiler_context()`](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/kbase/replay/replay_egl_triangle.c)
in `replay_egl_triangle.c`. Invoked from `build_shader_fbd()` after
the rest of the FBD is built, when `SHADER_TILER=1`.

## Phase B (next)

To render real triangles we still need:

1. A **Valhall vertex shader** that writes `gl_Position` for a
   full-screen triangle (vid 0→(-1,-1), 1→(3,-1), 2→(-1,3)).
   The output store opcode is currently unknown; assembler-cases.txt
   has `STORE_TILED`, `LD_VAR`, and `VAR_TEX_SINGLE` but no clear
   "store position" example. Need to capture a vertex shader from
   real EGL traffic via `ioctl_spy.c` and study the disassembly.
2. A **Malloc Vertex Job** (Type 11, 384 bytes) at e.g. `0xD700`
   with the Primitive section (Triangles, index_count=3),
   Allocation, Tiler Pointer (→ our TILER_CONTEXT), Scissor, Draw
   (fragment shader env), Position (vertex shader env).
3. A submission chain: `Malloc Vertex → Cache Flush → Fragment`
   either as a single atom (Header.Next chain) or as two atoms with
   slot dependency (FS on slot 0, vertex+tiler on slot 1).
