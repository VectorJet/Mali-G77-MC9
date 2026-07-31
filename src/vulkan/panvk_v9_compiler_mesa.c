/* SPDX-License-Identifier: MIT
 *
 * Mesa NIR + Panfrost Valhall compiler adapter. This file is built as a
 * separate compiler library and deliberately has no kbase or Vulkan-loader
 * dependencies.
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panvk_v9_compiler.h"

#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "panfrost/bifrost/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

#define MALI_G77_GPU_ID 0x9000u

static pthread_once_t glsl_types_once = PTHREAD_ONCE_INIT;

static void initialize_glsl_types(void) {
    glsl_type_singleton_init_or_ref();
}

struct compile_diagnostic {
    char *buffer;
    size_t size;
};

static void compiler_error(struct compile_diagnostic *diagnostic,
                           const char *message) {
    if (diagnostic->buffer && diagnostic->size)
        snprintf(diagnostic->buffer, diagnostic->size, "%s", message);
}

static void spirv_debug(void *private_data, enum nir_spirv_debug_level level,
                        size_t spirv_offset, const char *message) {
    struct compile_diagnostic *diagnostic = private_data;
    if (level >= NIR_SPIRV_DEBUG_LEVEL_ERROR && diagnostic->buffer && diagnostic->size) {
        snprintf(diagnostic->buffer, diagnostic->size,
                 "SPIR-V word %zu: %s", spirv_offset, message);
    }
}

static bool spirv_has_function_entry_point(const uint32_t *spirv, size_t word_count,
                                           enum panvk_v9_shader_stage stage,
                                           const char *entry_point) {
    uint32_t entry_id = 0;
    for (size_t offset = 5; offset < word_count;) {
        uint16_t count = spirv[offset] >> 16;
        uint16_t opcode = spirv[offset] & 0xffff;
        if (!count || offset + count > word_count) return false;
        if (opcode == 15 && count >= 4 && spirv[offset + 1] == (uint32_t)stage) {
            const char *name = (const char *)&spirv[offset + 3];
            size_t name_bytes = (count - 3) * sizeof(uint32_t);
            const char *end = memchr(name, '\0', name_bytes);
            if (end && strlen(entry_point) == (size_t)(end - name) &&
                !memcmp(name, entry_point, (size_t)(end - name))) {
                entry_id = spirv[offset + 2];
                break;
            }
        }
        offset += count;
    }
    if (!entry_id) return false;

    for (size_t offset = 5; offset < word_count;) {
        uint16_t count = spirv[offset] >> 16;
        uint16_t opcode = spirv[offset] & 0xffff;
        if (!count || offset + count > word_count) return false;
        if (opcode == 54 && count >= 5 && spirv[offset + 2] == entry_id) return true;
        offset += count;
    }
    return false;
}

struct lower_descriptors_ctx {
    const struct panvk_v9_pipeline_layout *layout;
    bool unsupported;
};

static const struct panvk_v9_descriptor_binding *
find_descriptor_binding(const struct panvk_v9_pipeline_layout *layout,
                        uint32_t set, uint32_t binding) {
    if (!layout) return NULL;
    for (uint32_t i = 0; i < layout->binding_count; i++) {
        if (layout->bindings[i].set == set && layout->bindings[i].binding == binding)
            return &layout->bindings[i];
    }
    return NULL;
}

static bool lower_descriptor_intrinsic(nir_builder *builder,
                                       nir_instr *instruction, void *data) {
    if (instruction->type != nir_instr_type_intrinsic) return false;
    nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instruction);
    struct lower_descriptors_ctx *ctx = data;
    builder->cursor = nir_before_instr(instruction);
    nir_ssa_def *replacement = NULL;

    switch (intrinsic->intrinsic) {
    case nir_intrinsic_vulkan_resource_index: {
        const struct panvk_v9_descriptor_binding *binding = find_descriptor_binding(
            ctx->layout, nir_intrinsic_desc_set(intrinsic),
            nir_intrinsic_binding(intrinsic));
        if (!binding || (binding->descriptor_type != 6 && binding->descriptor_type != 8)) {
            ctx->unsupported = true;
            return false;
        }
        replacement = nir_vec2(builder,
            nir_iadd(builder, nir_imm_int(builder, binding->resource_index),
                     intrinsic->src[0].ssa),
            nir_imm_int(builder, 0));
        break;
    }
    case nir_intrinsic_vulkan_resource_reindex:
        replacement = nir_vec2(builder,
            nir_iadd(builder, nir_channel(builder, intrinsic->src[0].ssa, 0),
                     intrinsic->src[1].ssa),
            nir_channel(builder, intrinsic->src[0].ssa, 1));
        break;
    case nir_intrinsic_load_vulkan_descriptor:
        if (nir_intrinsic_desc_type(intrinsic) != 6 &&
            nir_intrinsic_desc_type(intrinsic) != 8) {
            ctx->unsupported = true;
            return false;
        }
        replacement = intrinsic->src[0].ssa;
        break;
    default:
        return false;
    }

    nir_ssa_def_rewrite_uses(&intrinsic->dest.ssa, replacement);
    nir_instr_remove(instruction);
    return true;
}

static bool lower_descriptors(nir_shader *nir,
                              const struct panvk_v9_pipeline_layout *layout) {
    struct lower_descriptors_ctx ctx = { .layout = layout };
    NIR_PASS_V(nir, nir_shader_instructions_pass, lower_descriptor_intrinsic,
               nir_metadata_block_index | nir_metadata_dominance, &ctx);
    return !ctx.unsupported;
}

static bool prepare_nir(nir_shader *nir,
                        const struct panvk_v9_pipeline_layout *layout) {
    /* Match the Vulkan runtime's canonical SPIR-V cleanup before applying
     * Panfrost-specific lowering. In particular, returns and helper functions
     * must not reach the Valhall backend. */
    NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
    NIR_PASS_V(nir, nir_lower_returns);
    NIR_PASS_V(nir, nir_inline_functions);
    NIR_PASS_V(nir, nir_copy_prop);
    NIR_PASS_V(nir, nir_opt_deref);
    nir_remove_non_entrypoints(nir);
    NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);
    NIR_PASS_V(nir, nir_split_var_copies);
    NIR_PASS_V(nir, nir_split_per_member_structs);
    NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);
    NIR_PASS_V(nir, nir_propagate_invariant, false);

    nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
    if (!entrypoint) return false;

    NIR_PASS_V(nir, nir_lower_io_to_temporaries, entrypoint, true, true);
    NIR_PASS_V(nir, nir_lower_indirect_derefs,
               nir_var_shader_in | nir_var_shader_out, UINT32_MAX);
    if (!lower_descriptors(nir, layout)) return false;
    NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo,
               nir_address_format_32bit_index_offset);
    NIR_PASS_V(nir, nir_opt_copy_prop_vars);
    NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);
    NIR_PASS_V(nir, nir_opt_trivial_continues);
    NIR_PASS_V(nir, nir_lower_system_values);
    NIR_PASS_V(nir, nir_split_var_copies);
    NIR_PASS_V(nir, nir_lower_var_copies);

    nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
                                nir->info.stage);
    nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                                nir->info.stage);
    NIR_PASS_V(nir, nir_lower_global_vars_to_local);
    nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
    return true;
}

