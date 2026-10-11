#!/bin/sh
# Builds build/switch/sh4-nx.nro inside the devkitPro container (docker or podman, no local install needed).
# Run from the repository root, or set ROOT.
set -e
ROOT=${ROOT:-$PWD}
ENGINE=${ENGINE:-$(command -v podman >/dev/null 2>&1 && echo podman || echo docker)}
"$ENGINE" run --rm -v "$ROOT:/src" -w /src docker.io/devkitpro/devkita64:latest sh -c '
  cmake -S . -B build/switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build build/switch -j "$(nproc)"'
ls -la "$ROOT/build/switch/sh4-nx.nro"
