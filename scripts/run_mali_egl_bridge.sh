#!/usr/bin/env bash
set -euo pipefail

DEVICE_HOST="${DEVICE_HOST:-localhost}"
DEVICE_PORT="${DEVICE_PORT:-8022}"
DEVICE_USER="${DEVICE_USER:-u0_a371}"

SRC_LOCAL="src/kbase/tools/mali_egl_bridge_server.c"
SRC_REMOTE="/data/data/com.termux/files/home/mali_egl_bridge_server.c"
BIN_REMOTE="/data/local/tmp/mali_egl_bridge_server"
TERMUX_CLANG="/data/data/com.termux/files/usr/bin/clang"
LOG_REMOTE="/data/local/tmp/mali_egl_bridge.log"
PID_REMOTE="/data/local/tmp/mali_egl_bridge.pid"

scp -P "$DEVICE_PORT" "$SRC_LOCAL" "${DEVICE_USER}@${DEVICE_HOST}:${SRC_REMOTE}"

ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c '${TERMUX_CLANG} -Wall -Wextra -O2 -o ${BIN_REMOTE} ${SRC_REMOTE} -ldl -llog && chmod 755 ${BIN_REMOTE}'"

ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c 'if [ -f ${PID_REMOTE} ]; then kill -9 \$(cat ${PID_REMOTE}) 2>/dev/null || true; fi; for p in \$(pidof mali_egl_bridge_server 2>/dev/null); do kill -9 \$p 2>/dev/null || true; done; rm -f /data/local/tmp/mali_egl_bridge.sock ${LOG_REMOTE}; nohup ${BIN_REMOTE} > ${LOG_REMOTE} 2>&1 & echo \$! > ${PID_REMOTE}; sleep 1; cat ${LOG_REMOTE}'"

gcc -Wall -Wextra -O2 -o /tmp/mali_egl_bridge_client src/kbase/chroot/mali_egl_bridge_client.c
/tmp/mali_egl_bridge_client 0.25 0.50 0.75 1.0
