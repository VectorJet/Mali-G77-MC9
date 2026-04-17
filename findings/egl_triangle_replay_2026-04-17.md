# EGL Triangle Replay Progress

**Date:** 2026-04-17  
**Device:** Mali-G77-MC9 / MT6893  
**Driver path:** `/vendor/lib64/egl/mt6893/libGLES_mali.so`

## Summary

The direct root-launched vendor EGL path now gives a clean capture and a partially working replay:

- root-running `/data/local/tmp/egl_dumper` succeeds
- direct `LD_PRELOAD=/data/local/tmp/ioctl_spy.so /data/local/tmp/egl_dumper` capture succeeds
- compute + fragment hardware atoms can now be replayed successfully from captured bytes in a fresh SAME_VA allocation
- the replayed hardware atoms complete cleanly, but visible output is still missing because the render-target side is still preserved vendor state rather than an output path we clearly own

This means the bottleneck has moved:

- **no longer** job submission or shader relocation in general
- **now** the unresolved problem is the fragment output target / render-target descriptor chain

## Capture Results

The direct-preload capture produced:

- `001_atoms_raw.bin`
- `001_atom0_soft_jc.bin`
- `001_atom1_hw_jc.bin`
- `001_atom1_compute_shader_isa.bin`
- `001_atom1_compute_fau.bin`
- `001_atom1_compute_thread_storage.bin`
- `001_atom1_compute_resources.bin`
- `001_atom2_hw_jc.bin`
- `001_atom2_frag_fbd.bin`
- `001_atom2_frag_shader_dcd.bin`
- `001_atom2_frag_shader_isa.bin`
- `001_atom2_frag_fau.bin`
- `001_atom2_frag_tls.bin`
- `001_atom2_frag_resources.bin`
- closure pages around the fragment FAU / TLS / DCD / arena

Artifacts live in:

- `captured_shaders/egl_dumper_root_preload_2026-04-17/`

## Atom Chain

The intercepted `JOB_SUBMIT` contains 4 atoms:

1. atom 0: soft job, `core_req=0x209`
2. atom 1: hardware compute, `core_req=0x4e`, job type `4`
3. atom 2: hardware fragment, `core_req=0x1`, job type `9`
4. atom 3: soft job, `core_req=0x20a`

Dependencies:

- atom 1 depends on atom 0
- atom 2 depends on atom 1
- atom 3 depends on atom 2 with dep type `2`

## Fragment ISA Relocations

The new fragment shader blob is much cleaner than the earlier vkmark-style capture, but it is not relocation-free.

Observed embedded `0x5eff...` addresses inside `001_atom2_frag_shader_isa.bin`:

- `0x020 -> 0x5effe9d4c0`
- `0x048 -> 0x5effe9e460`
- `0x100 -> 0x5effe9e480`
- `0x140 -> 0x5effe9d4c0`
- `0x1d8 -> 0x5effe9de80`
- `0x1e8 -> 0x5effe9e181`

Interpretation:

- `0x20` and `0x140` point at the FAU[0] target
- `0x48` and `0x100` are self-relative pointers into the fragment ISA block
- the tail contains descriptor-like trailer state rather than pure instruction bytes

So the replay still requires pointer patching, but the old "17-page relocation nightmare" is gone.

## Replay Implementation

Created:

- `src/kbase/replay_egl_triangle.c`
- `scripts/run_replay_egl_triangle.sh`

The replay loader:

- allocates one SAME_VA block
- loads captured soft jobs, compute job, fragment job, and selected closure pages
- relocates absolute `0x5eff...` addresses into the new SAME_VA mapping
- reconstructs the atom batch from `001_atoms_raw.bin`
- supports:
  - `exact4` mode: preserve the 4-atom submit
  - `hw2` mode: submit only the compute -> fragment hardware chain

## Replay Results

### `exact4` mode

Result:

- `JOB_SUBMIT ret=0`
- first event `0x4003` on atom 1

Interpretation:

- the replayed soft-job payload for atom 0 is still not valid outside the original vendor process context
- preserving the exact 4-atom submit is therefore not yet viable

### `hw2` mode

Result:

- `JOB_SUBMIT ret=0`
- event drain shows:
  - `event[0] code=0x4 atom=1`
  - `event[1] code=0x4 atom=2`

Interpretation:

- the replayed compute atom completes
- the replayed fragment atom completes
- the relocation and descriptor patching are good enough for successful hardware execution of the 2-atom chain

This is the strongest replay result so far.

### Render-target probe modes

Additional replay probes were added:

- `hw2_zero_fc4000`
- `hw2_zero_fc2540`
- `hw2_hybrid_fbd`
- `hw2_hybrid_fbd_dcd28_color`
- `hw3_hybrid_fbd_dcd28_color_soft3`

Observed results:

- zeroing the entire relocated `fc4000` page still allows atom 2 to complete
- zeroing the `fc2540` region inside `fc2000` still allows atom 2 to complete
- replacing the vendor FBD/RT chain with a simple owned SFBD/RT chain still allows atom 2 to complete, but the owned color buffer stays unchanged
- additionally patching `fragment DCD + 0x28` to the owned color buffer still allows atom 2 to complete, but the owned color buffer stays unchanged

This means:

- neither `fc4000` nor `fc2540` alone is the decisive execution dependency
- the fragment hardware atom can complete even when those regions are replaced or redirected
- visible output is therefore still being resolved through some other packed render-target state path

### Soft-tail replay test

Tested a 3-atom chain:

- compute
- fragment
- captured soft atom 3

with the hybrid owned FBD + `DCD+0x28 -> owned color` setup.

Result:

- atom 1 completed: event `0x4`
- atom 2 completed: event `0x4`
- atom 3 failed: event `0x4003` (`JOB_INVALID`)
- owned color buffer remained unchanged

Interpretation:

- the captured tail soft job is not trivially portable outside the original vendor process
- it is not currently giving us the missing "resolve to owned output" step

## Why There Is Still No Visible Output

Even though the compute + fragment chain completes successfully:

- none of the tracked captured pages change during replay
- no obvious red pixel or owned color buffer is produced

This strongly suggests the fragment output path still targets preserved vendor-internal render-target state rather than a buffer we explicitly control.

## Render-Target Side Findings

Key pointers on the fragment side:

- FBD + `0x10` -> `0x5efffc4000`
- FBD + `0x18` -> `0x5effe9e000` (fragment DCD)
- DCD + `0x28` -> `0x5efffc2540`

Additional missing side pages had to be captured from the DCD/TLS closure:

- `0x5efffc2000`
- `0x5efffc1000`
- `0x5effeb9000`

Observations:

- `0x5efffc4000` does not look like a simple linear color buffer pointer page
- `0x5efffc2540` inside the `fc2000` page looks like structured state / LUT-style data, not a direct pointer table
- the fragment FAU target page `0x5effe9d000` is still being mutated during replay even when the owned color path is injected
- replay completion without visible output is therefore consistent with the fragment stage still resolving its actual render target through packed descriptor state that we have not yet retargeted

## Current Conclusion

The project crossed an important boundary:

- captured compute + fragment vendor jobs are now replayable in fresh SAME_VA memory
- the remaining problem is not shader ISA replay or atom submission
- the remaining problem is **retargeting the fragment output path**

## Next Step

The most promising next step is to turn the replay into a controlled render-target probe:

1. identify which fields in the `fc2000` / `fc4000` render-target descriptor chain govern the final output location
2. patch those fields to point at a fresh output page inside the replay SAME_VA block
3. rerun the 2-atom compute -> fragment chain
4. verify that the owned output page changes to the expected red result
