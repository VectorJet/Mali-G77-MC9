---
name: termux
description: Device interaction via SSH/Termux for Mali-G77-MC9 reverse engineering. Provides workflows for pushing code, compiling on device, running tests, and debugging.
---

# Termux Skill

## Purpose
Device interaction via SSH/Termux for the Mali-G77-MC9 reverse engineering project.

## Connection

### SSH into Termux (primary method)
```bash
ssh -p 8022 u0_a371@localhost
```

### Run as root
```bash
su -c 'command'
```

## Workflow

### 1. Push code to device
```bash
scp -P 8022 src/kbase/test.c u0_a371@localhost:/data/data/com.termux/files/home/
```

### 2. Compile on device
```bash
ssh -p 8022 u0_a371@localhost "cd /data/data/com.termux/files/home && gcc -o test test.c"
```

### 3. Run on device (as root)
```bash
ssh -p 8022 u0_a371@localhost "su -c /data/data/com.termux/files/home/test"
```

### 4. Combined push, compile, run
```bash
scp -P 8022 src/kbase/test.c u0_a371@localhost:/data/data/com.termux/files/home/ && \
ssh -p 8022 u0_a371@localhost "cd /data/data/com.termux/files/home && gcc -o test test.c && su -c ./test"
```

## Working Directory
```
/data/data/com.termux/files/home/
```

## Debug Commands

### Check kernel messages
```bash
ssh -p 8022 u0_a371@localhost "su -c 'dmesg | tail -50'"
```

### List files
```bash
ssh -p 8022 u0_a371@localhost "ls -la /data/data/com.termux/files/home/"
```

### Check GPU state
```bash
ssh -p 8022 u0_a371@localhost "su -c 'cat /sys/class/misc/mali0/device/gpuinfo'"
ssh -p 8022 u0_a371@localhost "su -c 'cat /sys/kernel/ged/hal/gpu_power_state'"
```

### Check processes using /dev/mali0
```bash
ssh -p 8022 u0_a371@localhost "su -c 'ls -la /proc/*/fd/* 2>/dev/null | grep mali0'"
```

### Strace a process using mali0
```bash
ssh -p 8022 u0_a371@localhost 'su -c "/data/data/com.termux/files/usr/bin/strace -f -e trace=ioctl -p PID 2>&1" | grep mali0'
```

## Key Notes
- Use SSH with port 8022
- Always run test programs with `su -c` for root privileges
- Working directory on device is `/data/data/com.termux/files/home/`
- GCC is available in Termux for on-device compilation
- strace binary is at `/data/data/com.termux/files/usr/bin/strace`