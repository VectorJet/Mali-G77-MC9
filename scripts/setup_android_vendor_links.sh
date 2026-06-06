#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    exec sudo -n "$0" "$@"
fi

for dir in system vendor apex odm; do
    dst="/${dir}"
    src="/proc/1/root/${dir}"

    if [[ ! -d "${src}" ]]; then
        echo "missing Android source: ${src}" >&2
        continue
    fi

    if [[ -L "${dst}" ]]; then
        current="$(readlink "${dst}")"
        if [[ "${current}" == "${src}" ]]; then
            echo "${dst} already points to ${src}"
            continue
        fi
        echo "${dst} is a symlink to ${current}; leaving it unchanged" >&2
        continue
    fi

    if [[ -e "${dst}" ]]; then
        if rmdir "${dst}" 2>/dev/null; then
            :
        else
            echo "${dst} exists and is not an empty directory; leaving it unchanged" >&2
            continue
        fi
    fi

    ln -s "${src}" "${dst}"
    echo "linked ${dst} -> ${src}"
done

ls -l /system/bin/linker64 /vendor/lib64/egl/mt6893/libGLES_mali.so /vendor/lib64/hw/mt6893/vulkan.mali.so /vendor/lib64/libgpud.so
