#!/usr/bin/env bash
set -euo pipefail

gcc -Wall -Wextra -O2 -fPIC -shared \
    -o /tmp/libgl_mali_bridge.so \
    src/kbase/chroot/libgl_mali_bridge.c \
    -ldl

mkdir -p /tmp/mali-bridge-lib
gcc -Wall -Wextra -O2 -fPIC -shared \
    -Wl,-soname,libGLESv2.so.2 \
    -o /tmp/mali-bridge-lib/libGLESv2.so.2 \
    src/kbase/chroot/libgl_mali_bridge.c \
    -ldl
ln -sf libGLESv2.so.2 /tmp/mali-bridge-lib/libGLESv2.so

gcc -Wall -Wextra -O2 -fPIC -shared \
    -Wl,-soname,libEGL.so.1 \
    -o /tmp/mali-bridge-lib/libEGL.so.1 \
    src/kbase/chroot/libegl_mali_bridge.c \
    -ldl -lX11 -lXext
ln -sf libEGL.so.1 /tmp/mali-bridge-lib/libEGL.so

gcc -Wall -Wextra -O2 \
    -o /tmp/test_gl_bridge \
    src/kbase/chroot/test_gl_bridge.c \
    -lGL

gcc -Wall -Wextra -O2 \
    -o /tmp/test_gles_bridge \
    src/kbase/chroot/test_gles_bridge.c \
    -L/tmp/mali-bridge-lib -Wl,-rpath,/tmp/mali-bridge-lib \
    -lEGL -lGLESv2

gcc -Wall -Wextra -O2 \
    -o /tmp/test_egl_window_bridge \
    src/kbase/chroot/test_egl_window_bridge.c \
    -L/tmp/mali-bridge-lib -Wl,-rpath,/tmp/mali-bridge-lib \
    -lEGL -lGLESv2 -lX11

echo "built /tmp/libgl_mali_bridge.so, /tmp/mali-bridge-lib, /tmp/test_gl_bridge, /tmp/test_gles_bridge, and /tmp/test_egl_window_bridge"
echo "run with: LD_PRELOAD=/tmp/libgl_mali_bridge.so /tmp/test_gl_bridge"
echo "run GLES shim with: LD_LIBRARY_PATH=/tmp/mali-bridge-lib /tmp/test_gles_bridge"
echo "run X11 window shim with: DISPLAY=:0 LD_LIBRARY_PATH=/tmp/mali-bridge-lib /tmp/test_egl_window_bridge"
