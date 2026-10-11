#!/bin/sh
# Builds build/linux/sh4 (x86-64 Linux) inside the sh4nx-linux container (tools/Dockerfile.linux).
# Run from the repository root, or set ROOT. MSYS_NO_PATHCONV=1 keeps Git Bash from mangling /src.
set -e
ROOT=${ROOT:-$PWD}
export MSYS_NO_PATHCONV=1
docker image inspect sh4nx-linux >/dev/null 2>&1 || docker build -t sh4nx-linux -f "$ROOT/tools/Dockerfile.linux" "$ROOT/tools"
docker run --rm -v "$ROOT:/src" -w /src sh4nx-linux sh -c '
  cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null &&
  cmake --build build/linux -j "$(nproc)"'
ls -la "$ROOT/build/linux/sh4"
