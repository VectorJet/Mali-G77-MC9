# Mali-G77-MC9 Reverse Engineering

## Project Overview

This is a reverse engineering project for the ARM Mali-G77 MC9 GPU. It involves understanding the Mali kernel driver (kbase), GPU job submission, and hardware interaction via ioctls.

## Key Directories

- `src/kbase/debug/` - Debug utilities and test programs
- `src/kbase/replay/` - Workload capture and replay tools
- `src/kbase/tests/` - Test cases for GPU operations
- `scripts/` - Shell scripts for running tests and debugging
- `findings/` - Markdown documentation of reverse engineering discoveries

## Device Access

### SSH into Termux (primary method)
```bash
ssh -p 8022 u0_a659@localhost
```

### Run commands as root
```bash
su -c 'command'
```

### Key paths on device
- Working directory: `/data/data/com.termux/files/home/`
- GPU device: `/dev/mali0`
- Kernel module: `mali_kbase`

## Common Workflows

### Push, compile, and run a test
```bash
scp -P 8022 src/kbase/test.c u0_a659@localhost:/data/data/com.termux/files/home/ && \
ssh -p 8022 u0_a659@localhost "cd /data/data/com.termux/files/home && gcc -o test test.c && su -c ./test"
```

### Check kernel messages
```bash
ssh -p 8022 u0_a659@localhost "su -c 'dmesg | tail -50'"
```

### Strace GPU operations
```bash
ssh -p 8022 u0_a659@localhost 'su -c "/data/data/com.termux/files/usr/bin/strace -f -e trace=ioctl -p PID 2>&1" | grep mali0'
```

## Skills

The following skills are available for this project:

- **termux**: Device interaction via SSH/Termux
- **adb**: Device interaction via ADB
- **skill-creator**: Creating and improving skills

## Important Notes

- Always run test programs with `su -c` for root privileges
- Use the systematic-debugging skill when investigating bugs or unexpected behavior
- Document findings in the `findings/` directory with descriptive names