# Mali-G77-MC9 Job Submission Debugging - Current Status

## Progress Summary

### Fixed: Atom Struct Size
- **SOLVED**: Kernel expects exactly **48 bytes** for `base_jd_atom_v2`
- This was determined by compiling the kernel struct definition

### Working Components
1. Device open (`/dev/mali0`) - OK
2. Version handshake (KBASE_IOCTL_VERSION_CHECK 11.13) - OK  
3. Context creation (KBASE_IOCTL_GET_CONTEXT_ID) - OK
4. Memory allocation (KBASE_IOCTL_MEM_ALLOC) - OK
5. Memory mapping (mmap) - OK
6. SET_FLAGS (KBASE_IOCTL_SET_FLAGS) - OK

### Failing: Job Submission
- **KBASE_IOCTL_JOB_SUBMIT** returns EINVAL (22)
- Tried all variations:
  - Different job types (FS, CS, T, combinations)
  - Different priority values (0-255)
  - Different atom numbers (0-5)
  - NULL jc vs valid jc
  - With/without SET_FLAGS
  - Timing delays
  - Memory allocation order

## Root Cause

The MediaTek kernel driver (`mali_kbase_mt6893_r49`) is heavily customized and returns EINVAL on job submission. Possible causes:

1. **GED (GPU Energy Device) Integration** - MediaTek uses a custom GED interface for power management. The job submit queue may remain locked until GED initialization.

2. **Custom Validation** - MTK likely added additional validation beyond the standard ARM kernel checks.

3. **Protected/Memory Bounds** - The GPU may require memory to be within specific bounds or registered with a specific subsystem.

4. **User-space Protocol** - MediaTek may require proprietary libgpud.so to be loaded first to initialize protocol state.

## Technical Details

- Mali-G77 r0p1, 9 cores, Valhall architecture
- GPU ID: 0x09000800  
- Kernel module: `mali_kbase_mt6893_r49`
- Device: `/dev/mali0` (misc device, major 10)
- UAPI Version: 11.13
- GED device: linked in device tree (`soc:ged--platform:13000000.mali`)

## Test Files Created

- `src/kbase/kbase_mem_job_test.c` - Full memory + job test
- `src/kbase/kbase_null_job_test.c` - Minimal NULL job test
- `src/kbase/kbase_job_types_test.c` - Test different job types
- `src/kbase/kbase_prio_test.c` - Test priority values
- `src/kbase/kbase_timing_test.c` - Test timing/race conditions

## Next Steps

1. **Analyze libgpud.so deeper** - Find exact atom construction and any initialization calls
2. **Try to run libgpud.so** - Load it and see if it initializes the driver differently
3. **Look for GED-specific ioctls** - MediaTek may have added custom calls
4. **Check /sys interface** - There may be additional setup via sysfs

## Key Files

- `refs/mali-kernel/` - ARM Mali kernel source (reference only)
- `libgpud.so` - MediaTek's proprietary driver (330KB, heavily stripped)
- `libGLES_mali.so` - Shader compiler (54MB)
