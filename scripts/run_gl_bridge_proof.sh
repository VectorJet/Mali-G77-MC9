#!/usr/bin/env bash
set -euo pipefail

bash scripts/setup_android_vendor_links.sh

if ! ssh -p "${DEVICE_PORT:-8022}" "${DEVICE_USER:-u0_a371}@${DEVICE_HOST:-localhost}" \
    'su -c "test -S /data/local/tmp/mali_egl_bridge.sock"'; then
    bash scripts/run_mali_egl_bridge.sh >/dev/null
fi

bash scripts/build_gl_bridge_shim.sh >/dev/null
LD_PRELOAD=/tmp/libgl_mali_bridge.so /tmp/test_gl_bridge

echo
echo "Normal GLX status:"
DISPLAY="${DISPLAY:-:0}" glxinfo -B 2>/dev/null | grep -E 'OpenGL renderer string|Accelerated' || true
