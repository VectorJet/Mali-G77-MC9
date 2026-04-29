# Hand-Crafted Valhall Fragment Shader — BREAKTHROUGH 2026-04-29

## Result

The `shader_fbd` mode in `src/kbase/replay/replay_egl_triangle.c` now
successfully runs a hand-crafted 7-instruction Valhall (v9) fragment
shader on Mali-G77 MC9 and produces solid green pixels.

```
JOB_SUBMIT (scratch_fbd) ret=0 errno=0 (Success)
event[0] read=24 code=0x1 atom=1 data0=0x0 data1=0x0
color[0] (0,0) = 0xff00ff00
...
scratch_fbd: color changed=256 / 256 (16x16)
scratch_fbd: first=0xff00ff00 last=0xff00ff00
```

`0xff00ff00` in ABGR8 = (R=0, G=0xff, B=0, A=0xff) = solid green,
exactly what the shader specified via `(r0,r1,r2,r3) = (0.0, 1.0, 0.0, 1.0)`.

Verified working at:
- 16×16   — 100% green
- 64×64   — 100% green
- 256×256 — mostly green (some tile-edge red sentinel left over;
  follow-up to clean up)

## The Valhall fragment shader (56 bytes / 7 instructions)

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

Encodings extracted verbatim from
`refs/panfrost/src/panfrost/compiler/bifrost/valhall/test/assembler-cases.txt`.

## What it took to get here

After the first attempt (commit ec38923) returned
`BASE_JD_EVENT_TERMINATED = 0x04`, the failure was traced through
several layers:

### 1. Frame Shader DCDs is an array of 3 DRAW structs

`pan_fb_preload.c::pan_preload_fb_alloc_pre_post_dcds` allocates
**3 DRAW descriptors** — one per pre/post slot. The pointer at MFBD+0x18
indexes into this array as `[Pre Frame 0, Pre Frame 1, Post Frame]`.
We now allocate `3 × 128 = 384 bytes` and only populate index 0.

### 2. DCD.Shader points to SHADER_PROGRAM, not raw ISA

In v9, `DCD Shader Environment + 0x28` (the `Shader` address field)
points to a 32-byte `SHADER_PROGRAM` struct that wraps the binary.
Format:

```
word 0: Type=Shader(8) | Stage=Fragment(2) | CovBitmask=GL(1)
        | RequiresHelpers | RegAlloc=32Per(2)
word 1: Preload mask (16 bits)
word 2-3: Binary address (the raw ISA VA)
```

### 3. DCD.Resources is `(table_va | table_count)`

Resource tables are 64-byte aligned, so the low 6 bits of the pointer
encode the count. We use a 1-table dummy with `ptr | 1`.

### 4. DCD.Depth/stencil must point to a valid Depth/stencil descriptor

`pan_preload_emit_zs` builds a 32-byte Depth/stencil descriptor for
**every** pre-frame DCD (color or zs). Setting it to NULL caused the
NULL deref. Minimum viable:

```
word 0: Type=Depth/stencil(7) | FrontFunc=Always(7) | BackFunc=Always(7)
word 4: DepthCullEnable=1 | DepthFunc=Always(7)
```

### 5. Shader binaries need a separate GPU_EX allocation

The big one. Our main SAME_VA allocation has flags `0x200F`
(CPU_RD/WR + GPU_RD/WR + SAME_VA), which **does not include
`BASE_MEM_PROT_GPU_EX` (bit 4)**. When the GPU tried to fetch shader
instructions, it got a `PERMISSION_FAULT` (exception type 0xCB).

The fix: allocate a separate page with flags
`CPU_RD | CPU_WR | GPU_RD | GPU_EX | SAME_VA = 0x2015`
and put the shader binary there. Note: `GPU_EX` cannot be combined
with `GPU_WR`.

```c
uint64_t mem_ex[4] = { 1, 1, 0,
                       0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x2000 };
ioctl(fd, KBASE_IOCTL_MEM_ALLOC, mem_ex);
mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mem_ex[1]);
```

### 6. Local Storage descriptor needs valid TLS Base

The 32-byte Local Storage descriptor at `DCD+0x70` was zeroed,
which embeds a NULL TLS base. Setting:
- TLS Size = 0
- WLS Instances = `MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM` (0x80000000)
- TLS Base Pointer = a valid backing VA (zeroed scratch buffer)

### 7. Blend descriptor (16 bytes)

```
word 0: Enable=1 (bit 9)
word 1: 0xF0122122  // RGB+Alpha both A=Src(2), B=Src(2), C=Zero(1),
                   // ColorMask=RGBA(0xF)
word 2: Mode=Fixed-Function(2) | NumComps-1=3 | RT=0
word 3: Conversion = (RGBA8_TB << 12) | RGBA = 0xED000
```

## Diagnostic infrastructure that was critical

Three env knobs allowed isolating each failure mode quickly:
- `SHADER_PFM`        — Pre Frame Mode (0=Never, 1=Always, 2=Intersect, 3=Early ZS)
- `SHADER_MINIMAL`    — only emit `BLEND.end` (rules out shader logic bugs)
- `SHADER_SKIP_ATEST` — skip NOP+ATEST (isolates ATEST as suspect)

Reading `dmesg` after each TERMINATED event gave the exact GPU fault
type (NULL deref vs PERMISSION_FAULT vs INSTR_INVALID), which
pinpointed the next field to fix.

## Run

```
SHADER_PFM=1 bash scripts/run_replay_egl_triangle.sh shader_fbd
SHADER_PFM=1 bash scripts/run_replay_egl_triangle.sh shader_fbd_64
SHADER_PFM=1 bash scripts/run_replay_egl_triangle.sh shader_fbd_256
```

## Next steps

1. Clean up the 256×256 tile-edge red speckle.
2. Try varying the shader output color (sanity check that we're
   actually controlling pixels, not coincidentally matching a clear).
3. Add a vertex shader + tiler so we can render a real triangle
   (rather than full-screen pre-frame fills).
