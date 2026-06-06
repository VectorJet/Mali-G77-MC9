# MDLA Status-Zero — End-to-End Hardware Execution

**Date:** 2026-05-11
**Component:** MediaTek APU 3.0 / MDLA 1.7
**Status:** **HARDWARE COMPUTE EXECUTION CONFIRMED**

---

## Headline

The MDLA 1.7 NPU executed a real `conv2d_depthwise` workload submitted from a tool we wrote. Kernel dmesg confirmed `apu_device_power_on`, `mdla_cmd_prepare_v1_x_sched` and the userspace return path produced `Execute success!`.

```
=== MDLA Built-In Pattern Runner ===
[OK] libmdla_ut.so loaded
BOARD_PLATFORM_ID: mt6893
[OK] mdla_platform_init rc=1
 Set boost value: 100, turbo: 0
 MDLA success run
Execute success!
[OK] execute_pattern returned 0
```

Kernel side:
```
[APUMDLA0:power][apu_device_power_on] call by APUMDLA0 end
mdla_cmd_prepare_v1_x_sched: kva=0xffffffc0ac455100 mva=0xffc00100 cnt=1 sz=0x1c0
```

## Bridging Strategy That Unlocked This

1. The Termux default linker namespace blocks vendor libraries (`is not accessible for the namespace "(default)"`).
2. Copying the executable into `/data/local/tmp/` and running it via `su -c` yields a process with **vendor namespace access** — `dlopen("/vendor/lib64/libapu_mdw.so")` and `libmdla_ut.so` then succeed.
3. `libmdla_ut.so` exports `execute_pattern(int argc, char **argv)` as a C entry point, which is the proprietary CLI's `main()`. Wrapping it with `argv = {"runner","-f","Test"}` triggers the entire MDLA test suite.

## Reverse-Engineered MDLA Command Format

Embedded inside `libmdla_ut.so` (.data section):

| Symbol                                  | Size      | Role                              |
|-----------------------------------------|-----------|-----------------------------------|
| `conv2d_depthwise_Command`              | **0x1c0 (448 B)** | **MDLA 1.7 instruction stream** |
| `conv2d_depthwise_Weight_1`             | 0x600     | Convolution weights               |
| `conv2d_depthwise_QuantTableAdd_1`      | 0x80      | Quant add table                   |
| `conv2d_depthwise_Activation_1`         | 0x80f40   | Input activation                  |
| `conv2d_depthwise_Golden_1`             | 0x7c00    | Expected output                   |
| `conv2d_depthwise_Golden_Mask_1`        | 0x7c00    | Verification mask                 |
| `MaxPower_1_Golden_1` / `_Mask_1`       | 0x200000  | Max-power stress pattern          |

Confirmed cmd format constants (from `_Z15GetCmdSize_v1_7v` / `_Z15GetCmdType_v1_7v`):

```
MDLA cmd type = 1
MDLA cmd size = 0x1c0 (448 bytes)
```

Captured into `captured_shaders/mdla/`:
- `conv2d_depthwise_Command.bin` (448 B)
- `conv2d_depthwise_Weight_1.bin` (1536 B)
- `conv2d_depthwise_QuantTableAdd_1.bin` (128 B)

First 16 dwords of the captured MDLA command stream:
```
0000:  41371000 41995000 40eda000 007e0001
0010:  00000083 00000200 00060020 00010007
0020:  001f0001 00200020 00007c00 00000400
0030:  00000020 00080f40 00001060 00000020
```

## Path To 100% Bypass

The remaining proprietary call site is `libmdla_ut.so::execute_pattern` (which itself only calls `libapu_mdw.so` through the `apusys*` C ABI we've already mapped). To remove the last vendor dependency:

1.  Re-implement `MDLAPlayer::ExecutePattern` directly on top of the ioctl protocol we already control (handshake → cmdBufAlloc → setSubCmdParam → submit).
2.  Use the captured `conv2d_depthwise_Command.bin` as the cmdbuf payload. Sizes already align: 0x1c0 matches `GetCmdSize_v1_7`.
3.  Verify success by comparing computed output against `conv2d_depthwise_Golden_1.bin` masked by `Golden_Mask_1.bin`.

After this step, the entire NPU stack is open: handshake, allocation, submission, opcode encoding, and result verification, all in pure userspace C.
