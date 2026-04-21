# From-Scratch MFBD Fragment Job: First Pixel Output

**Date:** 2026-04-21
**Device:** Mali-G77 MC9 / MT6893
**Mode:** `scratch_fbd`

## Summary

Successfully produced visible pixel output from a fully from-scratch MFBD fragment job on the Mali-G77 MC9. A 16×16 RGBA8 color buffer was filled with solid red (`0xFF0000FF`) by the GPU's tile-based clear-and-writeback mechanism — no shader execution needed.

## Key Discovery: Clear-Only Fragment Job

The GPU's fragment engine can produce pixel output through the **tile clear + writeback** path alone, without any shader, geometry, or tiler. The correct configuration is:

### What WORKS (produces pixels):
- **All Pre/Post Frame modes = Never (0)** — no frame shader DCDs
- **Frame Shader DCDs pointer = NULL (0)**
- **Bifrost Tiler Pointer = NULL (0)** — no geometry
- **Sample Locations pointer = valid** (must point to a valid sample locations table)
- **RT Write Enable = 1**, Block Format v7 = 2 (Linear)
- **Color Buffer Allocation ≠ 0** (1024 for RGBA8 at 16×16 tiles)
- **Clear Color = packed integer RGBA8**, replicated to all 4 RT Clear words

### What FAILS (event 0x58 = config fault):
- Pre/Post Frame modes = Never but DCDs pointer != 0
- Pre/Post Frame modes = Never but missing sample locations
- Any non-zero Pre Frame mode with a null/invalid DCD shader

### What SILENTLY NO-OPS (event 0x4 but no output):
- Pre Frame 0 = Always with a null shader (address = 0 in Renderer State)
- Pre Frame 0 = Always with a valid DCD but Write Enable = 0 or Color Buffer Allocation = 0

## MFBD Layout for Clear-Only Fragment Job

### Bifrost Parameters (MFBD+0x00, 32 bytes)
```
Word 0 (0x00): 0x00000000  — All pre/post frame modes = Never
Word 1 (0x04): 0x00000000  — No Local Storage overlay
Words 2-3 (0x08): 0  — No TLS
Words 4-5 (0x10): sample_locations_gpu_va  — REQUIRED, even for 1x MSAA
Words 6-7 (0x18): 0  — No Frame Shader DCDs
```

### Multi-Target FB Parameters (MFBD+0x20, 24 bytes)
```
Word 0 (0x20): width-1 | (height-1 << 16)
Word 1 (0x24): 0  — Bound Min X/Y = 0
Word 2 (0x28): (width-1) | ((height-1) << 16)  — Bound Max
Word 3 (0x2C): (tile_log2 << 9) | (1 << 24)  — Effective Tile Size + Color Buffer Alloc
Word 4 (0x30): (1 << 16)  — Z Internal Format = D24
```

### Bifrost Tiler Pointer (MFBD+0x38) = 0
### Bifrost Padding (MFBD+0x40) = zero

### RT Descriptor (MFBD+0x80, 64 bytes)
```
Word 0 (RT+0x00): (1 << 26)  — Internal Format = R8G8B8A8
Word 1 (RT+0x04): 1 | (19<<3) | (2<<8) | (1<<15) | (swizzle<<16) | (1u<<31)
    Write Enable=1, Format=R8G8B8A8(19), Block=Linear(2), Dither=1, Clean=1
Words 8-9 (RT+0x20): color_buffer_gpu_va
Word 10 (RT+0x28): row_stride_bytes (e.g., 64 for 16×4)
Words 12-15 (RT+0x30): packed_clear_color × 4 (replicated)
```

### Fragment JC (64 bytes)
```
Word 4 (0x10): (1<<0) | (9<<1)  — Is_64b=1, Type=9 (Fragment)
Word 8 (0x20): 0  — Bound Min X/Y
Word 9 (0x24): 0  — Bound Max X/Y (single tile)
Words 10-11 (0x28): mfbd_gpu_va | 0x01  — FBD pointer with IS_MFBD tag
```

### Sample Locations Table (192 bytes)
```
Entry 0: (128, 128) — center of pixel (1x MSAA)
Entries 1-31: (0, 256)
Entry 32: (128, 128)
Entries 33-47: (0, 0)
```

## Clear Color Encoding

For RGBA8 UNORM, the clear color is **packed as integer bytes** in `(A<<24)|(B<<16)|(G<<8)|(R<<0)` format, replicated to all 4 clear color words. **Not** as float values.

Examples:
- Red: `0xFF0000FF` (R=255, G=0, B=0, A=255)
- Green: `0xFF00FF00`
- Blue: `0xFFFF0000`
- White: `0xFFFFFFFF`

## Atom Configuration
- `core_req = 0x001` (FS only)
- Single fragment-only atom (no compute dependency needed)
- Event code on success: `0x1` (not `0x4` as with compute+fragment chains)

## Why Previous Attempts Failed

1. **Pre Frame 0 = Always with null shader**: The GPU "executes" the pre-frame DCD but a null shader address causes the tile buffer to never be initialized. The GPU reports success (0x4) but skips the entire tile walk.

2. **Captured fragment shader DCD**: The captured vendor DCD is a depth-clear pre-frame shader (Pre Frame 0 = Always). It initializes depth but doesn't touch the color tile buffer. Even with Write Enable=1 on the RT, the color buffer stays empty because the shader doesn't write to RT0.

3. **Clear color as float values**: The RT Clear fields expect packed integer format matching the RT's internal format, not IEEE-754 floats.

## Next Steps

1. **Scale up**: Test with larger framebuffers (64×64, 256×256) to verify multi-tile handling
2. **Add geometry**: Build a Bifrost Tiler with a full-screen triangle polygon list to test actual fragment shading
3. **Capture the draw pass**: The vendor driver's color-producing fragment shader is in a separate JOB_SUBMIT call that was not captured
