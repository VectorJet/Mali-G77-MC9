# Fragment JC (Atom 2) Fix & Valhall v9 Hardware Pipeline Success

**Date**: 2026-07-29  
**Status**: **COMPLETE SUCCESS**. All 3 atoms (`TILER_JOB`, `Cache Flush`, `Fragment JC`) execute with **`0x1 DONE`**!

---

## 1. Summary of Achievements

After months of encountering `0x58 DATA_INVALID_FAULT` on `Atom 2: Fragment JC`, we identified and resolved the root causes:

1. **`core_req` Hardware Queue Routing**:
   - `tri_atoms[2].core_req` was previously set to `0x04E` (`PROTECTED | TILER | CS | COHERENT`), which assigned the Fragment job to the **Tiler hardware queue**.
   - Changing `core_req` to **`0x041`** (`BASE_JD_REQ_FS | BASE_JD_REQ_COHERENT_GROUP`) correctly routes the Fragment job to the **Fragment hardware engine**.

2. **Frame Shader DCD & Draw Descriptor Pointer Encoding**:
   - On Valhall v9, `dcd[12..13]` (Blend pointer) and `dcd[24..25]` (Resource Table pointer) are 64-bit GPU Virtual Addresses OR'd with count flags (`1ULL | blend_addr` and `1ULL | resource_addr`), **not** shifted `blend_addr >> 4` bitfields.
   - Fixed `dw[12]`, `dw[24]`, `dcd[12]`, and `dcd[24]` in `replay_egl_triangle.c`.

3. **FAU Field Configuration**:
   - For non-uniform fragment shaders (e.g. `k_valhall_green_fs`), `dcd[17]` (FAU count) must be `0` and `dcd[24..25]` / `dcd[30..31]` set to `0`.

4. **TILER_JOB Primitive Type & NDC Viewport**:
   - Set `vt[8] = (4u << 0) | (1u << 15) | (1u << 16) | (1u << 17)` (`primitive_type = 4` = `TRIANGLES`).
   - Updated vertex position buffer to standard NDC coordinates `[-1.0, 3.0]`.

---

## 2. Hardware Verification Logs

Running `run_replay_egl_triangle.sh triangle` on device:

```text
JOB_SUBMIT (atom 0: TILER) ret=0 errno=0 (Success)
event[0] read=24 code=0x1 atom=1 data0=0x0 data1=0x0
poly list header decode: start=0x94f80 end=0x94f90 diff=16

JOB_SUBMIT (atom 1: Flush) ret=0 errno=0 (Success)
event[0] read=24 code=0x1 atom=1 data0=0x0 data1=0x0

JOB_SUBMIT (atom 2: Fragment) ret=0 errno=0 (Success)
event[0] read=24 code=0x1 atom=2 data0=0x0 data1=0x0
```

---

## 3. Smaller Test Programs Verification

| Test Mode | Atom Result | Color Buffer Output | Status |
|---|---|---|---|
| `scratch_fbd` | `0x1 DONE` | 256 / 256 pixels `0xff0000ff` | PASSED |
| `shader_fbd` | `0x1 DONE` | 256 / 256 pixels `0xff00ff00` (solid green) | PASSED |
| `triangle` (full pipeline) | `0x1 DONE` | All 3 atoms complete clean | PASSED |
