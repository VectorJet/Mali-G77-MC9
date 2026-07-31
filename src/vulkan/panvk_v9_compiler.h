/* SPDX-License-Identifier: MIT */
#ifndef PANVK_V9_COMPILER_H
#define PANVK_V9_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum panvk_v9_shader_stage {
    PANVK_V9_SHADER_VERTEX = 0,
    PANVK_V9_SHADER_FRAGMENT = 4,
};

struct panvk_v9_compiled_shader {
    uint8_t *binary;
    size_t binary_size;
    uint32_t work_reg_count;
    uint32_t tls_size;
    uint64_t preload;
    bool contains_barrier;
    bool writes_depth;
    bool writes_stencil;
    bool writes_coverage;
    bool can_discard;
    bool ftz_fp16;
    bool ftz_fp32;
};

/* Compile Vulkan SPIR-V to Mali Valhall v9 machine code. The returned binary
 * is owned by the result and released with panvk_v9_compiled_shader_cleanup(). */
int panvk_v9_compile_spirv(const uint32_t *spirv, size_t spirv_size,
                           enum panvk_v9_shader_stage stage,
                           const char *entry_point,
                           struct panvk_v9_compiled_shader *result,
                           char *error, size_t error_size);

void panvk_v9_compiled_shader_cleanup(struct panvk_v9_compiled_shader *shader);

#ifdef __cplusplus
}
#endif

#endif
