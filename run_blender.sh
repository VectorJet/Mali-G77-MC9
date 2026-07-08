#!/bin/bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/virtio_icd.json
export VN_DEBUG=vtest
export VTEST_SOCKET_NAME=/tmp/local_tmp/vtest.sock
export GALLIUM_DRIVER=zink
export DISPLAY=:0
export MESA_GL_VERSION_OVERRIDE=4.3
export MESA_GLES_VERSION_OVERRIDE=3.2

echo "Running Blender with Zink via Venus Vulkan Bridge..."
blender "$@"
