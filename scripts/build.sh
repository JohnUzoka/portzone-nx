#!/bin/bash
# Builds PortZone in Docker, so nothing is installed on this machine.
#   scripts/build.sh desktop          -> build/portzone            (Linux; runs under WSLg)
#   scripts/build.sh switch           -> build_switch/portzone.nro
#   scripts/build.sh switch-emulator  -> build_switch_emu/portzone.nro: for Ryujinx without firmware (no system
#                                        fonts); test builds only, never release one
set -euo pipefail
cd "$(dirname "$0")/.."

# borealis is a submodule (with its own glfw submodule), plus PortZone's patch: once, after a fresh clone.
[ -f library/borealis/CMakeLists.txt ] || git submodule update --init --recursive
git -C library/borealis apply --reverse --check ../../patches/borealis.patch 2>/dev/null ||
    git -C library/borealis apply ../../patches/borealis.patch

case "${1:-desktop}" in
    desktop)
        docker image inspect portzone-desktop-build >/dev/null 2>&1 ||
            docker build -t portzone-desktop-build -f docker/desktop.Dockerfile docker
        docker run --rm -v "$PWD":/work -w /work portzone-desktop-build bash -c \
            'cmake -B build -G Ninja -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release >/dev/null && ninja -C build'
        ;;
    switch)
        docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64 bash -c \
            'cmake -B build_switch -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release >/dev/null && make -C build_switch -j"$(nproc)" portzone.nro'
        ;;
    switch-emulator)
        docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64 bash -c \
            'cmake -B build_switch_emu -DPLATFORM_SWITCH=ON -DPORTZONE_EMULATOR_BUILD=ON -DCMAKE_BUILD_TYPE=Release >/dev/null && make -C build_switch_emu -j"$(nproc)" portzone.nro'
        ;;
    *)
        echo "usage: $0 [desktop|switch|switch-emulator]" >&2
        exit 1
        ;;
esac
