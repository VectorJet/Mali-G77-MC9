# libgpud.so Property Surface - 2026-04-15

## Scope

This note captures the property names statically visible in
[refs/libgpud.so](/home/tammy/dev/experiments/Mali-G77-MC9/refs/libgpud.so),
plus a small runtime check against a wrapped Chrome process.

## Why this matters

`gpudInitialize` is heavily driven by `property_get`. If vendor-side logging or
job/memory tracing can be enabled with properties, that is much cheaper than
fully reversing every control path first.

## High-value discovered properties

### Job / command visibility

- `vendor.debug.gpud.run.job.dump`
- `vendor.debug.gpud.command.type.log`
- `vendor.debug.gpud.log`
- `vendor.debug.gpud.systrace`

These are the most obvious candidates for surfacing the real submit path.

### Vulkan memory visibility

- `vendor.debug.gpud.vk.allocate.memory.dump`
- `vendor.debug.gpud.vk.allocate.memory.dump.size`
- `vendor.debug.gpud.vk.allocate.memory.clear`
- `vendor.debug.gpud.vk.allocate.memory.clear.mode`
- `vendor.debug.gpud.vk.allocate.memory.clear.size`
- `vendor.debug.gpud.vk.map.memory.dump`
- `vendor.debug.gpud.vk.map.memory.dump.size`

These are especially relevant because the unresolved working `jc` GPU VAs do not
line up with the simple low-range `MEM_ALLOC` results already observed via the
spy.

### Import / buffer / framebuffer related

- `vendor.debug.gpud.extimage.dump`
- `vendor.debug.gpud.extimage.dump.params`
- `vendor.debug.gpud.vk.fb.extimage.dump`
- `vendor.debug.gpud.vk.fb.extimage.dump.params`
- `vendor.debug.gpud.wsframebuffer.dump`
- `vendor.debug.gpud.wsframebuffer.dump.params`
- `vendor.debug.gpud.wsframebuffer.log`

These are plausible leads if descriptor-bearing memory is tied to imported image
or buffer paths.

### General GPUD configuration

- `vendor.debug.gpud.init`
- `persist.vendor.debug.gpud.init`
- `vendor.debug.gpud.process.name`
- `vendor.debug.gpud.folder`

These suggest GPUD may have per-process gating and configurable dump output
locations.

## Static evidence

Relevant embedded strings include:

```text
vendor.debug.gpud.run.job.dump
vendor.debug.gpud.command.type.log
vendor.debug.gpud.vk.allocate.memory.dump
vendor.debug.gpud.vk.map.memory.dump
persist.vendor.debug.gpud.init
vendor.debug.gpud.init
vendor.debug.gpud.process.name
/data/local/tmp/gpud_dump/
@%s: ### detect run job: PID=%d TID=%d Counter=%u Job=%s Protected=%d
```

This confirms the blob contains both:
- property-driven gating
- built-in text for explicit "run job" reporting

## Runtime experiment

Tested on 2026-04-15 with a wrapped Chrome process:

```text
setprop vendor.debug.gpud.run.job.dump 1
setprop vendor.debug.gpud.command.type.log 1
setprop vendor.debug.gpud.log 1
setprop wrap.com.android.chrome "LD_PRELOAD=/data/local/tmp/s.so"
am force-stop com.android.chrome
monkey -p com.android.chrome -c android.intent.category.LAUNCHER 1
am start -a android.intent.action.VIEW -d https://example.com com.android.chrome
```

Observed result:
- our own `mali_ioctl_spy` logs still appeared as expected
- no additional obvious vendor GPUD log lines appeared in `logcat`

## Interpretation

The lack of immediate extra logs does **not** mean the properties are unused.
More likely possibilities:

- the relevant flags are read only during a different initialization path
- the wrapped Chrome process is not the primary GPUD process that emits those logs
- additional companion properties are required, such as `vendor.debug.gpud.init`
  or `vendor.debug.gpud.process.name`
- output is routed to `/data/local/tmp/gpud_dump/` or another file sink rather
  than plain logcat

## Practical conclusion

The property surface is real and worth preserving as a target set, but the
simple `setprop + relaunch Chrome` experiment was insufficient to unlock more
vendor-side visibility.

## Next steps

1. Correlate the exact property strings used inside `gpudInitialize` with the
   process-name logic in that function.
2. Check whether GPUD writes to `/data/local/tmp/gpud_dump/` when the relevant
   properties are set.
3. Try enabling:
   - `vendor.debug.gpud.init`
   - `persist.vendor.debug.gpud.init`
   - `vendor.debug.gpud.process.name`
4. Continue static analysis around `gpudVkAllocateMemory` and imported-buffer
   code paths, since those remain the strongest explanation for the unresolved
   high/tagged `jc` ranges.

---

## Runtime Tests - 2026-04-15 (continued)

### Properties tested

```
vendor.debug.gpud.init=1
persist.vendor.debug.gpud.init=1
vendor.debug.gpud.run.job.dump=1
vendor.debug.gpud.command.type.log=1
vendor.debug.gpud.log=1
vendor.debug.gpud.folder=/data/local/tmp/gpud_dump
vendor.debug.gpud.process.name=com.android.chrome (also tried "", "*")
```

### Observed result

- Properties confirmed set via `getprop`
- No additional logcat output beyond mali_ioctl_spy
- `/data/local/tmp/gpud_dump/` remains empty
- Tried sdcard path `/sdcard/Download/gpud_dump/` - also empty

