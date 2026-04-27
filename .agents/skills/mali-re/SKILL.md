---
name: mali-re
description: Reverse engineering workflow for ARM Mali-G77 MC9 GPU via the Mali kbase driver. Use this skill whenever working with GPU binaries, kernel driver ioctls, job submission, shader analysis, GPU memory, Chrome job capture/replay, or any Mali GPU RE task. This skill combines static binary analysis (ghidra-cli, radare2, objdump), dynamic device interaction (SSH/Termux), tracing (strace/dmesg), and GPU testing. Make sure to use this skill for ANY GPU, Mali, kbase, or kernel driver reverse engineering work, even if the user doesn't explicitly ask for it.
---

# Mali-G77 GPU Reverse Engineering

## Project Context

This project reverse engineers the ARM Mali-G77 MC9 GPU on a MediaTek Dimensity 700 chip. The GPU communicates via the `mali_kbase` kernel driver through ioctl calls on `/dev/mali0`. Key discoveries are documented in `findings/` and test programs live in `src/kbase/`.

## Device Connection

### SSH (primary)
```bash
ssh -p 8022 u0_a659@localhost
```

### Root access
```bash
su -c 'command'
```

### Key device paths
- GPU device: `/dev/mali0`
- Working dir: `/data/data/com.termux/files/home/`
- Kernel module: `mali_kbase`
- Strace: `/data/data/com.termux/files/usr/bin/strace`

## Push/Compile/Run Workflow

For a test program like `src/kbase/tests/test_foo.c`:

```bash
# One-liner: push, compile, run
scp -P 8022 src/kbase/tests/test_foo.c u0_a659@localhost:/data/data/com.termux/files/home/ && \
ssh -p 8022 u0_a659@localhost "cd /data/data/com.termux/files/home && gcc -o test_foo test_foo.c && su -c ./test_foo"

# Check dmesg after running
ssh -p 8022 u0_a659@localhost "su -c 'dmesg | tail -30'"
```

## Static Binary Analysis

### ghidra-cli (recommended for speed)
```bash
ghidra import /path/to/binary
ghidra analyze /path/to/binary
ghidra query functions --filter "name =~ 'submit'"
ghidra decompile 0x_address
ghidra dump --format json /path/to/binary > output.json
```

### radare2
```bash
r2 /path/to/binary
aaa          # analyze all
afl          # list functions
pdf @sym.main
izz          # strings
/ik job      # search for "job" in instructions
```

### objdump / readelf
```bash
objdump -d binary | head -200
readelf -s binary | grep -i job
readelf -h binary  # file header
```

### pahole (for struct layouts)
```bash
pahole binary  # shows struct definitions
```

## Dynamic Tracing

### Strace GPU ioctls
```bash
ssh -p 8022 u0_a659@localhost 'su -c "/data/data/com.termux/files/usr/bin/strace -f -e trace=ioctl -p PID 2>&1" | grep mali0'
```

### Strace all relevant syscalls
```bash
ssh -p 8022 u0_a659@localhost 'su -c "strace -f -e trace=openat,mmap,ioctl,write -p PID" 2>&1 | head -500'
```

### Kernel dmesg
```bash
ssh -p 8022 u0_a659@localhost "su -c 'dmesg | tail -50'"
ssh -p 8022 u0_a659@localhost "su -c 'dmesg | grep -i mali'"
```

## Mali ioctl Reference

Key ioctls on `/dev/mali0` (all use magic `0x80`):

| NR  | Name         | Size | Notes                          |
|-----|--------------|------|--------------------------------|
| 0x00| VERSION_CHECK| 32   | Must call first                |
| 0x01| SET_FLAGS    | 16   | Second, after VERSION_CHECK    |
| 0x02| JOB_SUBMIT   | var  | TARGET - requires GPU init      |
| 0x05| MEM_ALLOC    | 32   | Standard                       |
| 0x07| MEM_FREE     | 16   | Standard                       |
| 0x13| MTK_1        | 0    | Mediatek custom, no args       |
| 0x14| MTK_2        | 24   | Mediatek custom                |
| 0x24| MTK_3        | 32   | Mediatek custom                |

### ioctl decode formula
`_IOC(dir, magic=0x80, nr, size)` produces: `dir << 30 | 0x80 << 8 | nr | size << 16`

## Job Structure Reference

Job chains are 64-byte structs submitted via `JOB_SUBMIT`. The GPU uses a linked-list of job headers connected via a `NEXT_JOB_PTR` field. Each job header references a "fragment" structure at an offset (typically `+0x58`).

## Chrome Job Capture/Replay

### Capture from Chrome
Use `scripts/run_egl_dumper_with_spy.sh` to capture EGL job submissions from Chrome.

### Replay captured jobs
```bash
# Replay the EGL triangle
bash scripts/run_replay_egl_triangle.sh
```

## Test Program Structure

Test programs follow this pattern:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>

// ioctl numbers (from mali_kernel_ioctl.h or reverse engineered)
#define KBASE_IOCTL_VERSION_CHECK    0x800810
#define KBASE_IOCTL_SET_FLAGS         0x800811
#define KBASE_IOCTL_JOB_SUBMIT        0x800802

int main() {
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    // VERSION_CHECK first
    struct version_check { uint32_t major; uint32_t minor; } vc = {1, 0};
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &vc)) { perror("version_check"); }

    // SET_FLAGS second
    uint64_t flags = 0;
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags)) { perror("set_flags"); }

    // Now try JOB_SUBMIT, MEM_ALLOC, etc.

    close(fd);
    return 0;
}
```

## Findings Documentation

Document discoveries in `findings/` with descriptive names like `ioctl_analysis_complete.md`. Include:
- What works / what's blocked
- Key data structures and offsets
- Next steps

## Common Issues

### EINVAL on JOB_SUBMIT
Likely missing GPU power-on via libgpud.so or memory not properly registered. Try loading `libgpud.so` with dlopen.

### EPERM after VERSION_CHECK
Check if SET_FLAGS was called in the correct order. Both must succeed before other ioctls.

### dmesg shows nothing
Increase log level: `echo 8 > /proc/sys/kernel/printk` as root.

## Tools Quick Reference

| Tool | Purpose | Command |
|------|---------|---------|
| ghidra-cli | Fast binary analysis | `ghidra <subcommand>` |
| radare2 | Binary analysis/shell | `r2 binary` |
| objdump | Disassembly | `objdump -d binary` |
| readelf | ELF structure | `readelf -s binary` |
| pahole | Struct layout | `pahole binary` |
| strace | Syscall tracing | `strace -f -e trace=ioctl -p PID` |
| dmesg | Kernel logs | `su -c dmesg \| tail -50` |
| gcc | Compile on device | `gcc -o out source.c` |
| scp | Push to device | `scp -P 8022 file user@host:path` |