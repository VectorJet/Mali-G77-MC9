# Mali-G77-MC9 (Valhall) Correct Job Types

**Date:** 2026-04-15  
**Device:** Mali-G77-MC9 (MediaTek MT6893)  
**Architecture:** Valhall v9  

---

## The "Fake Success" Legacy
In earlier findings, we believed we had successfully executed `VERTEX` (3), `COMPUTE` (3), `TILER` (4), and `FRAGMENT` (5) jobs. 

As discovered during the `SAME_VA` memory mapping breakthrough, **none of those jobs ever actually executed on the GPU**. A combination of a misaligned C structure (`jit_id` sizing) and pointer aliasing caused the kernel to silently convert them to dependency-only Soft Jobs, and our C code accidentally wrote the "success" values using the CPU.

Because the jobs never reached the hardware Job Manager, the GPU never faulted on the invalid Job Type values. 

## The True Valhall (v9) Job Types
By consulting the open-source Panfrost driver (`src/panfrost/genxml/v9.xml`), we have identified the **actual** hardware Job Types for the Mali Valhall (G77) architecture. 

The Job Type is encoded in bits `[7:1]` of the Control Word (offset `+0x10`) of a 128-byte Job Descriptor.

| Valhall Name | Value | Used For | Notes |
|---|---|---|---|
| `Not started` | 0 | - | Invalid |
| `Null` | 1 | Dependency Sync | Empty payload |
| `Write value` | **2** | Initialization | Write 32/64-bit value to memory |
| `Cache flush` | **3** | Synchronization | Replaces old Vertex job type (3) |
| `Compute` | **4** | Compute Shaders | Replaces old Tiler job type (4) |
| `Tiler` | **7** | Binning/Tiling | The real Valhall Tiler job |
| `Fragment` | **9** | Pixel Shading | The real Valhall Fragment job |
| `Indexed vertex` | **10** | Vertex Shading | Index buffer driven |
| `Malloc vertex` | **11** | Vertex Shading | Valhall specific vertex allocation |

## The Impact on "THE TRIANGLE"
If we had attempted to submit a Tiler job with type `4` or a Fragment job with type `5` now that the kernel is actually submitting our descriptors to the GPU hardware, the Job Manager would have encountered undefined instructions or mismatched payloads and crashed the GPU bus.

To successfully render a triangle, our pipeline descriptors must use:
1. **Type 10/11 (Vertex) or 4 (Compute)** for the vertex processing stage.
2. **Type 7 (Tiler)** for the primitive binning stage.
3. **Type 9 (Fragment)** for the pixel shading and color buffer write stage.

## `core_req` Mapping
When building the `kbase_atom_mtk` struct, the `core_req` must map to the corresponding hardware execution units:
- **Compute/Vertex/Tiler (Types 4, 7, 10, 11)**: `0x4a` (`BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP`) submitted to `jobslot = 1`.
- **Fragment (Type 9)**: `0x49` (`BASE_JD_REQ_FS | BASE_JD_REQ_CF | BASE_JD_REQ_COHERENT_GROUP`) submitted to `jobslot = 0`.
