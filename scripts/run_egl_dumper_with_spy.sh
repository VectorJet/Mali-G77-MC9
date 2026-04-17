#!/usr/bin/env bash
set -euo pipefail

DEVICE_HOST="${DEVICE_HOST:-localhost}"
DEVICE_PORT="${DEVICE_PORT:-8022}"
DEVICE_USER="${DEVICE_USER:-u0_a659}"

DUMPER_SRC_LOCAL="src/kbase/egl_dumper_vendor.c"
SPY_SRC_LOCAL="src/kbase/ioctl_spy.c"
TERMUX_STAGE_DIR="/data/data/com.termux/files/home"
TERMUX_CLANG="/data/data/com.termux/files/usr/bin/clang"
TMP_DUMPER_SRC="/data/data/com.termux/files/home/egl_dumper_vendor.c"
TMP_SPY_SRC="/data/data/com.termux/files/home/ioctl_spy.c"
TMP_DUMPER_BIN="/data/local/tmp/egl_dumper"
TMP_SPY_SO="/data/local/tmp/ioctl_spy.so"
TMP_CAPTURE_DIR="/data/local/tmp/mali_capture"

for src in "$DUMPER_SRC_LOCAL" "$SPY_SRC_LOCAL"; do
    if [[ ! -f "$src" ]]; then
        echo "Missing source file: $src" >&2
        exit 1
    fi
done

echo "[1/5] Push dumper and spy sources to device staging area"
scp -P "$DEVICE_PORT" "$DUMPER_SRC_LOCAL" "${DEVICE_USER}@${DEVICE_HOST}:${TMP_DUMPER_SRC}"
scp -P "$DEVICE_PORT" "$SPY_SRC_LOCAL" "${DEVICE_USER}@${DEVICE_HOST}:${TMP_SPY_SRC}"

echo "[2/5] Build /data/local/tmp/egl_dumper as root"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${TERMUX_CLANG} -o ${TMP_DUMPER_BIN} ${TMP_DUMPER_SRC} -ldl -llog && chmod 755 ${TMP_DUMPER_BIN}'"

echo "[3/5] Build /data/local/tmp/ioctl_spy.so as root"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${TERMUX_CLANG} -shared -fPIC -o ${TMP_SPY_SO} ${TMP_SPY_SRC} -ldl -llog && chmod 755 ${TMP_SPY_SO}'"

echo "[4/5] Reset capture directory"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c 'rm -rf ${TMP_CAPTURE_DIR} && mkdir -p ${TMP_CAPTURE_DIR} && chmod 777 ${TMP_CAPTURE_DIR}'"

echo "[5/5] Run dumper with direct root-side LD_PRELOAD injection"
ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c 'LD_PRELOAD=${TMP_SPY_SO} ${TMP_DUMPER_BIN}'"
