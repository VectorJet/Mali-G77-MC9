# Manual MDLA Replay — Submitting From Pure libapu_mdw

**Date:** 2026-05-16
**Status:** **PARTIAL — kernel accepts submission, MDLA driver returns ENODEV**

---

## What Works (No libmdla_ut Required)

Our [src/kbase/tools/mdla_replay.c](file:///home/tammy/dev/experiments/Mali-G77-MC9/src/kbase/tools/mdla_replay.c) successfully:

1. `dlopen("/vendor/lib64/libapu_mdw.so")` — direct C API only
2. `apusysSession_createInstance` → session
3. `apusysSession_memAlloc(sess, size, 0x1000, 0, 2)` × 4 → data buffers (528 KB, 1.5 KB, 128 B, 31 KB)
4. Loads `Activation_1.bin`, `Weight_1.bin`, `QuantTableAdd_1.bin` from `/data/local/tmp/mdla_data/`
5. `apusysSession_memGetInfoFromHostPtr(sess, host_ptr, 1)` → DVAs (low 32 bits of returned uint64)
6. `apusysSession_cmdBufAlloc(sess, 448)` → cmd stream buffer
7. Loads `conv2d_depthwise_Command.bin` and **patches DVAs** at offsets `0x000, 0x004, 0x008, 0x054`
8. `apusysSession_createCmd / apusysCmd_createSubcmd(cmd, 2 /*MDLA*/) / apusysSubCmd_addCmdBuf`
9. `apusysCmd_build` → **rc = 0** (kernel accepted the 152-byte job packet)

## Discovered libapu_mdw C-API Signatures

The published C wrappers in `libapu_mdw.so` are minimal thunks that leave extra arg registers untouched. Calling them from C requires matching the underlying C++ method's register layout:

```c
/* sess in x0, size in w1, align in w2, pad in w3 (=0), type in w4 (=2) */
void *apusysSession_memAlloc(void *sess, uint32_t size,
                             uint32_t align, uint32_t pad, uint32_t type);

/* sess in x0, host_ptr in x1, type in w2 — returns DVA in x0 (low 32 bits) */
uint64_t apusysSession_memGetInfoFromHostPtr(void *sess, void *host_ptr, uint32_t type);
```

## Still Missing — kernel reports `-19 (ENODEV)` from MDLA

```
misc apusys: [error] mdw_sched_trace fail :
  pid(...) cmd(...) dev(2/mdla-#0) pack(0) sched(20/0/30000/0/0/0)
  mem(0/0/0x0/0x0) boost(100) bw(0x0) time(0/3) ret(-19)
```

The MDLA engine itself rejects the cmd in ~3 ms. Three likely missing pieces (visible in `libmdla_ut` symbols but not yet in our replay):

1. **72-byte "SubCmd Info" buffer** — captured during instrumentation but not yet added to our submission. `cmdBufAlloc(sess, 72)` then `addCmdBuf(subcmd, info_buf, mode)` with mode setting it as the **info/driver** struct rather than the cmd stream.
2. **`apusysSession_memFlush(sess, host_ptr)`** — MDLA reads from DMA so cache lines holding our data must be flushed. `mdlaCmd::syncPoolBuf` and `syncCmdBuf` in libmdla_ut do exactly this.
3. **`apusysSubCmd_setParam`** — sets per-subcmd params (boost=100, softlimit, hardlimit) that show up in the `sched(...)` dmesg trace. Without them the MDLA scheduler may reject the engine match.

## Implementation Path

```c
/* After loading data buffers, add cache flushes */
apusysSession_memFlush(sess, act);
apusysSession_memFlush(sess, wgt);
apusysSession_memFlush(sess, qnt);

/* Allocate and attach the 72-byte info buffer */
void *info = apusysSession_cmdBufAlloc(sess, 72);
memset(info, 0, 72);
((uint32_t *)info)[2] = 0x1c0;  /* cmd_size */
((uint32_t *)info)[5] = 1;      /* type = MDLA v1.7 */
apusysSubCmd_addCmdBuf(sub, info, 1 /*info mode*/);
apusysSubCmd_addCmdBuf(sub, cb,   0 /*cmd  mode*/);

/* Set per-subcmd boost so the scheduler accepts the engine */
apusysSubCmd_setParam(sub, APUSYS_SUBCMD_PARAM_BOOST,  100);
apusysSubCmd_setParam(sub, APUSYS_SUBCMD_PARAM_PRIO,   0);

apusysCmd_setParam(cmd, APUSYS_CMD_PARAM_SOFTLIMIT_MS, 20);
apusysCmd_setParam(cmd, APUSYS_CMD_PARAM_HARDLIMIT_MS, 30000);
```

Next iteration will wire these in and re-check against `Golden_1.bin`.