int panvk_v9_compile_spirv(const uint32_t *spirv, size_t spirv_size,
                           enum panvk_v9_shader_stage stage,
                           const char *entry_point,
                           const struct panvk_v9_pipeline_layout *layout,
                           struct panvk_v9_compiled_shader *result,
                           char *error, size_t error_size) {
    if (!result) return -EINVAL;
    memset(result, 0, sizeof(*result));
    if (error && error_size) error[0] = '\0';

    if (!spirv || spirv_size < 20 || (spirv_size & 3) || !entry_point ||
        (stage != PANVK_V9_SHADER_VERTEX && stage != PANVK_V9_SHADER_FRAGMENT)) {
        if (error && error_size) snprintf(error, error_size, "invalid compiler arguments");
        return -EINVAL;
    }
    if (!spirv_has_function_entry_point(spirv, spirv_size / sizeof(uint32_t),
                                        stage, entry_point)) {
        if (error && error_size)
            snprintf(error, error_size, "SPIR-V entry point has no function body");
        return -EINVAL;
    }

    struct compile_diagnostic diagnostic = { error, error_size };
    pthread_once(&glsl_types_once, initialize_glsl_types);
    struct spirv_to_nir_options spirv_options = {
        .environment = NIR_SPIRV_VULKAN,
        .caps = {
            .variable_pointers = true,
        },
        .ubo_addr_format = nir_address_format_32bit_index_offset,
        .ssbo_addr_format = nir_address_format_64bit_global_32bit_offset,
        .push_const_addr_format = nir_address_format_32bit_offset,
        .shared_addr_format = nir_address_format_32bit_offset,
        .temp_addr_format = nir_address_format_32bit_offset,
        .debug = {
            .func = spirv_debug,
            .private_data = &diagnostic,
        },
    };

    nir_shader *nir = spirv_to_nir(spirv, spirv_size / sizeof(uint32_t),
                                    NULL, 0, (gl_shader_stage)stage,
                                    entry_point, &spirv_options,
                                    &bifrost_nir_options);
    if (!nir) {
        if (!error || !error[0]) compiler_error(&diagnostic, "SPIR-V to NIR conversion failed");
        return -EINVAL;
    }

    if (!prepare_nir(nir, layout)) {
        compiler_error(&diagnostic, "shader uses an unsupported descriptor binding");
        ralloc_free(nir);
        return -EINVAL;
    }

    struct panfrost_compile_inputs inputs = {
        .gpu_id = MALI_G77_GPU_ID,
        .fixed_sysval_ubo = -1,
        .no_idvs = true,
        .no_ubo_to_push = true,
        .nr_cbufs = stage == PANVK_V9_SHADER_FRAGMENT ? 1 : 0,
    };
    struct pan_shader_info info = {0};
    struct util_dynarray binary;
    util_dynarray_init(&binary, NULL);

    bifrost_compile_shader_nir(nir, &inputs, &binary, &info);
    ralloc_free(nir);

    if (!binary.size) {
        compiler_error(&diagnostic, "Valhall compiler produced an empty binary");
        util_dynarray_fini(&binary);
        return -EIO;
    }

    result->binary = malloc(binary.size);
    if (!result->binary) {
        util_dynarray_fini(&binary);
        return -ENOMEM;
    }
    memcpy(result->binary, binary.data, binary.size);
    result->binary_size = binary.size;
    result->work_reg_count = info.work_reg_count;
    result->tls_size = info.tls_size;
    result->preload = info.preload;
    result->contains_barrier = info.contains_barrier;
    result->ftz_fp16 = info.ftz_fp16;
    result->ftz_fp32 = info.ftz_fp32;
    result->outputs_written = info.outputs_written;
    if (stage == PANVK_V9_SHADER_FRAGMENT) {
        result->writes_depth = info.fs.writes_depth;
        result->writes_stencil = info.fs.writes_stencil;
        result->writes_coverage = info.fs.writes_coverage;
        result->can_discard = info.fs.can_discard;
    }

    util_dynarray_fini(&binary);
    return 0;
}

void panvk_v9_compiled_shader_cleanup(struct panvk_v9_compiled_shader *shader) {
    if (!shader) return;
    free(shader->binary);
    memset(shader, 0, sizeof(*shader));
}
