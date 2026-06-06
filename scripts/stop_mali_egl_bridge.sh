#!/usr/bin/env bash
set -euo pipefail

DEVICE_HOST="${DEVICE_HOST:-localhost}"
DEVICE_PORT="${DEVICE_PORT:-8022}"
DEVICE_USER="${DEVICE_USER:-u0_a371}"
PID_REMOTE="/data/local/tmp/mali_egl_bridge.pid"

ssh -p "$DEVICE_PORT" "${DEVICE_USER}@${DEVICE_HOST}" \
    "su -c 'if [ -f ${PID_REMOTE} ]; then kill -9 \$(cat ${PID_REMOTE}) 2>/dev/null || true; rm -f ${PID_REMOTE}; fi; for p in \$(pidof mali_egl_bridge_server 2>/dev/null); do kill -9 \$p 2>/dev/null || true; done; rm -f /data/local/tmp/mali_egl_bridge.sock'"
