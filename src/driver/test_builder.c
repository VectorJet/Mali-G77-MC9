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
