#!/usr/bin/env bash
set -e

PORT=${DEVICE_PORT:-8022}
USER=${DEVICE_USER:-u0_a375}
HOST=${DEVICE_HOST:-localhost}
TARGET_DIR="/data/data/com.termux/files/home"
REMOTE="${USER}@${HOST}"
BENCHMARK_ARGS=${1:-"--winsys xcb -d 1"}

echo "[1/4] Pushing Vulkan driver and compiler sources to device..."
scp -P ${PORT} src/driver/kbase_winsys.h src/driver/kbase_winsys.c \
              src/vulkan/pan_kmod_kbase.h src/vulkan/pan_kmod_kbase.c \
              src/vulkan/v9_pack.h src/vulkan/v9_cmd_stream.h src/vulkan/v9_cmd_stream.c \
              src/vulkan/panvk_v9_compiler.h src/vulkan/panvk_v9_compiler_mesa.c \
              src/vulkan/panvk_v9_entrypoints.h src/vulkan/panvk_v9_entrypoints.c \
              src/vulkan/panvk_v9_icd.json ${REMOTE}:${TARGET_DIR}/

echo "[2/4] Compiling native compiler library libpanvk_v9_compiler.so on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -fPIC -shared -o libpanvk_v9_compiler.so panvk_v9_compiler_mesa.c \
        -DHAVE_PTHREAD -DHAVE_STRUCT_TIMESPEC -D_GNU_SOURCE \
        -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 \
        -I${TARGET_DIR}/mesa_include \
        -I${TARGET_DIR}/mesa_src \
        -I${TARGET_DIR}/mesa_build_src/compiler/nir \
        -I${TARGET_DIR}/mesa_src/include \
        -I${TARGET_DIR}/mesa_src/gallium/include \
        -I${TARGET_DIR}/mesa_src/panfrost/include \
        -I${TARGET_DIR}/mesa_src/src \
        -I${TARGET_DIR}/mesa_src/src/panfrost \
        -L${TARGET_DIR} -lpanvk_unique_nocompiler -lm -lz -lstdc++ -pthread"

echo "[3/4] Compiling Vulkan ICD shared library libvulkan_panvk_v9.so on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -fPIC -shared -o libvulkan_panvk_v9.so \
        kbase_winsys.c pan_kmod_kbase.c v9_cmd_stream.c panvk_v9_entrypoints.c \
        -lX11 -lxcb -ldl -pthread"

echo "[4/4] Executing vkmark benchmark (${BENCHMARK_ARGS}) on device as root..."
ssh -p ${PORT} ${REMOTE} "su -c 'cd ${TARGET_DIR} && \
    DISPLAY=:0 \
    VK_ICD_FILENAMES=${TARGET_DIR}/panvk_v9_icd.json \
    LD_LIBRARY_PATH=${TARGET_DIR}:/data/data/com.termux/files/usr/lib \
    /data/data/com.termux/files/usr/bin/vkmark ${BENCHMARK_ARGS}'"
