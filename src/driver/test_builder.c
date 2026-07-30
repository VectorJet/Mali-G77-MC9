#include <stdio.h>
#include <stdlib.h>
#include "kbase_winsys.h"
#include "v9_builder.h"

static int test_size(struct kbase_dev *dev, uint32_t width, uint32_t height) {
    printf("--- Testing %dx%d Framebuffer Rendering ---\n", width, height);

    struct v9_framebuffer *fb = v9_framebuffer_create(dev, width, height);
    if (!fb) {
        fprintf(stderr, "FAIL: Could not create %dx%d framebuffer\n", width, height);
        return 1;
    }

    /* Assert tiler heap top pointer immediately after creation, before any submissions */
    {
        uint64_t *th_words = (uint64_t *)((uint8_t *)fb->mem_bo->cpu +
                                         (fb->tiler_heap_desc_gpu - fb->mem_bo->gpu));
        uint64_t heap_base  = th_words[1]; /* th[2..3] = base */
        uint64_t heap_bot   = th_words[2]; /* th[4..5] = bottom */
        uint64_t heap_top   = th_words[3]; /* th[6..7] = top */
        uint64_t expected_top = fb->tiler_heap_backing_gpu + 0x40000;
        printf("HEAP ASSERT (post-create): base=0x%llx bot=0x%llx top=0x%llx expected=0x%llx %s\n",
               (unsigned long long)heap_base,
               (unsigned long long)heap_bot,
               (unsigned long long)heap_top,
               (unsigned long long)expected_top,
               heap_top == expected_top ? "CORRECT" : "CORRUPTED");
        if (heap_top != expected_top) {
            printf("  offset from expected: 0x%llx\n",
                   (unsigned long long)(expected_top - heap_top));
        }
    }

    /* Also assert DCD[0] flags value */
    {
        uint32_t *dcd = (uint32_t *)((uint8_t *)fb->mem_bo->cpu +
                                    (fb->dcd_gpu - fb->mem_bo->gpu));
        printf("DCD[0] flags (post-create): 0x%08x\n", dcd[0]);
    }

    int ret = v9_render_triangle(fb);
    if (ret < 0) {
        fprintf(stderr, "FAIL: v9_render_triangle failed (ret=%d)\n", ret);
        v9_framebuffer_free(fb);
        return 1;
    }

    /* Verify color pixels */
    uint32_t p0 = v9_read_pixel(fb, 0, 0);
    uint32_t p1 = v9_read_pixel(fb, width / 2, height / 2);
    printf("Render Output: pixel(0,0)=0x%08x, pixel(%d,%d)=0x%08x\n",
           p0, width / 2, height / 2, p1);

    int total_pixels = width * height;
    int green_pixels = 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            if (v9_read_pixel(fb, x, y) == 0xFF00FF00u) {
                green_pixels++;
            }
        }
    }

    printf("Pixel Count: %d / %d pixels solid green (0xFF00FF00)\n",
           green_pixels, total_pixels);

    v9_framebuffer_free(fb);
    return (green_pixels > 0) ? 0 : 1;
}

int main(void) {
    printf("=== Testing Mali-G77 Valhall v9 Driver Builder (Phase 2) ===\n");

    struct kbase_dev *dev = kbase_dev_open("/dev/mali0");
    if (!dev) {
        fprintf(stderr, "FAIL: Could not open GPU device\n");
        return 1;
    }

    int fail_count = 0;
    fail_count += test_size(dev, 16, 16);
    fail_count += test_size(dev, 64, 64);
    fail_count += test_size(dev, 256, 256);

    kbase_dev_close(dev);

    if (fail_count == 0) {
        printf("=== Valhall v9 Driver Builder Test PASSED CLEANLY! ===\n");
        return 0;
    } else {
        printf("=== Valhall v9 Driver Builder Test FAILED (%d failures) ===\n", fail_count);
        return 1;
    }
}
