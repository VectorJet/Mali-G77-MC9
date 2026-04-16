# Chrome Job Submit Capture - 2026-04-15

Source:
- Wrapped `com.android.chrome` using `setprop wrap.com.android.chrome "LD_PRELOAD=/data/local/tmp/s.so"`
- Spy output collected from `logcat` tag `mali_ioctl_spy`
- Captured process: `com.android.chrome` PID `32435`

## Why this matters

This is the first confirmed capture of real `KBASE_IOCTL_JOB_SUBMIT` traffic from
the vendor GLES stack (`libGLES_mali.so` + `libgpud.so`) on this MT6893 / Mali-G77
device using the updated `ioctl_spy.so`.

It confirms:
- Real userspace uses `JOB_SUBMIT` with `stride=72`
- Real userspace emits both single-atom and batched submissions
- The current handwritten `WRITE_VALUE` path is structurally simpler than vendor
  submits and likely under-specifies `core_req` / dependencies / setup ioctls

## Observed ioctls near submit

Before several submits, Chrome emitted:
- `ioctl nr=0x19`, size 4, examples: `0x00000180`, `0x00000173`, `0x0000014d`, `0x0000013f`
- `ioctl nr=0x05` (`MEM_ALLOC`), size 32
- `ioctl nr=0x1b`, size 16, repeated after submit in the same process

These are strong candidates for vendor or higher-r49 scheduling / queue control
that should be cross-referenced with `mali_kbase` r49 headers and `libgpud.so`.

After adding lightweight decode in the spy:
- `ioctl nr=0x19` is a single `u32` control value
- observed values include:
  - `0x00000131`
  - `0x00000152`
  - `0x00000111`
  - `0x0000010e`
  - `0x00000108`
  - `0x00000153`
  - `0x00000161`
- `ioctl nr=0x1b` appears to carry a stable pointer plus one changing integer:
  - example pointer: `0xb400007d06892fb0`
  - observed `arg0` values include `0x148f`, `0x142e`, `0x13e7`
  - `arg1` was `0` in observed samples

This makes `0x19` look more like queue / state selection than a bulk data ioctl,
while `0x1b` looks more like a pointer-based control / notification call than a
memory allocation primitive.

## Atom format confirmation

All captured submits used:
- `magic = 0x80`
- `nr = 0x02`
- `stride = 72`

That matches the current Valhall-era `base_jd_atom` hypothesis better than the
older 48-byte / 64-byte notes.

## Working Single-Atom Example

Submit header:
```text
JOB_SUBMIT addr=0xb4000071466e61a0 nr_atoms=1 stride=72
```

Captured atom bytes:
```text
0000: 00 00 00 00 00 00 00 00 08 10 02 57 6d 00 00 00
0010: b0 e9 e6 46 6f 00 00 b4 00 b0 fd 27 71 00 00 b4
0020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0030: 01 00 00 00 03 02 00 00 00 00 00 00 00 00 00 00
0040: 00 00 00 00 00 00 00 00
```

Decoded fields:
- `seq_nr = 0x0`
- `jc = 0x0000006d57021008`
- `udata[0] = 0xb400006f46e6e9b0`
- `udata[1] = 0xb400007127fdb000`
- `extres_list = 0`
- `nr_extres = 0`
- `atom_number = 1`
- `prio = 0`
- `device/jobslot bytes = 0`
- `core_req = 0x00000203`

## Working Batched Example

Submit header:
```text
JOB_SUBMIT addr=0xb4000071466e61a0 nr_atoms=4 stride=72
```

Observed atom summary:

1. Atom 0
   - `seq_nr = 0x0`
   - `jc = 0xb400007086e50010` in one sample and `0xb400007086eb5250` in another
   - `atom_number = 1`
   - `core_req = 0x00000209`

2. Atom 1
   - `seq_nr = 0x11` or `0x36` in observed samples
   - `jc = 0x0000005effd46700` / `0x0000005effc47280`
   - `atom_number = 2`
   - `core_req = 0x0000004e`

3. Atom 2
   - `seq_nr = 0x11` or `0x36`
   - `jc = 0x0000005effd48080` / `0x0000005effc48c00`
   - `atom_number = 3`
   - `core_req = 0x00000001`

4. Atom 3
   - `seq_nr = 0x0`
   - `jc = 0xb400006f16e66bb0` / `0xb400006f16e67e30`
   - `atom_number = 4`
   - `core_req = 0x0000020a`

## Immediate comparison with `gpu_hello.c`

Current handwritten atom in `src/kbase/gpu_hello.c`:
- `seq_nr = 0`
- `jc = gpu_va`
- `udata[0] = 0x1234`
- `atom_number = 1`
- `core_req = 0x10`

Differences versus working Chrome submits:
- Real submits do not use `core_req = 0x10`; observed values include `0x203`,
  `0x209`, `0x4e`, `0x1`, and `0x20a`
- Real submits often batch multiple atoms together
- Real submits use realistic nontrivial `udata[0]` and `udata[1]` pointers
- Real submits use `jc` values from multiple GPU VA regions, including both low
  and tagged high VA ranges
- Real submits are often bracketed by `nr=0x19` and followed by repeated `nr=0x1b`

## Current limitation

The spy captured the atom array successfully, but many `jc` GPU VAs could not be
resolved back to CPU mappings:

```text
ATOM[0].jc gpu_va=0x6d57021008 not in tracked mmap ranges
ATOM[0].jc gpu_va=0xb400007086eb5250 not in tracked mmap ranges
```

That means the working descriptors are likely backed by GPU allocations that were
not CPU-mapped through the currently observed `mmap` path, or were mapped before
our spy saw them.

## MEM_ALLOC observations

After extending the spy to decode `KBASE_IOCTL_MEM_ALLOC`, Chrome was observed
issuing real alloc calls in the same wrapped process.

Examples:
```text
[SPY]   MEM_ALLOC decoded: out_flags=0x180280f gpu_va=0x41000 extension=0x0 in_flags=0x180280f
[SPY]   MEM_ALLOC decoded: out_flags=0x180380f gpu_va=0x41000 extension=0x0 in_flags=0x180380f
[SPY]   MEM_ALLOC decoded: out_flags=0x1802006 gpu_va=0x41000 extension=0x0 in_flags=0x1802006
```

Important implication:
- The wrapped Chrome process really is using `MEM_ALLOC`
- But the returned VA in observed samples is still the low range `0x41000`
- The real `jc` values seen in working submits are often in unrelated ranges like:
  - `0x6d57021008`
  - `0xb400006f46e7e890`
  - `0xb400007086eb1f50`

This means the unresolved `jc` pointers are probably not coming from the simple
allocations we currently observe and decode. More likely explanations:
- imported/shared allocations
- allocations established before the wrapped process began logging
- VA regions managed through another path than the currently correlated
  `MEM_ALLOC` + `mmap` sequence
- CPU-inaccessible GPU allocations used for real job descriptors
- pointer-bearing setup/state ioctls such as `0x1b` that may reference indirect
  tracking structures rather than raw job chain memory

## Next steps

1. Extend the spy to log `MEM_ALLOC` outputs with returned GPU VAs and flags.
2. Correlate `nr=0x19` and `nr=0x1b` with public r49 ioctls and `libgpud.so`.
3. Extend correlation beyond `MEM_ALLOC` to imported/shared or unmapped GPU VA
   regions so real `jc` pointers can be resolved.
4. Diff `core_req` usage against public kbase `BASE_JD_REQ_*` bits.
5. Capture a smaller deterministic workload if possible, but Chrome already proves
   that the submit path and atom stride are correct.
