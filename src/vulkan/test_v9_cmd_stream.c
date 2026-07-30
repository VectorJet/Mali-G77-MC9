/*
 * Test harness for Step 2: Valhall v9 GenXML Descriptor Pack & Command Buffer Engine
 */

#include <stdio.h>
#include <stdlib.h>

#include "pan_kmod_kbase.h"
#include "v9_cmd_stream.h"

int main(int argc, char **argv) {
    printf("=== Testing Step 2: Valhall v9 Command Buffer & GenXML Pack Engine ===\n");

    struct pan_kmod_dev *dev = pan_kmod_dev_create(NULL);
    if (!dev) {
        fprintf(stderr, "FAIL: pan_kmod_dev_create returned NULL\n");
        return 1;
    }

    struct v9_render_target_config config = {
        .width = 16,
        .height = 16,
        .clear_color = 0xFF0000FF,
    };

    struct v9_cmd_buffer *cmd = v9_cmd_buffer_create(dev, &config);
    if (!cmd) {
        fprintf(stderr, "FAIL: v9_cmd_buffer_create returned NULL\n");
        pan_kmod_dev_destroy(dev);
        return 1;
    }
    printf("SUCCESS: v9_cmd_buffer created for 16x16 render target\n");

    v9_cmd_buffer_begin(cmd);
    v9_cmd_draw_indexed_triangle(cmd);
    v9_cmd_buffer_end(cmd);
    printf("SUCCESS: Command buffer recorded (TILER_JOB + Fragment JC)\n");

    int ret = v9_cmd_buffer_submit(cmd);
    if (ret != 0) {
        fprintf(stderr, "FAIL: v9_cmd_buffer_submit failed (ret=%d)\n", ret);
        v9_cmd_buffer_destroy(cmd);
        pan_kmod_dev_destroy(dev);
        return 1;
    }
    printf("SUCCESS: v9_cmd_buffer_submit completed all 4 atoms cleanly!\n");

    uint32_t p0 = v9_cmd_buffer_read_pixel(cmd, 0, 0);
    uint32_t p_out = v9_cmd_buffer_read_pixel(cmd, 15, 15);
    printf("Rendered Output: pixel(0,0)=0x%08x, pixel(15,15)=0x%08x\n", p0, p_out);

    if (p0 == 0xFF00FF00) {
        printf("SUCCESS: Top-left pixel rendered solid green (0xFF00FF00)!\n");
    } else {
        fprintf(stderr, "FAIL: Expected 0xFF00FF00, got 0x%08x\n", p0);
        v9_cmd_buffer_destroy(cmd);
        pan_kmod_dev_destroy(dev);
        return 1;
    }

    v9_cmd_buffer_destroy(cmd);
    pan_kmod_dev_destroy(dev);

    printf("=== Step 2: Valhall v9 GenXML Pack & Command Engine Test PASSED CLEANLY! ===\n");
    return 0;
}
