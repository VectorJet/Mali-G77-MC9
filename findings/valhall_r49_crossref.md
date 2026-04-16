# Valhall/r49 Cross-Reference Findings

## ARM Driver Version
- **r49p1** = MediaTek's Mali-G77 driver revision (Valhall architecture)
- Build path: `vendor/mediatek/proprietary/hardware/gpu_mali/mali_avalon/r49p1/`

## Mesa Panfrost Status

### Architecture Support
| Architecture | Panfrost Support | Notes |
|--------------|-------------------|-------|
| Midgard | Full | Older Mali-Txxx |
| Bifrost | Full | Mali-G5x, G6x |
| **Valhall** | Partial (Mesa 22.2+) | Mali-G7x (G77, G78) |

### Valhall GPU IDs in Mesa
Based on Mesa source inspection:
- `pan_is_bifrost()` checks `arch >= 6 && arch <= 7`
- Arch 10+ appears to be Valhall
- Mesa targets Valhall via `panfrost_device_kmod_version_major()`

### Key Mesa Code References
- `pan_valhall.h` - Valhall-specific header (likely in Mesa tree)
- `pan_device.c` - Device initialization with arch-based conditional
- `panfrost.h` - Main driver header

## Gap Analysis

| Aspect | ARM r49p1 | Mesa Panfrost | Status |
|--------|-----------|---------------|--------|
| ISA | Valhall (t7xx) | Valhall | Likely compatible |
| Job descriptors | r49 format | Valhall format | Unknown - needs verification |
| Fragment shader | Valhall | Valhall | ✓ Supported |
| Vertex/tiler | Valhall | Valhall | ✓ Supported |
| Compute | Valhall | Valhall | ✓ Supported |

## Conclusion
- **ISA compatibility**: Likely maintained across r49 revisions
- **Job descriptors**: Not verified - this is where r49p1 may differ from Mesa's expectations
- The blob uses `libgpud.so` for actual command emission - descriptor format changes would be there, not in this dispatcher library

## Next Steps
To cross-reference job descriptors, examine:
1. `libgpud.so` - actual GPU command builder
2. Compare against Mesa's `src/gallium/drivers/panfrost/pan_job.c` descriptor emission