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

/* GLIBC compatibility globals for Bionic */
char *program_invocation_name = (char *)"vkmark";
char *program_invocation_short_name = (char *)"vkmark";

#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "panfrost/bifrost/bifrost_compile.h"
#include "panfrost/bifrost/valhall/disassemble.h"
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

#define PANVK_LOG(...) do { if (getenv("PANVK_DEBUG")) { fprintf(stderr, __VA_ARGS__); fflush(stderr); } } while(0)

static const struct panvk_v9_descriptor_binding *
find_descriptor_binding(const struct panvk_v9_pipeline_layout *layout,
                        uint32_t set, uint32_t binding_idx) {
    PANVK_LOG("DEBUG: find_descriptor_binding layout=%p count=%u search_set=%u search_binding=%u\n",
              (void*)layout, layout ? layout->binding_count : 0, set, binding_idx);
    if (!layout) return NULL;
    for (uint32_t i = 0; i < layout->binding_count; ++i) {
        PANVK_LOG("DEBUG: binding[%u]: set=%u binding=%u type=%u res_idx=%u\n",
                  i, layout->bindings[i].set, layout->bindings[i].binding, layout->bindings[i].descriptor_type, layout->bindings[i].resource_index);
        if (layout->bindings[i].set == set && layout->bindings[i].binding == binding_idx)
            return &layout->bindings[i];
    }
    return NULL;
}

