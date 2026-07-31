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
    int ret = panvk_v9_compile_spirv(spirv, spirv_size, stage, "main",
                                     &shader, error, sizeof(error));
    free(spirv);
    if (ret) {
        fprintf(stderr, "compile failed (%d): %s\n", ret, error);
        return 1;
    }

    printf("Valhall binary: %zu bytes, work registers: %u, preload: 0x%llx, TLS: %u\n",
           shader.binary_size, shader.work_reg_count,
           (unsigned long long)shader.preload, shader.tls_size);
    if (!shader.binary_size || (shader.binary_size & 7)) {
        fprintf(stderr, "compiler returned an invalid Valhall instruction stream size\n");
        panvk_v9_compiled_shader_cleanup(&shader);
        return 1;
    }

    panvk_v9_compiled_shader_cleanup(&shader);
    return 0;
}