### Interpretation

GPUD debug output is either:
- Filtered by a different mechanism not exposed via these properties
- Conditioned on additional runtime checks not satisfied
- Logged at a different priority or tag not captured
- Conditioned on SELinux or other security context

### Alternative approach needed

Since property toggles are insufficient, focus should shift to:
1. Static analysis of gpudInitialize to extract exact property read order
2. Binary analysis of process-name filtering logic
3. AHardwareBuffer import code paths to explain the "jc" GPU VA ranges

---

## Static Analysis - 2026-04-15 (continued)

### Task 1: gpudInitialize Property Reads

The binary is heavily stripped (no symbol table), but key findings:

1. **Property functions used**: `property_get` and `property_get_bool` are imported and called extensively
2. **Init function exists**: `gpudInitialize` is confirmed by string `@%s: Successfully initialized with %s (debug_mode=%d)`
3. **Process gating**: `vendor.debug.gpud.process.name` controls per-process filtering
4. **Global context**: `g_gpud_context` is the central state structure (referenced at offset 0x51000)
5. **Debug output path**: `gpudDetectRunJob` logs job execution with PID/TID/Counter/Job/Protected info

The binary is too stripped for easy disassembly of property read order. The debug mode likely requires additional runtime conditions not satisfied by simple property toggles.

### Task 2: AHardwareBuffer Import Path Analysis

**Found strong evidence of AHardwareBuffer handling:**

**Key function strings**:
- `gpudVkDumpAllocateMemory` - memory allocation dumping
- `gpudIsVkAllocateMemoryDumpOrHackEnabled` - property-based enable
- `gpudDetectRunJob` - job detection and logging

**AHB import evidence**:
```
@%s:     dedicated_info=%p import_fd_info=%p import_ahb_info=%p(ahb=%p) export_info=%p
@%s: [import_ahb_info] AHardwareBuffer is NULL
@%s: [import_ahb_info] dedicated_info is NULL
```

**Gralloc integration**:
- Uses `android.hardware.graphics.allocator-V2-ndk.so`
- References `libgralloc_extra.so`, `libgralloctypes.so`
- FBC (Frame Buffer Compression) control via properties

**This strongly supports the hypothesis**: The "jc" (job command) GPU VA ranges come from AHardwareBuffer-imported memory - allocated in kernel's DMAbuf/ion pool and mapped directly, explaining why they're not in tracked mmaps.

### Task 3: import_ahb_info Code Path

Direct AHB import string evidence:
```
@%s:     dedicated_info=%p import_fd_info=%p import_ahb_info=%p(ahb=%p) export_info=%p
@%s: [import_ahb_info] AHardwareBuffer is NULL
@%s: [import_ahb_info] dedicated_info is NULL
```

The import path processes:
1. `VkImportMemoryFdInfoKHR` - fd-based import
2. `VkImportMemoryWin32HandleInfoKHR` - win32 handle import  
3. `import_ahb_info` - AHardwareBuffer import (most likely source of high "jc" VAs)

The "jc" GPU VAs at high addresses (e.g., `0xb400007...`, `0x7167c8f008`) are consistent with:
- DMAbuf/ion heap allocations
- AHB-backed memory imported from gralloc
- Protected/secure memory pools

### Conclusion

The "jc" GPU VA mystery is likely solved: these are AHardwareBuffer-imported memory ranges from gralloc, not standard mmapped memory. The spy only tracks mmaps, so these allocations appear as "not in tracked ranges" - but they're valid GPU addresses assigned by the kernel's ion allocator.

---

### Additional Findings: Memory Heaps and Types

**Vulkan memory properties handling**:
- `gpudVkGetPhysicalDeviceMemoryProperties` / `gpudVkGetPhysicalDeviceMemoryProperties2`
- Memory heap and type enumeration logging:
  ```
  @%s:     [memoryHeap#%d] size=%lu flags=0x%x
  @%s:     [memoryType#%d] heapIndex=%u propertyFlags=0x%x
  @%s: physicalDevice=%p memoryTypeCount=%d memoryHeapCount=%d
  ```

**Heap uncaching control**:
- `gpudGrallocHeapUncache` function
- Property `ro.vendor.debug.gpud.gralloc.heap.uncache`
- Log: `@%s: ### The gralloc heap is uncached (ro_prop=%d gpud_prop=%d)`

This confirms GPUD has visibility into which memory heap allocations come from, which could explain different VA ranges for different heap types.

---

### Runtime Test: vk.allocate.memory.dump

**Tested property**: `vendor.debug.gpud.vk.allocate.memory.dump=1`

**Additional properties set**:
```
vendor.debug.gpud.init=1
vendor.debug.gpud.log=1
vendor.debug.gpud.process.name=*
vendor.debug.gpud.run.job.dump=1
vendor.debug.gpud.vk.allocate.memory.dump=1
vendor.debug.gpud.folder=/sdcard/Download/gpud_dump
```

**Verification**: Confirmed libgpud.so is loaded in Chrome (PID 11997):
```
6de3b47000-6de3b5f000 r--p 00000000 fe:09 2877 /vendor/lib64/libgpud.so
6de3b67000-6de3b87000 r-xp 00020000 fe:09 2877 /vendor/lib64/libgpud.so
```

**Result**: No GPUD logcat output, no dump files created.

**Interpretation**: The debug properties appear to be read at init time only, or require additional gating not satisfied. The library is loaded but debug features remain dormant.
