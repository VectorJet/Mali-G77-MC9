#!/usr/bin/env bash
set -euo pipefail

DEVICE_HOST="${DEVICE_HOST:-localhost}"
DEVICE_PORT="${DEVICE_PORT:-8022}"
DEVICE_USER="${DEVICE_USER:-u0_a659}"
MODE="${1:-exact4}"

SRC_LOCAL="src/kbase/replay/replay_egl_triangle.c"
ASSET_DIR_LOCAL="captured_shaders/egl_dumper_root_preload_2026-04-17"
TERMUX_STAGE_DIR="/data/data/com.termux/files/home"
TERMUX_CLANG="/data/data/com.termux/files/usr/bin/clang"
SRC_REMOTE="${TERMUX_STAGE_DIR}/replay_egl_triangle.c"
ASSET_DIR_REMOTE="${TERMUX_STAGE_DIR}/egl_dumper_root_preload_2026-04-17"
BIN_REMOTE="/data/local/tmp/replay_egl_triangle"

if [[ ! -f "$SRC_LOCAL" ]]; then
    echo "Missing source file: $SRC_LOCAL" >&2
    exit 1
fi
if [[ ! -d "$ASSET_DIR_LOCAL" ]]; then
    echo "Missing asset directory: $ASSET_DIR_LOCAL" >&2
    exit 1
fi

echo "[1/4] Push replay source"
scp -P "$DEVICE_PORT" "$SRC_LOCAL" "${DEVICE_USER}@${DEVICE_HOST}:${SRC_REMOTE}"

echo "[2/4] Push captured assets"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" "rm -rf ${ASSET_DIR_REMOTE} && mkdir -p ${ASSET_DIR_REMOTE}"
scp -P "$DEVICE_PORT" "${ASSET_DIR_LOCAL}"/*.bin "${DEVICE_USER}@${DEVICE_HOST}:${ASSET_DIR_REMOTE}/"

echo "[3/4] Build replay binary in /data/local/tmp as root"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${TERMUX_CLANG} -O0 -g -o ${BIN_REMOTE} ${SRC_REMOTE} && chmod 755 ${BIN_REMOTE}'"

echo "[4/4] Run replay binary as root"
ENV_PASS=""
for v in SHADER_PFM SHADER_SKIP_ATEST SHADER_MINIMAL SHADER_RED SHADER_TILER SHADER_HELPERS; do
    if [[ -n "${!v:-}" ]]; then
        ENV_PASS="${ENV_PASS} ${v}=${!v}"
    fi
done
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${ENV_PASS} ${BIN_REMOTE} ${ASSET_DIR_REMOTE} ${MODE}'"
