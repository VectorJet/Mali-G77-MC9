# APU Kernel-Side Acceptance Breakthrough

**Date:** 2026-05-11
**Component:** MediaTek APU 3.0 / `/dev/apusys`
**Status:** **PROTOCOL VERIFIED — KERNEL ACCEPTS USER-SUBMITTED JOBS**

---

## The Move From EINVAL to Hardware Errors

We previously stalled at `EINVAL` from `IOCTL 0x22` (152-byte submission). By switching strategy and bridging to the proprietary `libapu_mdw.so` C ABI from a `/data/local/tmp` binary (avoiding the Termux linker namespace block), we obtained a path through the kernel that the driver fully accepts.

`apusysCmd_build` returns **0** and the kernel proceeds to dispatch the command to a real APU engine. The error returned by `cmd_run` is now hardware/engine specific — meaning the entire ioctl protocol layer is conquered.

## Symbol Bridge Used (libapu_mdw.so)

| Symbol                            | Role                              |
|-----------------------------------|-----------------------------------|
| `apusysSession_createInstance`    | Open session (calls 10× IOCTL 0x20 handshake) |
| `apusysSession_createCmd`         | Allocate command container        |
| `apusysCmd_createSubcmd`          | Bind to engine (device_type enum) |
| `apusysSession_cmdBufAlloc`       | Allocate DMA-buf (IOCTL 0x21, 48B) |
| `apusysSubCmd_addCmdBuf`          | Attach cmdbuf to subcommand       |
| `apusysCmd_build` / `_run` / `_wait` | Submit (IOCTL 0x22, 152B) and wait |

## Device Type Enumeration (Empirically Confirmed)

Probed `apusys_device_type` 0..15:

| dev | Driver name (from dmesg) | Behavior on zero-filled cmdbuf |
|-----|--------------------------|--------------------------------|
| 0   | invalid                  | createSubcmd → NULL            |
| 1   | sample (test engine)     | `mdw_sample_exec command size invalid(N)` for any N — synthetic device |
| 2   | mdla                     | `ret(-19)` ENODEV, ~1ms — engine reaches MDLA driver, rejects header |
| 3   | vpu                      | `ret(-22)` EINVAL, ~7ms — VPU rejects descriptor format |
| 4   | edma                     | `ret(-22)` EINVAL, ~6ms — EDMA rejects descriptor format |
| 5–15 | unimplemented           | createSubcmd → NULL            |

Key dmesg trace fields decoded:
```
sched(softlimit/0/hardlimit/0/0/0) mem(...) boost(100) bw(0x0) time(0/N) ret(N)
```

These map directly into the 152-byte submission packet at offsets 32 (softlimit), 36 (hardlimit), and the boost/bw fields previously identified.

## Critical Finding: cmdbuf Is Pure Pass-Through

We pre-filled the cmdbuf with `0xCD` repeating, ran the full submission, and dumped the buffer after `cmd_build` and `cmd_run`:

```
[*] cmd buffer contents after build (first 256 bytes):
  0000 cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd
  …  (unchanged, 256/256 bytes)
[*] cmd buffer contents after run (first 256 bytes):
  0000 cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd
```

**Neither `libapu_mdw` nor the kernel writes a header into the cmdbuf.** The user is fully responsible for the engine-specific descriptor at byte 0.

## Implication

The reverse-engineering effort is now bisected:

1.  **DONE — IOCTL Layer:** Handshake (0x20), DMA-BUF alloc with linkage (0x21), 152-byte job submit (0x22). Kernel accepts our packets unchanged.
2.  **REMAINING — Engine Opcode Layer:** Producing a valid first-DWORD header for MDLA (`-19` says "no device" until we describe an existing core), and a valid descriptor for VPU/EDMA.

## Next Phase

- Disassemble `apusysSubCmd_addCmdBuf` and `mdw_subcmd_info::setupInfo` to identify the **`mdw_subcmd_info`** struct fields the kernel reads from offset 24 of the 152-byte packet (already partially mapped from `setupInfo` at `libapu_mdw.so:0x1c470`).
- Run the proprietary `apusys_test` daemons (kernel threads `apusys_sample0..1`) by writing to `/sys/kernel/debug/apusys/*` (if exposed) to capture a real MDLA cmdbuf on the wire.
- Build a GOT/PLT hook (since LD_PRELOAD is namespace-blocked from intercepting libapu_mdw's calls) to dump the populated 152-byte packet.
