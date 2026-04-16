# Job Chaining Investigation - 2026-04-15

## Summary

Investigation into job chaining on Mali-G77 GPU. Found that GPU doesn't support `next_job_ptr` chaining within a single atom, but multi-atom approach works.

## Tests Performed

| Test | jc offset | Result |
|------|-----------|--------|
| Job at offset 0 | 0x41000 | ✅ Works |
| Job at offset 0x10 | 0x41010 | ❌ Fail |
| Job at offset 0x20 | 0x41020 | ❌ Fail |
| Job at offset 0x30 | 0x41030 | ❌ Fail |
| Job at offset 0x40 | 0x41040 | ❌ Fail |
| Job at offset 0x50 | 0x41050 | ❌ Fail |
| Job at offset 0x60 | 0x41060 | ❌ Fail |
| Job at offset 0x70 | 0x41070 | ❌ Fail |
| Job at offset 0x80 | 0x41080 | ❌ Fail |
| Job at offset 0xC0 | 0x410c0 | ❌ Fail |
| Job at offset 0x100 | 0x41100 | ❌ Fail |
| 256-byte aligned job at 0x200 | 0x41200 | ❌ Fail |
| With JOB_CHAIN flag (0x303) | offset 0 | ❌ Fail |

## Job Chain Tests

| Test | Method | Result |
|------|--------|--------|
| next_job_ptr at offset 0x18 | Job1 → Job2 | ❌ Only first runs |
| next_job_ptr at 0x80 offset | Job1 → Job2 | ❌ Only first runs |
| 256-byte aligned chain | Job1 → Job2 | ❌ Only first runs |
| JOB_CHAIN flag | core_req=0x303 | ❌ Only first runs |

## Multi-Atom Tests

| Test | Method | Result |
|------|--------|--------|
| 2 atoms, same buffer | Both jc=0x41000 | ❌ Only first runs |
| 2 atoms, separate buffers | Different jc | ✅ Both run |
| 2 atoms, different core_req | 0x209, 0x20a | ✅ Both run |
| 3 atoms, separate buffers | Different jc | ✅ All run |

## Conclusion

**Job chaining via next_job_ptr does NOT work on this Mali-G77 implementation.**

The GPU silently ignores:
- Jobs at any offset other than 0
- next_job_ptr chains
- JOB_CHAIN flag

**Working approach**: Multi-atom submission with separate GPU VA allocations per job (like Chrome).

This is consistent with Chrome's observed behavior - each atom has a unique jc pointing to different GPU VA regions.

## For Triangle Rendering

Use multi-atom approach:
1. Allocate separate buffer for vertex job
2. Allocate separate buffer for tiler job
3. Allocate separate buffer for fragment job
4. Submit with atom dependencies (pre_dep) or sequential submits