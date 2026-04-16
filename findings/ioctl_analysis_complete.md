# Mali-G77-MC9 Job Submit - Ioctl Analysis Complete

## Final Ioctl Map

| NR | NAME | STATUS | NOTES |
|----|------|--------|-------|
| 0x00 | VERSION_CHECK | OK | Required first - without it everything is EPERM |
| 0x01 | SET_FLAGS | OK | Required second |
| 0x02 | JOB_SUBMIT | **EINVAL** | Our target - validation gate active |
| 0x03 | (unknown) | OK | Works after init |
| 0x05 | MEM_ALLOC | OK | Standard |
| 0x07 | MEM_FREE | OK | Standard |
| **0x13** | **MTK_1** | **OK** | No args (_IO), not in ARM spec |
| **0x14** | **MTK_2** | **OK** | Size 24, not in ARM spec |
| **0x24** | **MTK_3** | **OK** | Size 32, not in ARM spec |

## What Works
- Device open + VERSION_CHECK + SET_FLAGS + MEM_ALLOC + MEM_FREE all work
- 0x13 returns success (takes no arguments)
- 0x14/0x24 return success with correct sizes

## What's Blocked
- JOB_SUBMIT always returns EINVAL after proper init sequence
- Even calling the new MTK ioctls doesn't unlock it

## Probable Cause
The validation gate in JOB_SUBMIT is checking for something beyond just ioctl sequence:
1. **GPU power state** - May need GED to power on GPU first
2. **Memory validation** - The JC pointer may need additional registration
3. **Context state** - May need more complex setup via libgpud.so

## Next Steps for Continuing Agent

### 1. Try Loading libgpud.so
The `gpudInitialize` function in libgpud.so likely does the real init:
```c
void *h = dlopen("libgpud.so", RTLD_NOW);
int (*init)() = dlsym(h, "gpudInitialize");
init();  // Then try JOB_SUBMIT
```

### 2. Try GPU Power Via Sysfs
MediaTek may require GPU to be powered on first:
```bash
echo on > /sys/class/misc/mali0/device/power/control
cat /sys/class/misc/mali0/device/gpu_load  # Check if accessible
```

### 3. Check Kernel Logs
With root:
```bash
dmesg | grep -i mali
# Or enable debug
echo 8 > /proc/sys/kernel/printk
```

### 4. LD_PRELOAD Spy
Create ioctl spy to see what libgpud.so actually calls at init time

## Test Files Created
- kbase_ioctl_fuzz2.c - Full ioctl scan
- mtk_ioctl_probe.c - Probe unknown ioctls  
- mtk_sequence2.c - Test sequences with MTK ioctls
- kbase_comprehensive_test.c - Full init sequence

## Key Files
- /dev/mali0 - Mali misc device
- libgpud.so - Contains gpudInitialize (key init function)
- /sys/kernel/ged/ - GED subsystem (no /dev/ged)
