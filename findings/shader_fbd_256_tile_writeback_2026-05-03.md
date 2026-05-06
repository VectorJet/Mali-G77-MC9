# 256×256 Pre Frame Shader tile-writeback investigation

## TL;DR

At `shader_fbd_256` (256×256 framebuffer, 16×16 tiles → 256 tiles total),
**~1–15% of pixels per run** show the GPU's clear-color sentinel
(`0xFF0000FF` for the GREEN shader, `0xFF00FF00` for the RED shader)
instead of the shader output.

The pattern is **non-deterministic across runs** but always **tile-aligned
or cache-line-aligned**. Affected tiles are usually fully clear-colored
(256 sentinel pixels = whole 16×16 tile) or partial in multiples of 16
(64 byte = one ARM cache line). 16×16 and 64×64 framebuffers are 100 %
clean every run.

This is **not fixed** by:

- A chained Cache Flush Job (Type 3, L2 Clean=1, Clean Shader Core LS=1,
  Job Manager Clean=1, Tiler Clean=1) — the job runs to Exception
  Status=0x1=DONE but the pattern persists.
- CPU memory barriers (`__sync_synchronize()`) and busy-wait delays
  before reading the color buffer.
- `BASE_MEM_CACHED_CPU` flag + `KBASE_IOCTL_MEM_SYNC` CSYNC — adding
  cached CPU mapping makes the GPU fault `DATA_INVALID_FAULT` (0x58)
  because we'd need to MSYNC the descriptors before submit.
- Toggling `Requires Helper Threads` in the SHADER_PROGRAM.
- Toggling `SHADER_SKIP_ATEST` (rules out uninit r60 → ATEST.discard).
- `SHADER_MINIMAL` (BLEND-only, no IADD/FADD setup) — same issue.
- Increasing `Color Buffer Allocation` from 1 (1024 B/tile) to 4
  (4 KiB/tile) — actually makes it noticeably worse (~50 % sentinel).
- Setting "Empty Tile Read/Write Enable" bits in FBP word 12 — causes
  `TRANSLATION_FAULT` at random VAs (needs valid ZS/CRC attachments).
- Setting the `Tiler` pointer in FBD word 14 (MFBD+0x38) — causes
  `DATA_INVALID_FAULT` because we have no valid Tiler Heap descriptor.

## What we observed

```
Run 1: 60920 green / 4616 red   — 28 affected tiles
Run 2: 50892 green / 14644 red  — 67 affected tiles
Run 3: 59404 green / 6132 red   — 32 affected tiles
…
```

Tile breakdown for one bad run:
```
y=8:  (12,8)(13,8)(14,8)(15,8)              — 4 fully red tiles, right edge
y=9:  (12,9)(13,9)(14,9)(15,9)              — same column, next row
y=10: (7,10)80 (8,10)(9,10)(10,10)(11,10)   — partial + 4 full
y=11: (6,11)(7,11)(8,11)(9,11)(10,11)(11,11)— 6 full tiles
y=12: (1,12)80 (3,12)
y=13: (0,13)208 (1,13)(2,13)(3,13)
…
```

Partial-tile counts of 80, 96, 208 = multiples of 16. 16 RGBA8 pixels =
64 bytes = one ARM cache line. The clusters are not random; they look
like work-distribution slices to individual shader cores (Mali-G77 MC9
has 9 cores).

## Hypothesis (unproven)

The Pre Frame Shader is run on each tile in parallel across the 9
shader cores. With no Tiler Context / no polygon list, the cores may
treat tiles as "empty", but Pre Frame Shader still executes. Some
cores' tile-RAM → memory writeback path is racing with subsequent
work or with our event-receive read. The cache-line-aligned partial
tiles support the cache-write-back-race theory, but the L2 Clean
Cache Flush Job did not fix it, so it is **not** the GPU's L2 cache
itself; it must be either a shader-core-internal write buffer or a
JM-level race between writeback completion and IRQ delivery.

## What's deferred

The systematic fix likely requires setting up a real **Tiler Context**
+ **Tiler Heap** + **Polygon List** so the GPU's tile scheduling has a
proper view of "what tiles to render". This is exactly what step #2 of
the breakthrough next-steps already calls for (vertex shader + tiler
job path), so we'll come back to this once that infrastructure exists.

### Update: Tiler Context wired (Phase A done)

**`SHADER_TILER=1`** now builds a valid `TILER_HEAP` descriptor (32
bytes at `OFF_TILER_HEAP_DESC=0xD500`) plus a `TILER_CONTEXT` (192
bytes at `OFF_TILER_CTX=0xD600`) backed by 256 KiB at
`OFF_TILER_HEAP_BACKING=0x80000`, and patches `MFBD+0x38` (FBP
word 14) to point at the context. The fragment job runs to
DONE without faulting.

**Result: does not fix the 256×256 issue.** Across 10 runs:

| Config            | Avg shader_color | Avg sentinel | %sentinel |
|-------------------|------------------|--------------|-----------|
| Without tiler ctx | 55 519           | 10 017       | 15 %      |
| With tiler ctx    | 53 786           | 11 751       | 18 %      |

The tiler context being NULL was therefore not the cause. The
hypothesis is now narrowed to **per-shader-core tile-writeback racing**
on Mali-G77 MC9 (9 cores). The fix likely requires actually populating
the polygon list (i.e. submitting a real Tiler Job before the Fragment
Job) so the GPU schedules tiles based on real coverage instead of
treating every tile as "Pre-Frame-only".

## What we did add

- `findings/shader_fbd_breakthrough_2026-04-29.md` already documented
  the working green/red shader.
- New diagnostic in `replay_egl_triangle.c`: per-tile breakdown of
  shader-color vs clear-sentinel pixels, with first/last sentinel
  pixel coords, list of affected tiles. Lets us measure the issue
  precisely.
- A chained **Cache Flush Job** (Type 3) at `OFF_SHADER_FLUSH_JC`
  (0xD400) with all Clean bits + L2 Clean. Executes successfully
  (Exception Status=0x1) and, while it doesn't fix the 256×256 issue,
  is correct GPU hygiene we'll keep around.
- Sentinel clear color is now picked orthogonal to the shader output
  (`SHADER_RED=1` uses green sentinel; default GREEN shader uses red
  sentinel). Previously the RED shader case couldn't distinguish
  shader output from clear because both were red.
- New env knob `SHADER_HELPERS` (default 1) to flip the SHADER_PROGRAM
  Requires Helpers bit for diagnostics.

## Verifying status

Working (100 % shader output) at 16×16 and 64×64 for both GREEN and
RED shaders. 256×256 shows the issue described above.

```
$ SHADER_PFM=1 bash scripts/run_replay_egl_triangle.sh shader_fbd
scratch_fbd: shader_color=256 clear_sentinel=0 other=0
scratch_fbd: first=0xff00ff00 last=0xff00ff00

$ SHADER_PFM=1 bash scripts/run_replay_egl_triangle.sh shader_fbd_64
scratch_fbd: shader_color=4096 clear_sentinel=0 other=0
scratch_fbd: first=0xff00ff00 last=0xff00ff00

$ SHADER_PFM=1 SHADER_RED=1 bash scripts/run_replay_egl_triangle.sh shader_fbd
scratch_fbd: shader_color=256 clear_sentinel=0 other=0
scratch_fbd: first=0xff0000ff last=0xff0000ff
```
