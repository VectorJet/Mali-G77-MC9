# Shader FBD First Attempt — 2026-04-27

## Summary

Implemented the first iteration of `shader_fbd` mode in
`src/kbase/replay/replay_egl_triangle.c`, which extends the proven
`scratch_fbd` clear-only path with a real Valhall fragment shader that writes
solid green to RT0.

**Result**: Job submission accepted, GPU runs, but returns event code
`0x04 = BASE_JD_EVENT_TERMINATED` — job was hard-stopped (a fault or
inability to make progress). No pixels written.

## What was built

### 1. Minimal Valhall fragment shader (56 bytes / 7 instructions)

Encodings extracted from
`refs/panfrost/src/panfrost/compiler/bifrost/valhall/test/assembler-cases.txt`:

```
IADD_IMM.i32 r0, 0x0, #0x0           c0 00 00 00 00 c0 10 01
FADD.f32     r1, r0, 0x3F800000      00 d0 00 00 00 c1 a4 00
IADD_IMM.i32 r2, 0x0, #0x0           c0 00 00 00 00 c2 10 01
FADD.f32     r3, r0, 0x3F800000      00 d0 00 00 00 c3 a4 00
NOP.wait0126                         00 00 00 00 00 c0 00 40
ATEST.discard @r60, r60,
              0x3F800000, atest_datum 3c d0 ea 00 02 bc 7d 68
BLEND.slot0.v4.f32.end @r0:r1:r2:r3,
              blend_descriptor_0,
              r60, target:0x0         f0 00 3c 32 08 40 7f 78
```

Output `(r0,r1,r2,r3) = (0.0, 1.0, 0.0, 1.0)` = solid green.

### 2. Blend descriptor (16 bytes, fixed-function REPLACE on RGBA8)

Layout based on `refs/panfrost/src/panfrost/genxml/v9.xml` `Blend` struct:
- word 0: `Enable=1` (bit 9)
- word 1: Equation `0xF0122122` (RGB and Alpha both `Src+Src*Zero`,
  Color Mask = RGBA)
- word 2: Internal Blend `Mode=Fixed-Function(2), num_comps-1=3, RT=0`
- word 3: Conversion `(RGBA8_TB << 12) | RGBA = 0xED000`

### 3. v9 Draw / DCD descriptor (128 bytes)

- Flags 0 = `0x43` (allow forward pixel kill, primitive reorder)
- Flags 1 = `0x0001FFFF` (sample mask 0xFFFF, RT mask = 0x1)
- Min Z = 0.0, Max Z = 1.0
- Blend count + pointer at word 12 (`count=1 | blend_ptr`)
- Shader Environment at DCD+0x40:
  - Shader pointer at DCD+0x68
  - TLS pointer at DCD+0x70 (32-byte zeroed Local Storage descriptor)
  - Resources, FAU = 0

### 4. MFBD patches

- `Pre Frame 0 = Always` (word 0 = 1)
- Frame Shader DCDs pointer at MFBD+0x18 set to the new DCD

## Observed behavior

```
JOB_SUBMIT (scratch_fbd) ret=0 errno=0 (Success)
event[0] read=24 code=0x4 atom=1 data0=0x0 data1=0x0
scratch_fbd: NO pixels written
```

The kernel reports `BASE_JD_EVENT_TERMINATED = 0x04`, which generally
indicates the job was hard-stopped (timeout or kernel fault detection)
without a more specific instruction/config fault code being set. The
kernel `dmesg` shows no Mali errors during the run.

## Likely root causes (to investigate next)

The successful clear-only path used `Pre Frame 0 = Never` and the GPU
performed an internal clear-and-writeback. Now that we have set
`Pre Frame 0 = Always` plus a Frame Shader DCD pointer, the GPU is
attempting to actually run our shader and is failing somewhere. Most
suspicious areas:

1. **Frame Shader DCDs pointer format**. The v9 XML calls this field a
   plain `address`. Captured vendor traffic might encode it with
   a low-bit tag (similar to the FBD pointer in the fragment job
   header). We should compare to a captured working pre-frame DCD ptr.

2. **DCD Flags 0/1 values**. Set conservatively from XML defaults but
   we have not validated against a captured fragment DCD. In particular,
   `Multisample enable` is off, which may mismatch the MFBD's sample
   pattern.

3. **Vertex Array section in DCD**. v9 `Draw` has a 96-bit Vertex Array
   at words 2-4. We left it zeroed. For a pre-frame shader (no actual
   draw geometry — it runs once per pixel as a clear-replacement) this
   may be fine, but it may also need specific values.

4. **TLS / Local Storage**. We provided a 32-byte zero descriptor.
   This might be OK for a shader with no register spilling, but
   Valhall may require specific fields (Type=Local Storage, sizes).
   The `Buffer` struct in v9.xml has `Type=Buffer` at word 0, low 4 bits.

5. **Blend descriptor packing**. The 22-bit Pixel Format field
   composition `(RGBA8_TB << 12) | RGBA` was inferred from
   `refs/panfrost/src/panfrost/lib/pan_format.c` `MALI_BLEND_PU_R8G8B8A8`.
   Worth comparing byte-for-byte against a captured Blend descriptor.

6. **Shader Register File Format**. Our shader uses f32 BLEND.
   `cfg.fixed_function.conversion.register_format` is a v9 field
   (Register File Format enum: F16=0, F32=1, ...). On v9 this is
   placed somewhere in the Internal Blend Conversion words. We did
   NOT explicitly encode F32 — defaulted to 0 (F16), which conflicts
   with our `BLEND.v4.f32` instruction.

7. **Shader Resources pointer**. NULL might be invalid. Vendor
   shaders observed pointing at a real Resources table even when
   no textures are used (it provides the fragment intrinsics like
   `gl_FragCoord` machinery).

## Next steps

1. **Capture a real fragment DCD** with the enhanced ioctl_spy from a
   live EGL render. Compare byte-for-byte against the synthetic DCD
   we built.

2. **Fix the register format mismatch**: encode F32 in the blend
   conversion field. Find the exact bit position in v9 by inspecting
   `pan_blend.h` / `pan_format.h` macros.

3. **Try a simpler shader**: just `BLEND.slot0.v4.f32.end` with r0-r3
   pre-zeroed by the GPU's startup state, skip ATEST. If still
   terminated, the failure is structural (DCD/MFBD), not the shader.

4. **Inspect job header exception status post-failure**. v9 Job Header
   has `Exception Status` at offset 0 of the JC. After the failed run
   the JC dump should contain a non-zero value.

## Files changed

- `src/kbase/replay/replay_egl_triangle.c` — added
  - `OFF_SHADER_ISA/DCD/BLEND/TLS` offsets
  - `k_valhall_green_fs[]` shader binary
  - `build_shader_fbd()` builder
  - `is_shader_mode` dispatch in main()

## How to run

```
bash scripts/run_replay_egl_triangle.sh shader_fbd
bash scripts/run_replay_egl_triangle.sh shader_fbd_64
bash scripts/run_replay_egl_triangle.sh shader_fbd_256
```
