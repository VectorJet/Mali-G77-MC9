# Mali-G77-MC9 Ioctl Fuzzing Results

## Confirmed Working Ioctls (after init)

| NR | NAME | STATUS | NOTES |
|----|------|--------|-------|
| 0x00 | VERSION_CHECK | OK | Must call first |
| 0x01 | SET_FLAGS | OK | |
| 0x02 | JOB_SUBMIT | EINVAL | **Our target - gated** |
| 0x03 | GET_GPUPROPS | OK | Works after init |
| 0x05 | MEM_ALLOC | OK | |
| 0x07 | MEM_FREE | OK | |
| **0x13** | **MTK_NEW_1** | **OK** | Not in ARM spec |
| **0x14** | **MTK_NEW_2** | **OK** | Not in ARM spec |
| **0x24** | **MTK_NEW_3** | **OK** | Not in ARM spec |

## Critical Discovery: gpudInitialize

Found in libgpud.so:
- `gpudInitialize` function exists
- `persist.vendor.debug.gpud.init` property

This suggests libgpud.so does custom initialization that we need.

## What Works
- open() /dev/mali0
- VERSION_CHECK (ioctl 0)
- SET_FLAGS (ioctl 1)  
- MEM_ALLOC (ioctl 5)
- MEM_FREE (ioctl 7)
- The new MTK ioctls (0x13, 0x14, 0x24) return OK

## What's Blocked
- JOB_SUBMIT (ioctl 2) - Returns EINVAL for ALL valid atom structures
- GET_GPUPROPS returns ENOTTY without init (but OK after init)

## Next Steps

1. **Find libgpud.so initialization** - gpudInitialize() does the magic
2. **Try GED sysfs** - Write to /sys/kernel/ged/ entries 
3. **Check kallsyms** - If accessible, may show ioctl handlers

## Test Files

- kbase_ioctl_fuzz2.c - Comprehensive ioctl scan
- kbase_comprehensive_test.c - Full init sequence test
- kbase_compare.c - Compare with/without new ioctls
