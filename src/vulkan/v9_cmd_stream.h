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

struct v9_ubo_binding {
    uint64_t address;
    uint32_t size;
    uint32_t index;
};

struct v9_attribute_binding {
    uint32_t format;
    uint32_t offset;
    uint32_t stride;
    uint32_t input_rate;
    uint64_t buffer_address;
    uint32_t buffer_size;
};

struct v9_cmd_buffer *v9_cmd_buffer_create(struct pan_kmod_dev *dev,
                                           const struct v9_render_target_config *config);
struct v9_cmd_buffer *v9_cmd_buffer_ref(struct v9_cmd_buffer *cmd);
void v9_cmd_buffer_destroy(struct v9_cmd_buffer *cmd);
void v9_cmd_buffer_set_config(struct v9_cmd_buffer *cmd, const struct v9_render_target_config *config);

int v9_cmd_buffer_begin(struct v9_cmd_buffer *cmd);
int v9_cmd_buffer_set_vertex_shader(struct v9_cmd_buffer *cmd,
                                     const struct panvk_v9_compiled_shader *shader);
int v9_cmd_buffer_set_fragment_shader(struct v9_cmd_buffer *cmd,
                                      const struct panvk_v9_compiled_shader *shader);
int v9_cmd_buffer_set_ubos(struct v9_cmd_buffer *cmd,
                           const struct v9_ubo_binding *bindings,
                           uint32_t binding_count);
int v9_cmd_buffer_set_attributes(struct v9_cmd_buffer *cmd,
                                 const struct v9_attribute_binding *bindings,
                                 uint32_t binding_count);
int v9_cmd_draw_indexed_triangle(struct v9_cmd_buffer *cmd);
int v9_cmd_draw_indexed(struct v9_cmd_buffer *cmd,
                        uint64_t idx_gpu, uint32_t index_count, uint32_t index_type,
                        uint64_t pos_gpu, uint32_t vertex_count);
int v9_cmd_buffer_update_transformed_vertices(struct v9_cmd_buffer *cmd,
                                              const float *mvp_matrix,
                                              const float *positions,
                                              const float *normals,
                                              const float *colors,
                                              uint32_t vertex_count);
int v9_cmd_buffer_end(struct v9_cmd_buffer *cmd);
int v9_cmd_buffer_submit(struct v9_cmd_buffer *cmd);

uint64_t v9_cmd_buffer_get_pos_gpu(struct v9_cmd_buffer *cmd);
uint64_t v9_cmd_buffer_get_idx_gpu(struct v9_cmd_buffer *cmd);
uint32_t v9_cmd_buffer_read_pixel(struct v9_cmd_buffer *cmd, uint32_t x, uint32_t y);
void *v9_cmd_buffer_get_color_cpu(struct v9_cmd_buffer *cmd);
uint32_t v9_cmd_buffer_get_width(struct v9_cmd_buffer *cmd);
uint32_t v9_cmd_buffer_get_height(struct v9_cmd_buffer *cmd);
size_t v9_cmd_buffer_get_color_size(struct v9_cmd_buffer *cmd);

#ifdef __cplusplus
}
#endif

#endif /* V9_CMD_STREAM_H */
