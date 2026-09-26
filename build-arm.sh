#!/usr/bin/env bash
# Direct AArch64/Linux build for the R36S target. No Docker is used.
#
# Ubuntu/Debian host prerequisites:
#   gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake make
#   libsdl2-dev:arm64 libgles2-mesa-dev:arm64 libegl1-mesa-dev:arm64 zlib1g-dev:arm64
#
# Environment overrides:
#   SRC=... BUILD=... BUILD_TYPE=Release GE_BETA_RELEASE=ON GE_DEV_PROBES=OFF
#   SDL2_INCLUDE_DIR=... SDL2_LIBRARY=... ZLIB_INCLUDE_DIR=... ZLIB_LIBRARY=...
#   GL_LIBRARY=...
set -euo pipefail
cd "$(dirname "$0")"

SRC_DIR="${SRC:-work/goldeneye-pc-port}"
BUILD_DIR="${BUILD:-build/arm64}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BETA="${GE_BETA_RELEASE:-ON}"
PROBES="${GE_DEV_PROBES:-OFF}"

if [[ "${1:-}" == "clean" ]]; then
  rm -rf "$BUILD_DIR"
  echo "cleaned $BUILD_DIR"
  exit 0
fi

for tool in cmake aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ aarch64-linux-gnu-readelf; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "build-arm.sh: missing required tool: $tool" >&2
    exit 2
  }
done

MULTIARCH_LIB="${MULTIARCH_LIB:-/usr/lib/aarch64-linux-gnu}"
SDL2_INCLUDE_DIR="${SDL2_INCLUDE_DIR:-/usr/include/SDL2}"
SDL2_LIBRARY="${SDL2_LIBRARY:-$MULTIARCH_LIB/libSDL2.so}"
ZLIB_INCLUDE_DIR="${ZLIB_INCLUDE_DIR:-/usr/include}"
ZLIB_LIBRARY="${ZLIB_LIBRARY:-$MULTIARCH_LIB/libz.so}"
GL_LIBRARY="${GL_LIBRARY:-$MULTIARCH_LIB/libGLESv2.so}"

for path in "$SDL2_INCLUDE_DIR" "$SDL2_LIBRARY" "$ZLIB_LIBRARY" "$GL_LIBRARY"; do
  [[ -e "$path" ]] || {
    echo "build-arm.sh: target dependency missing: $path" >&2
    echo "Install the ARM64 SDL2/GLES/zlib development packages or override its path." >&2
    exit 3
  }
done

EXTRA_MULTIARCH_INCLUDE=""
if [[ -d /usr/include/aarch64-linux-gnu ]]; then
  EXTRA_MULTIARCH_INCLUDE="-I/usr/include/aarch64-linux-gnu"
fi

export PKG_CONFIG_LIBDIR="${PKG_CONFIG_LIBDIR:-$MULTIARCH_LIB/pkgconfig:/usr/share/pkgconfig}"
export PKG_CONFIG_SYSROOT_DIR="${PKG_CONFIG_SYSROOT_DIR:-/}"

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_ASM_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DROMID=ntsc-final \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DGE_BETA_RELEASE="$BETA" \
  -DGE_DEV_PROBES="$PROBES" \
  -DSDL2_INCLUDE_DIR="$SDL2_INCLUDE_DIR" \
  -DSDL2_LIBRARY="$SDL2_LIBRARY" \
  -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_DIR" \
  -DZLIB_LIBRARY="$ZLIB_LIBRARY" \
  -DGL_LIBRARY="$GL_LIBRARY" \
  -DCMAKE_C_FLAGS="-DUSE_GLES=1 $EXTRA_MULTIARCH_INCLUDE" \
  -DCMAKE_CXX_FLAGS="-DUSE_GLES=1 $EXTRA_MULTIARCH_INCLUDE"

cmake --build "$BUILD_DIR" -j"${JOBS:-$(nproc)}"

BIN="$BUILD_DIR/ge007.aarch64"
[[ -f "$BIN" ]] || { echo "build-arm.sh: expected output missing: $BIN" >&2; exit 4; }
aarch64-linux-gnu-readelf -h "$BIN" | grep -q "Machine:.*AArch64"
sha256sum "$BIN"
echo "built $BIN"
