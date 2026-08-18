/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panvk_v9_compiler.h"

static void *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) || (*size = (size_t)ftell(file), *size == 0) ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    void *data = malloc(*size);
    if (!data || fread(data, 1, *size, file) != *size) {
        free(data);
        data = NULL;
    }
    fclose(file);
    return data;
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "vertex") && strcmp(argv[1], "fragment"))) {
        fprintf(stderr, "usage: %s vertex|fragment shader.spv\n", argv[0]);
        return 2;
    }

    size_t spirv_size = 0;
    uint32_t *spirv = read_file(argv[2], &spirv_size);
    if (!spirv) {
        perror("read shader");
        return 1;
    }

    enum panvk_v9_shader_stage stage = !strcmp(argv[1], "vertex") ?
        PANVK_V9_SHADER_VERTEX : PANVK_V9_SHADER_FRAGMENT;
    struct panvk_v9_compiled_shader shader;
    char error[512];
    struct panvk_v9_descriptor_binding bindings[] = {
        { .set = 0, .binding = 0, .descriptor_type = 6 /* UNIFORM_BUFFER */, .array_size = 1, .resource_index = 0 },
        { .set = 0, .binding = 1, .descriptor_type = 6 /* UNIFORM_BUFFER */, .array_size = 1, .resource_index = 1 },
    };
    struct panvk_v9_pipeline_layout layout = {
        .bindings = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
        .ubo_count = 2,
    };
    int ret = panvk_v9_compile_spirv(spirv, spirv_size, stage, "main", &layout,
                                     &shader, error, sizeof(error));
    free(spirv);
    if (ret) {
        fprintf(stderr, "compile failed (%d): %s\n", ret, error);
        return 1;
    }

    printf("Valhall binary: %zu bytes, work registers: %u, preload: 0x%llx, TLS: %u\n",
           shader.binary_size, shader.work_reg_count,
           (unsigned long long)shader.preload, shader.tls_size);
    printf("Secondary: enable=%d offset=%u work_regs=%u preload=0x%llx\n",
           shader.secondary_enable, shader.secondary_offset,
           shader.secondary_work_reg_count, (unsigned long long)shader.secondary_preload);
    printf("Binary hex dump (%zu bytes):\n", shader.binary_size);
    const uint8_t *b = shader.binary;
    for (size_t i = 0; i < shader.binary_size; i += 8) {
        printf("  [%04zx] %02x %02x %02x %02x %02x %02x %02x %02x\n",
               i, b[i], b[i+1], b[i+2], b[i+3], b[i+4], b[i+5], b[i+6], b[i+7]);
    }
    if (!shader.binary_size || (shader.binary_size & 7)) {
        fprintf(stderr, "compiler returned an invalid Valhall instruction stream size\n");
        panvk_v9_compiled_shader_cleanup(&shader);
        return 1;
    }

    panvk_v9_compiled_shader_cleanup(&shader);
    return 0;
}
