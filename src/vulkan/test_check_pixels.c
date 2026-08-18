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

    uint32_t green = 0, red = 0, other = 0;
    for (uint32_t y = 0; y < config.height; y++) {
        for (uint32_t x = 0; x < config.width; x++) {
            uint32_t p = v9_cmd_buffer_read_pixel(cmd, x, y);
            if (p == 0xFF00FF00) green++;
            else if (p == 0xFF0000FF) red++;
            else other++;
        }
    }
    printf("Exact Pixel Count: green=%u, red=%u, other=%u (total=%u)\n",
           green, red, other, config.width * config.height);
    v9_cmd_buffer_destroy(cmd);
    pan_kmod_dev_destroy(dev);
    return 0;
}