static bool lower_descriptor_intrinsic(nir_builder *builder,
                                       nir_instr *instruction, void *data) {
    struct lower_descriptors_ctx *ctx = data;
    PANVK_LOG("DEBUG: lower_descriptor_intrinsic type=%d\n", instruction->type);
    if (instruction->type == nir_instr_type_deref) {
        nir_deref_instr *deref = nir_instr_as_deref(instruction);
        if (deref->deref_type == nir_deref_type_cast || (deref->modes & nir_var_mem_ubo)) {
            deref->dest.ssa.num_components = 2;
        } else if (deref->parent.is_ssa && deref->parent.ssa && deref->parent.ssa->num_components == 2) {
            deref->dest.ssa.num_components = 2;
        }
        return false;
    }
    if (instruction->type != nir_instr_type_intrinsic) return false;
    nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instruction);
    builder->cursor = nir_before_instr(instruction);
    nir_ssa_def *replacement = NULL;

    switch (intrinsic->intrinsic) {
    case nir_intrinsic_vulkan_resource_index: {
        uint32_t set = nir_intrinsic_desc_set(intrinsic);
        uint32_t binding_idx = nir_intrinsic_binding(intrinsic);
        const struct panvk_v9_descriptor_binding *binding = find_descriptor_binding(
            ctx->layout, set, binding_idx);
        uint32_t res_idx = binding ? binding->resource_index : 0;
        replacement = nir_vec2(builder,
            nir_iadd(builder, nir_imm_int(builder, res_idx),
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
        replacement = intrinsic->src[0].ssa;
        break;
    default:
        return false;
    }

    nir_ssa_def_rewrite_uses(&intrinsic->dest.ssa, replacement);
    nir_instr_remove(instruction);
    PANVK_LOG("DEBUG: lowered intrinsic %d successfully\n", intrinsic->intrinsic);
    return true;
}

static void fixup_ubo_derefs(nir_shader *nir) {
    nir_foreach_function(func, nir) {
        if (!func->impl) continue;
        nir_foreach_block(block, func->impl) {
            nir_foreach_instr_safe(instr, block) {
                if (instr->type == nir_instr_type_deref) {
                    nir_deref_instr *deref = nir_instr_as_deref(instr);
                    if (deref->deref_type == nir_deref_type_cast || (deref->modes & nir_var_mem_ubo)) {
                        deref->dest.ssa.num_components = 2;
                    } else if (deref->parent.is_ssa && deref->parent.ssa && deref->parent.ssa->num_components == 2) {
                        deref->dest.ssa.num_components = 2;
                    }
                }
            }
        }
    }
}

static bool lower_descriptors(nir_shader *nir,
                              const struct panvk_v9_pipeline_layout *layout) {
    struct lower_descriptors_ctx ctx = { layout, false };
    fixup_ubo_derefs(nir);
    bool progress = nir_shader_instructions_pass(nir,
                                                 lower_descriptor_intrinsic,
                                                 nir_metadata_none,
                                                 &ctx);
    if (ctx.unsupported) return false;
    fixup_ubo_derefs(nir);
    return progress || layout != NULL;
}

static bool prepare_nir(nir_shader *nir,
                        const struct panvk_v9_pipeline_layout *layout) {
    PANVK_LOG("DEBUG: prepare_nir start, nir=%p\n", (void*)nir);
    nir_lower_variable_initializers(nir, nir_var_function_temp);
    nir_lower_returns(nir);
    nir_inline_functions(nir);
    nir_copy_prop(nir);
    nir_opt_deref(nir);
    nir_remove_non_entrypoints(nir);
    nir_lower_variable_initializers(nir, (nir_variable_mode)~0);
    nir_split_var_copies(nir);
    nir_split_per_member_structs(nir);
    nir_lower_clip_cull_distance_arrays(nir);
    nir_propagate_invariant(nir, false);

    nir_function_impl *entrypoint = NULL;
    nir_foreach_function(func, nir) {
        if (func->is_entrypoint && func->impl) {
            entrypoint = func->impl;
            break;
        }
    }
    if (!entrypoint) return false;

    nir_lower_io_to_temporaries(nir, entrypoint, true, true);
    nir_lower_indirect_derefs(nir, nir_var_shader_in | nir_var_shader_out, UINT32_MAX);
    if (!lower_descriptors(nir, layout)) {
        return false;
    }
    if (getenv("PANVK_DEBUG")) {
        nir_print_shader(nir, stderr);
    }
    nir_lower_explicit_io(nir, nir_var_mem_ubo, nir_address_format_32bit_index_offset);
    nir_opt_copy_prop_vars(nir);
    nir_opt_combine_stores(nir, nir_var_all);
    nir_opt_trivial_continues(nir);
    nir_lower_system_values(nir);
    nir_split_var_copies(nir);
    nir_lower_var_copies(nir);

    nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
    nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, nir->info.stage);
    nir_lower_global_vars_to_local(nir);
    nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
    PANVK_LOG("DEBUG: prepare_nir finished successfully\n");
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

    PANVK_LOG("DEBUG: entering panvk_v9_compile_spirv stage=%d entry='%s' size=%zu\n",
              stage, entry_point, spirv_size);
    struct compile_diagnostic diagnostic = { error, error_size };
    pthread_once(&glsl_types_once, initialize_glsl_types);
    struct spirv_to_nir_options spirv_options;
    memset(&spirv_options, 0, sizeof(spirv_options));
    spirv_options.environment = NIR_SPIRV_VULKAN;
    spirv_options.global_addr_format = nir_address_format_32bit_global;
    spirv_options.temp_addr_format = nir_address_format_32bit_offset;
    spirv_options.constant_addr_format = nir_address_format_32bit_offset;
    spirv_options.task_payload_addr_format = nir_address_format_32bit_offset;
    spirv_options.shared_addr_format = nir_address_format_32bit_offset;
    spirv_options.push_const_addr_format = nir_address_format_32bit_offset;
    spirv_options.ubo_addr_format = nir_address_format_32bit_index_offset;
    spirv_options.phys_ssbo_addr_format = nir_address_format_64bit_global_32bit_offset;
    spirv_options.ssbo_addr_format = nir_address_format_64bit_global_32bit_offset;
    spirv_options.debug.func = spirv_debug;
    spirv_options.debug.private_data = &diagnostic;

    nir_shader *nir = spirv_to_nir(spirv, spirv_size / sizeof(uint32_t),
                                    NULL, 0, (gl_shader_stage)stage,
                                    entry_point, &spirv_options,
                                    &bifrost_nir_options);
    if (!nir) {
        if (!error || !error[0]) compiler_error(&diagnostic, "SPIR-V to NIR conversion failed");
        return -EINVAL;
    }
    PANVK_LOG("DEBUG: spirv_to_nir completed successfully, nir=%p\n", (void*)nir);

    if (!prepare_nir(nir, layout)) {
        compiler_error(&diagnostic, "shader uses an unsupported descriptor binding");
        ralloc_free(nir);
        return -EINVAL;
    }

    struct panfrost_compile_inputs inputs = {
        .gpu_id = MALI_G77_GPU_ID,
        .fixed_sysval_ubo = -1,
        .no_idvs = stage != PANVK_V9_SHADER_VERTEX,
        .no_ubo_to_push = true,
        .nr_cbufs = stage == PANVK_V9_SHADER_FRAGMENT ? 1 : 0,
    };
    struct pan_shader_info info = {0};
    struct util_dynarray binary;
    util_dynarray_init(&binary, NULL);

    bifrost_compile_shader_nir(nir, &inputs, &binary, &info);
    ralloc_free(nir);
    PANVK_LOG("DEBUG: bifrost_compile_shader_nir completed successfully (size=%zu)\n", (size_t)binary.size);

    FILE *flog = fopen("/data/data/com.termux/files/usr/tmp/panvk_debug.log", "a");
    if (flog) {
        fprintf(flog, "=== COMPILED SHADER STAGE %d (size=%u bytes) ===\n", stage, (unsigned)binary.size);
        fprintf(flog, "work_reg_count=%u, tls_size=%u, preload=0x%llx, barrier=%d, ftz16=%d, ftz32=%d, outputs=0x%llx\n",
                info.work_reg_count, info.tls_size, (unsigned long long)info.preload,
                info.contains_barrier, info.ftz_fp16, info.ftz_fp32,
                (unsigned long long)info.outputs_written);
        fprintf(flog, "attributes_read_count=%u, attribute_count=%u, ubo_count=%u, ubo_mask=0x%x\n",
                info.attributes_read_count, info.attribute_count, info.ubo_count, info.ubo_mask);
        fprintf(flog, "varyings: input_count=%u, output_count=%u\n",
                info.varyings.input_count, info.varyings.output_count);
        for (unsigned i = 0; i < info.varyings.input_count; i++) {
            fprintf(flog, "  var in[%u]: location=%d, format=%d\n",
                    i, info.varyings.input[i].location, info.varyings.input[i].format);
        }
        for (unsigned i = 0; i < info.varyings.output_count; i++) {
            fprintf(flog, "  var out[%u]: location=%d, format=%d\n",
                    i, info.varyings.output[i].location, info.varyings.output[i].format);
        }
        if (stage == PANVK_V9_SHADER_VERTEX) {
            fprintf(flog, "VS: idvs=%d, secondary_enable=%d, sec_offset=%u, sec_work_regs=%u, sec_preload=0x%llx\n",
                    info.vs.idvs, info.vs.secondary_enable, info.vs.secondary_offset,
                    info.vs.secondary_work_reg_count, (unsigned long long)info.vs.secondary_preload);
        }
        if (stage == PANVK_V9_SHADER_FRAGMENT) {
            fprintf(flog, "FS: writes_depth=%d, writes_stencil=%d, can_discard=%d, reads_coord=%d, reads_face=%d\n",
                    info.fs.writes_depth, info.fs.writes_stencil, info.fs.can_discard,
                    info.fs.reads_frag_coord, info.fs.reads_face);
        }
        fprintf(flog, "===============================================\n");
        fflush(flog);
        fclose(flog);
    }

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
    if (stage == PANVK_V9_SHADER_VERTEX) {
        result->idvs = info.vs.idvs;
        result->no_psiz_offset = info.vs.no_psiz_offset;
        result->secondary_enable = info.vs.secondary_enable;
        result->secondary_offset = info.vs.secondary_offset;
        result->secondary_work_reg_count = info.vs.secondary_work_reg_count;
        result->secondary_preload = info.vs.secondary_preload;
    }
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
