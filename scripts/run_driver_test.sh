#!/usr/bin/env bash
set -e

PORT=${DEVICE_PORT:-8022}
USER=${DEVICE_USER:-u0_a659}
HOST=${DEVICE_HOST:-localhost}
TARGET_DIR="/data/data/com.termux/files/home"
REMOTE="${USER}@${HOST}"

echo "[1/3] Pushing driver sources to device..."
scp -P ${PORT} src/driver/kbase_winsys.h src/driver/kbase_winsys.c \
              src/driver/v9_builder.h src/driver/v9_builder.c \
              src/driver/test_winsys.c src/driver/test_builder.c ${REMOTE}:${TARGET_DIR}/

echo "[2/3] Compiling test_winsys and test_builder on device..."
ssh -p ${PORT} ${REMOTE} "cd ${TARGET_DIR} && \
    gcc -Wall -O2 -o test_winsys kbase_winsys.c test_winsys.c && \
    gcc -Wall -O2 -o test_builder kbase_winsys.c v9_builder.c test_builder.c"

echo "[3/3] Running Phase 1 test_winsys as root..."
ssh -p ${PORT} ${REMOTE} "su -c '${TARGET_DIR}/test_winsys'"

echo "[4/4] Running Phase 2 test_builder as root..."
ssh -p ${PORT} ${REMOTE} "su -c '${TARGET_DIR}/test_builder'"
