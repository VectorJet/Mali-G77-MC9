# MDLA Manual Replay — Kernel Now Parsing Our Cmd

**Date:** 2026-05-16
**Status:** **MDLA DRIVER READING OUR INFO BUFFER — final structural piece remaining**

---

## What Changed Since Last Iteration

[src/kbase/tools/mdla_replay.c](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/kbase/tools/mdla_replay.c) now correctly:

- Allocates a 72-byte info cmd buf with flag `0x100` (matches `mdlaCmd::prepareData`)
- Initializes it with `info[0x08] = 0x1c0` (cmd_size) and `info[0x14] = 1` (count)
- Allocates the 448-byte cmd stream with flag `0x100`
- Attaches **both** buffers: `addCmdBuf(sub, cb, 0)` then `addCmdBuf(sub, info, 1)`

Result — `apusysCmd_build` still rc=0, but kernel side now logs:

```
mdla_cmd_get_codebuf_addr: 164 count/offset check fail
```

This is the **MDLA driver itself**, parsing our 72-byte info buf and the cmd stream, then rejecting one of the codebuf addresses inside the cmd. We have advanced from "ENODEV" (engine doesn't see us) all the way to "engine sees us, parses us, validates our cmd, finds one specific field wrong".

## Decoding `mdla_cmd_get_codebuf_addr`

The kernel function looks up code buffer addresses (DVAs) at specific offsets in the 448-byte stream. The literal `164` (= `0xa4`) is the offset/index that failed validation. In the captured stream, byte 0xa4 holds `0x22222222` — a sentinel filler, not a buffer reference. The driver presumably expects an extra patched DVA there based on either:

1. A second pass of address rewrites we haven't done (the encoded `0x22222222` is one of the section-table markers similar to the four DVAs we did patch).
2. A per-cmd "code buffer count" field in the info struct that says how many DVAs to look up; ours says 1, but the engine wants more.

## Confirmed Working API Calls (No `libmdla_ut`)

```c
sess  = apusysSession_createInstance();
act   = apusysSession_memAlloc(sess, 528192, 0x1000, 0, 2);
wgt   = apusysSession_memAlloc(sess, 1536,   0x1000, 0, 2);
qnt   = apusysSession_memAlloc(sess, 128,    0x1000, 0, 2);
out   = apusysSession_memAlloc(sess, 31744,  0x1000, 0, 2);
dva   = (uint32_t)apusysSession_memGetInfoFromHostPtr(sess, host_ptr, 1);
info_cb = apusysSession_cmdBufAlloc(sess, 72,  0x100);  /* flag MUST be 0x100 */
cb    = apusysSession_cmdBufAlloc(sess, 448, 0x100);
cmd   = apusysSession_createCmd(sess);
sub   = apusysCmd_createSubcmd(cmd, 2);              /* 2 = MDLA */
apusysSubCmd_addCmdBuf(sub, cb,      0);             /* mode 0 = cmd stream */
apusysSubCmd_addCmdBuf(sub, info_cb, 1);             /* mode 1 = info/driver */
apusysCmd_build(cmd);  /* rc = 0 */
apusysCmd_run(cmd);    /* rc = -5; kernel sees and parses */
```

## Next Step

Re-run `mdla_capture` and dump the full 72-byte info buffer **and** examine the relationship between `info[0x14]` (count) and the number of buffer-reference offsets the kernel inspects in the cmd stream. The encoded `0x22222222` at offset `0xa4` of the cmd stream is almost certainly a second-pass relocation marker that `mdla_cmd_get_codebuf_addr` is failing on. With that one decode resolved, the manual replay completes.
