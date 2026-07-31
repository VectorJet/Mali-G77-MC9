#!/usr/bin/env bash
set -e

PORT=${DEVICE_PORT:-8022}
USER=${DEVICE_USER:-u0_a375}
HOST=${DEVICE_HOST:-localhost}
TARGET_DIR="/data/data/com.termux/files/home"
REMOTE="${USER}@${HOST}"

echo "[1/4] Pushing Vulkan Step 4 sources to device..."
scp -P ${PORT} src/driver/kbase_winsys.h src/driver/kbase_winsys.c \
              src/vulkan/pan_kmod_kbase.h src/vulkan/pan_kmod_kbase.c \
              src/vulkan/v9_pack.h src/vulkan/v9_cmd_stream.h src/vulkan/v9_cmd_stream.c \
              src/vulkan/panvk_v9_compiler.h \
              src/vulkan/panvk_v9_entrypoints.h src/vulkan/panvk_v9_entrypoints.c \
              src/vulkan/panvk_v9_icd.json \
              src/vulkan/test_vulkan_loader_icd.c ${REMOTE}:${TARGET_DIR}/

echo "[2/4] Compiling shared library libvulkan_panvk_v9.so on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -fPIC -shared -o libvulkan_panvk_v9.so \
        kbase_winsys.c pan_kmod_kbase.c v9_cmd_stream.c panvk_v9_entrypoints.c \
        -lX11 -lxcb -ldl -pthread"

echo "[3/4] Compiling test_vulkan_loader_icd on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -o test_vulkan_loader_icd test_vulkan_loader_icd.c -ldl"

echo "[4/4] Running Vulkan Step 4 test_vulkan_loader_icd as root..."
ssh -p ${PORT} ${REMOTE} "su -c 'cd ${TARGET_DIR} && ${TARGET_DIR}/test_vulkan_loader_icd'"
