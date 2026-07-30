#!/usr/bin/env bash
set -e

PORT=${DEVICE_PORT:-8022}
USER=${DEVICE_USER:-u0_a375}
HOST=${DEVICE_HOST:-localhost}
TARGET_DIR="/data/data/com.termux/files/home"
REMOTE="${USER}@${HOST}"

echo "[1/3] Pushing Vulkan Step 1 sources to device..."
scp -P ${PORT} src/driver/kbase_winsys.h src/driver/kbase_winsys.c \
              src/vulkan/pan_kmod_kbase.h src/vulkan/pan_kmod_kbase.c \
              src/vulkan/test_pan_kmod.c ${REMOTE}:${TARGET_DIR}/

echo "[2/3] Compiling test_pan_kmod on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -o test_pan_kmod kbase_winsys.c pan_kmod_kbase.c test_pan_kmod.c"

echo "[3/3] Running Vulkan Step 1 test_pan_kmod as root..."
ssh -p ${PORT} ${REMOTE} "su -c '${TARGET_DIR}/test_pan_kmod'"
