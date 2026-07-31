/*
 * Valhall v9 Command Stream Recorder Engine
 * Higher-level Vulkan-like command buffer API for Mali-G77
 */

#ifndef V9_CMD_STREAM_H
#define V9_CMD_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "pan_kmod_kbase.h"
#include "panvk_v9_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

struct v9_cmd_buffer;

struct v9_render_target_config {
    uint32_t width;
    uint32_t height;
    uint32_t clear_color;
};

struct v9_cmd_buffer *v9_cmd_buffer_create(struct pan_kmod_dev *dev,
                                           const struct v9_render_target_config *config);
struct v9_cmd_buffer *v9_cmd_buffer_ref(struct v9_cmd_buffer *cmd);
void v9_cmd_buffer_destroy(struct v9_cmd_buffer *cmd);

int v9_cmd_buffer_begin(struct v9_cmd_buffer *cmd);
int v9_cmd_buffer_set_fragment_shader(struct v9_cmd_buffer *cmd,
                                      const struct panvk_v9_compiled_shader *shader);
int v9_cmd_draw_indexed_triangle(struct v9_cmd_buffer *cmd);
int v9_cmd_buffer_end(struct v9_cmd_buffer *cmd);
int v9_cmd_buffer_submit(struct v9_cmd_buffer *cmd);

uint32_t v9_cmd_buffer_read_pixel(struct v9_cmd_buffer *cmd, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif

#endif /* V9_CMD_STREAM_H */
