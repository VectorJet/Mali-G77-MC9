#!/usr/bin/env bash
set -euo pipefail

DEVICE_HOST="${DEVICE_HOST:-localhost}"
DEVICE_PORT="${DEVICE_PORT:-8022}"
DEVICE_USER="${DEVICE_USER:-u0_a659}"

SRC_LOCAL="src/kbase/egl_dumper_vendor.c"
SRC_REMOTE="/data/data/com.termux/files/home/egl_dumper_vendor.c"
BIN_REMOTE="/data/local/tmp/egl_dumper"
TERMUX_CLANG="/data/data/com.termux/files/usr/bin/clang"

if [[ ! -f "$SRC_LOCAL" ]]; then
    echo "Missing source file: $SRC_LOCAL" >&2
    exit 1
fi

echo "[1/3] Push source to device: ${DEVICE_USER}@${DEVICE_HOST}:${SRC_REMOTE}"
scp -P "$DEVICE_PORT" "$SRC_LOCAL" "${DEVICE_USER}@${DEVICE_HOST}:${SRC_REMOTE}"

echo "[2/3] Compile standalone binary in /data/local/tmp as root"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${TERMUX_CLANG} -o ${BIN_REMOTE} ${SRC_REMOTE} -ldl -llog && chmod 755 ${BIN_REMOTE}'"

echo "[3/3] Run ${BIN_REMOTE} as root outside the Termux app context"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${BIN_REMOTE}'"
