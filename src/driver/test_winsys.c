#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kbase_winsys.h"

int main(void) {
    printf("=== Testing Mali-G77 kbase Winsys Layer (Phase 1) ===\n");

    /* 1. Open GPU Device */
    struct kbase_dev *dev = kbase_dev_open("/dev/mali0");
    if (!dev) {
        fprintf(stderr, "FAIL: Could not open GPU device\n");
        return 1;
    }
    printf("SUCCESS: GPU device opened successfully\n");

    /* 2. Allocate BOs (Read/Write and Executable) */
    struct kbase_bo *rw_bo = kbase_bo_alloc(dev, 64 * 1024, KBASE_BO_PROT_READ | KBASE_BO_PROT_WRITE);
    if (!rw_bo) {
        fprintf(stderr, "FAIL: Could not allocate RW BO\n");
        kbase_dev_close(dev);
        return 1;
    }
    printf("SUCCESS: RW BO allocated at CPU/GPU VA 0x%llx (size %zu B)\n",
           (unsigned long long)rw_bo->gpu, rw_bo->size);

    struct kbase_bo *exec_bo = kbase_bo_alloc(dev, 4 * 1024, KBASE_BO_PROT_READ | KBASE_BO_PROT_WRITE | KBASE_BO_PROT_EXEC);
    if (!exec_bo) {
        fprintf(stderr, "FAIL: Could not allocate Executable BO\n");
        kbase_bo_free(rw_bo);
        kbase_dev_close(dev);
        return 1;
    }
    printf("SUCCESS: Executable BO allocated at CPU/GPU VA 0x%llx (size %zu B)\n",
           (unsigned long long)exec_bo->gpu, exec_bo->size);

    /* 3. Setup Cache Flush Job (Atom Type 3) */
    uint32_t *fl = (uint32_t *)rw_bo->cpu;
    memset(fl, 0, 64);
    fl[4] = (3u << 1);        /* Atom Type = 3 (Cache Flush) */
    fl[8] = 0xFFFFFFFFu;      /* Clean core caches */
    fl[9] = 0xFFFFFFFFu;      /* Clean L2 caches */

    int ret = kbase_submit_job(dev, rw_bo->gpu, KBASE_QUEUE_REQ_FLUSH, 1, 0, 0);
    if (ret < 0) {
        fprintf(stderr, "FAIL: kbase_submit_job failed (ret=%d)\n", ret);
        kbase_bo_free(exec_bo);
        kbase_bo_free(rw_bo);
        kbase_dev_close(dev);
        return 1;
    }
    printf("SUCCESS: Cache Flush atom submitted (JC=0x%llx, core_req=0x%x)\n",
           (unsigned long long)rw_bo->gpu, KBASE_QUEUE_REQ_FLUSH);

    /* 4. Wait for completion event */
    uint32_t atom_nr = 0, event_code = 0;
    ret = kbase_wait_event(dev, &atom_nr, &event_code);
    if (ret < 0) {
        fprintf(stderr, "FAIL: kbase_wait_event failed (ret=%d)\n", ret);
        kbase_bo_free(exec_bo);
        kbase_bo_free(rw_bo);
        kbase_dev_close(dev);
        return 1;
    }
    printf("SUCCESS: Received event code=0x%x for atom=%u (%s)\n",
           event_code, atom_nr, (event_code == 0x1) ? "0x1 DONE" : "FAILED");

    /* 5. Clean up */
    kbase_bo_free(exec_bo);
    kbase_bo_free(rw_bo);
    kbase_dev_close(dev);

    printf("=== Winsys Layer Test Completed Successfully! ===\n");
    return (event_code == 0x1) ? 0 : 1;
}
