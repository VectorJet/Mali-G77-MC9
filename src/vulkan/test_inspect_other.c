#include <stdio.h>
#include <stdint.h>
#include "pan_kmod_kbase.h"
#include "v9_cmd_stream.h"

int main() {
    struct pan_kmod_dev *dev = pan_kmod_dev_create(NULL);
    struct v9_render_target_config config = {
        .width = 800,
        .height = 600,
        .clear_color = 0xFF0000FF,
    };
    struct v9_cmd_buffer *cmd = v9_cmd_buffer_create(dev, &config);
    v9_cmd_buffer_begin(cmd);
    v9_cmd_draw_indexed_triangle(cmd);
    v9_cmd_buffer_end(cmd);
    v9_cmd_buffer_submit(cmd);

    uint32_t val0 = v9_cmd_buffer_read_pixel(cmd, 0, 0);
    uint32_t val1 = v9_cmd_buffer_read_pixel(cmd, 400, 300);
    uint32_t val2 = v9_cmd_buffer_read_pixel(cmd, 100, 100);
    printf("Pixel (0,0)=0x%08x, Pixel(400,300)=0x%08x, Pixel(100,100)=0x%08x\n",
           val0, val1, val2);

    /* Histogram of pixel values */
    uint32_t hist[256] = {0};
    for (uint32_t y = 0; y < config.height; y++) {
        for (uint32_t x = 0; x < config.width; x++) {
            uint32_t p = v9_cmd_buffer_read_pixel(cmd, x, y);
            hist[p & 0xFF]++;
        }
    }
    printf("Byte 0 histogram (Red component):\n");
    for (int i = 0; i < 256; i++) {
        if (hist[i]) printf("  0x%02x: %u pixels\n", i, hist[i]);
    }
    v9_cmd_buffer_destroy(cmd);
    pan_kmod_dev_destroy(dev);
    return 0;
}
