# MDLA Full Capture — All Buffers Mapped, Replay-Ready

**Date:** 2026-05-12
**Status:** **EVERY DRIVER ALLOCATION & ADDRESS FIXUP RECORDED**

---

## ARM64 Trampoline Hooks

A 16-byte `LDR X16, [PC,#8] / BR X16 / .quad target` trampoline was installed at the entry of three libapu_mdw symbols, allowing us to intercept every call libmdla_ut makes:

- `apusysSession_cmdBufAlloc`
- `apusysSession_memAlloc`
- `apusysCmd_run`

Implementation: [src/kbase/tools/mdla_capture.c](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/kbase/tools/mdla_capture.c). The hook restores the original prologue, calls through, then reinstalls itself.

## Captured Per-Submission Allocation Set (Test Pattern, conv2d_depthwise)

| # | Source API     | Size (bytes / hex) | Role                 | Matches embedded |
|---|----------------|--------------------|----------------------|------------------|
| 0 | cmdBufAlloc    | 72   / 0x48        | SubCmd info header   | n/a (kernel-managed) |
| 1 | memAlloc       | 528192 / 0x80f40   | **Activation**       | ✅ matches `conv2d_depthwise_Activation_1` |
| 2 | memAlloc       | 1536 / 0x600       | **Weight**           | ✅ matches `conv2d_depthwise_Weight_1` |
| 3 | memAlloc       | 128  / 0x80        | **QuantTableAdd**    | ✅ matches `conv2d_depthwise_QuantTableAdd_1` |
| 4 | memAlloc       | 31744 / 0x7c00     | **Output**           | n/a (pre-filled `0xCD`) |
| 5 | cmdBufAlloc    | 448 / 0x1c0        | **MDLA cmd stream**  | ≠ embedded (DVA fixups applied) |

All saved to [captured_shaders/mdla/](file:///home/tammy/dev/experiments/Mali-G77-MC9/captured_shaders/mdla/).

## Address Fixup Table Decoded

By diffing the embedded `conv2d_depthwise_Command.bin` against the captured `cap_before_buf_5_cmdBuf_448.bin`:

| Cmd offset | Embedded value | Resolved DVA | Points to        |
|-----------|----------------|--------------|------------------|
| `0x000`   | `0x41371000`   | `0xffb00000` | Activation (buf 1) |
| `0x004`   | `0x41995000`   | `0xffafd000` | QuantTableAdd (buf 3) |
| `0x008`   | `0x40eda000`   | `0xffae8000` | Output (buf 4)    |
| `0x054`   | `0x417e9000`   | `0xffafc000` | Weight (buf 2)    |
| `0x154`   | `0x00000000`   | `0x01000000` | Cmd version / count |

Encoding: the top byte (`0x40` / `0x41`) plus second byte encode a section-table lookup; lower bytes encode a per-section offset. The kernel maps each `memAlloc` to a 4-KB-aligned DVA from a top-down arena (~`0xffae8000`–`0xffb00000`). Exact DVAs vary per run, but the layout is consistent.

## SubCmd Info Header Structure (72 bytes)

```
0x00: u64    flags        = 0
0x08: u32    cmd_size     = 0x1c0    ← matches GetCmdSize_v1_7
0x10: u32    boost        = 0
0x14: u32    type         = 1        ← matches GetCmdType_v1_7
... (zeros)
0x18: u32    status       = 1        ← kernel writes after submission
... (zeros)
```

## Manual Replay Plan

With every piece captured, manual replay needs only six allocations and a single fixup table — no libmdla_ut. The next tool will:

1. `apusysSession_createInstance()` (handshake)
2. `apusysSession_queryDeviceNum(MDLA=2)` (confirms hw available)
3. `apusysSession_memAlloc` × 4 with sizes 528192, 1536, 128, 31744 then load `Activation_1.bin`, `Weight_1.bin`, `QuantTableAdd_1.bin`, and zeros respectively
4. `apusysSession_memGetInfoFromHostPtr` on each, type=2, to obtain its DVA
5. `apusysSession_cmdBufAlloc(72)` and `cmdBufAlloc(448)`; load `Command.bin` into the 448-byte buffer, then patch dwords at offsets `{0x000, 0x004, 0x008, 0x054}` with the resolved DVAs
6. `createCmd / createSubcmd(dev=2) / addCmdBuf / cmd_build / cmd_run / cmd_wait`
7. Memcmp output buffer against `Golden_1.bin` masked by `Golden_Mask_1.bin`

This removes the final vendor dependency (`libmdla_ut`) from the MDLA execution path.
