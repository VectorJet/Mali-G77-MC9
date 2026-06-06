#!/usr/bin/env bash
set -euo pipefail

bash scripts/setup_android_vendor_links.sh

if ! ssh -p "${DEVICE_PORT:-8022}" "${DEVICE_USER:-u0_a371}@${DEVICE_HOST:-localhost}" \
    'su -c "test -S /data/local/tmp/mali_egl_bridge.sock"'; then
    bash scripts/run_mali_egl_bridge.sh >/dev/null
fi

bash scripts/build_gl_bridge_shim.sh >/dev/null
LD_LIBRARY_PATH=/tmp/mali-bridge-lib /tmp/test_gles_bridge

cat <<'MSG'

For experimental GLES apps, launch with:
  LD_LIBRARY_PATH=/tmp/mali-bridge-lib <your-gles-app>

Current shim coverage is still incomplete, but it now supports a realistic GLES2
textured triangle path: shader source/compile, program link/use, VBO upload,
sampler uniform, texture upload/parameters, vertex attrib setup, viewport,
draw arrays, finish, and RGBA readback through the Mali bridge.
MSG
