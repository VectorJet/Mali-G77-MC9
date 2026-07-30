#!/usr/bin/env bash
set -e

PORT=${DEVICE_PORT:-8022}
USER=${DEVICE_USER:-u0_a375}
HOST=${DEVICE_HOST:-localhost}
TARGET_DIR="/data/data/com.termux/files/home"
REMOTE="${USER}@${HOST}"

echo "[1/3] Pushing Vulkan Step 2 sources to device..."
scp -P ${PORT} src/driver/kbase_winsys.h src/driver/kbase_winsys.c \
              src/vulkan/pan_kmod_kbase.h src/vulkan/pan_kmod_kbase.c \
              src/vulkan/v9_pack.h src/vulkan/v9_cmd_stream.h src/vulkan/v9_cmd_stream.c \
              src/vulkan/test_v9_cmd_stream.c ${REMOTE}:${TARGET_DIR}/

echo "[2/3] Compiling test_v9_cmd_stream on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -o test_v9_cmd_stream kbase_winsys.c pan_kmod_kbase.c v9_cmd_stream.c test_v9_cmd_stream.c"

echo "[3/3] Running Vulkan Step 2 test_v9_cmd_stream as root..."
ssh -p ${PORT} ${REMOTE} "su -c '${TARGET_DIR}/test_v9_cmd_stream'"
