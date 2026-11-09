#!/usr/bin/env bash
# Builds the Switch NRO: run in the MSYS2 bash of D:\msys64 (devkitPro's packages in /opt/devkitpro).
# TARGET=street2_host_check compiles only the host and runtime; JOBS lowers the parallel jobs if GCC runs out of memory.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"
# PGO=generate builds the instrumented NRO in build-switch-pgo; PGO=use builds build-switch with its profile
if [ "${PGO:-off}" = generate ]; then BUILD="${BUILD_DIR:-$ROOT/build-switch-pgo}"; else BUILD="${BUILD_DIR:-$ROOT/build-switch}"; fi
cmake -S "$HERE" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DSTREET2_VERBOSE_LOG=${VERBOSE:-OFF} -DSTREET2_PGO=${PGO:-off}
ninja -C "$BUILD" -j"${JOBS:-6}" ${TARGET:-street2_nro}
