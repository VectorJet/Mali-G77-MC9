# Multi-Tile Clear Verified Across Framebuffer Sizes

**Date:** 2026-04-22
**Status:** Confirmed working

## Summary

Scaled the from-scratch MFBD fragment job from 16×16 to 64×64 and 256×256. All three sizes produce correct solid red clear output (every pixel = `0xFF0000FF`) via the GPU's tile-based clear-and-writeback mechanism. No shader execution needed.

## Results

| Size | Pixels | Tiles (16×16) | JC Bounds Max | Event | Status |
|------|--------|---------------|---------------|-------|--------|
| 16×16 | 256 | 1×1 | (0,0) | 0x1 | ✅ |
| 64×64 | 4,096 | 4×4 | (3,3) | 0x1 | ✅ |
| 256×256 | 65,536 | 16×16 | (15,15) | 0x1 | ✅ |

## Key Parameters for Multi-Tile Clear

The same MFBD configuration works at all sizes. Only these fields change with framebuffer dimensions:

### MFBD Multi-Target Parameters (offset 0x20)
- **Word 0**: `(width-1) | ((height-1) << 16)` — pixel dimensions, minus(1) encoded
- **Word 2**: `(width-1) | ((height-1) << 16)` — Bound Max X/Y in pixels
- **Word 3**: Effective Tile Size and Color Buffer Allocation stay constant at `(8 << 9) | (1 << 24)` — tile size is always 16×16 (log2=8), internal buffer always 1024 bytes for RGBA8

### Fragment JC Payload (offset 0x20 from JC base)
- **Word 0**: Bound Min = 0 (tile coordinates)
- **Word 1**: `((width/16)-1) | (((height/16)-1) << 16)` — tile coordinates (MALI_TILE_SHIFT=4)

### RT Descriptor
- **Row Stride** (RT+0x28): `width * 4` for RGBA8

## Memory Layout

For large framebuffers, color buffer moved to avoid overlapping captured page data:
- 16×16: `OFF_SCRATCH_COLOR = 0xA000` (1024 bytes)
- 64×64+: `OFF_SCRATCH_COLOR_LG = 0x40000` (up to 262,144 bytes for 256×256)

SAME_VA allocation increased from 64 → 256 pages (1 MB).

## Modes

```
scratch_fbd       # 16×16 solid red clear
scratch_fbd_64    # 64×64 solid red clear (4×4 tiles)
scratch_fbd_256   # 256×256 solid red clear (16×16 tiles)
```

## ioctl_spy Enhancements

- Capture limit raised 10 → 50 JOB_SUBMITs
- Added MFBD field logging: PrePost modes, SampleLoc, DCD, Tiler pointers
- Added MFBD params logging: Width, Height, CbufAlloc, TileSize, RTCount
- Added RT descriptor logging/capture: IntFmt, WriteEnable, BlockFmt, Writeback address
- FBD capture expanded 256 → 512 bytes
