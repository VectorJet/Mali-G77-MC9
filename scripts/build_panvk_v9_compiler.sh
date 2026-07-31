#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MESA_SOURCE=${MESA_SOURCE:-"${ROOT}/refs/mesa-Panfork-android"}
BUILD_DIR=${BUILD_DIR:-"${TMPDIR:-/tmp}/panvk-v9-mesa-build"}
OUTPUT=${OUTPUT:-"${ROOT}/src/vulkan/libpanvk_v9_compiler.so"}

if [[ ! -f "${MESA_SOURCE}/meson.build" ]]; then
    echo "Mesa source not found at ${MESA_SOURCE}" >&2
    exit 1
fi

for tool in meson ninja python3 bison flex; do
    command -v "${tool}" >/dev/null || {
        echo "Missing build dependency: ${tool}" >&2
        exit 1
    }
done

if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    meson setup "${BUILD_DIR}" "${MESA_SOURCE}" \
        -Dgallium-drivers=panfrost \
        -Dvulkan-drivers=[] \
        -Dplatforms=[] \
        -Dglx=disabled \
        -Degl=disabled \
        -Dgbm=disabled \
        -Dllvm=disabled \
        -Dshared-glapi=disabled \
        -Dbuild-tests=false \
        -Dtools=panfrost \
        -Dlibunwind=disabled \
        -Dvalgrind=disabled
fi

ninja -C "${BUILD_DIR}" src/panfrost/bifrost_compiler

ROOT="${ROOT}" BUILD_DIR="${BUILD_DIR}" python3 <<'PY'
import json
import os
import shlex
import subprocess

root = os.environ["ROOT"]
build = os.environ["BUILD_DIR"]
commands = json.loads(subprocess.check_output(
    ["ninja", "-C", build, "-t", "compdb"], text=True))
template = next(c for c in commands if c["file"].endswith("bifrost/cmdline.c"))
command = shlex.split(template["command"])
command[command.index("-o") + 1] = os.path.join(build, "panvk_v9_compiler_mesa.o")
source_index = command.index("-c") + 1
command[source_index] = os.path.join(root, "src/vulkan/panvk_v9_compiler_mesa.c")
command.insert(source_index, "-I" + os.path.join(root, "src/vulkan"))
subprocess.run(command, cwd=build, check=True)
PY

mkdir -p "$(dirname "${OUTPUT}")"
c++ -shared -static-libstdc++ -static-libgcc -o "${OUTPUT}" "${BUILD_DIR}/panvk_v9_compiler_mesa.o" \
    -Wl,--no-undefined -Wl,--start-group \
    "${BUILD_DIR}/src/compiler/nir/libnir.a" \
    "${BUILD_DIR}/src/compiler/libcompiler.a" \
    "${BUILD_DIR}/src/util/libmesa_util.a" \
    "${BUILD_DIR}/src/util/format/libmesa_format.a" \
    "${BUILD_DIR}/src/util/libmesa_util_sse41.a" \
    "${BUILD_DIR}/src/c11/impl/libmesa_util_c11.a" \
    "${BUILD_DIR}/src/panfrost/bifrost/libpanfrost_bifrost.a" \
    "${BUILD_DIR}/src/panfrost/util/libpanfrost_util.a" \
    "${BUILD_DIR}/src/panfrost/bifrost/libpanfrost_bifrost_disasm.a" \
    "${BUILD_DIR}/src/panfrost/bifrost/valhall/libpanfrost_valhall_disasm.a" \
    -pthread -lm -lz -Wl,--end-group

echo "Built ${OUTPUT}"
