#!/usr/bin/env bash
# Build bundled SDL3 3.4.10 from source as a static library for the Win64
# MinGW-w64 frontend.
#
# The native Win32/Win64 PCFXEmu frontend (D3D11/GDI video, WASAPI/waveOut
# audio) keeps its own window and audio code; SDL3 is used only for gamepad /
# joystick input on the Win64 build, mirroring how GP32emu ships SDL3. SDL3 is
# only built for the 64-bit target -- the legacy 32-bit (pcfx-winxp.exe) build
# stays keyboard + XInput only.
#
# Usage:
#   scripts/build_sdl3_mingw.sh win64
set -euo pipefail

ARCH="${1:-win64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
URL="${SDL3_URL:-https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz}"
TARBALL="$ROOT/third_party/sdl/SDL3-3.4.10.tar.gz"
SRC="$ROOT/third_party/sdl/SDL3-3.4.10"
BUILD="$ROOT/third_party/sdl/build/$ARCH"
PREFIX="$ROOT/third_party/sdl/install/$ARCH"

case "$ARCH" in
  win64)
    CC=${CC:-x86_64-w64-mingw32-gcc}
    CXX=${CXX:-x86_64-w64-mingw32-g++}
    RC=${RC:-x86_64-w64-mingw32-windres}
    ;;
  *)
    echo "usage: $0 win64" >&2
    echo "SDL3 input is only provided for the 64-bit PCFXEmu frontend." >&2
    exit 2
    ;;
esac

mkdir -p "$ROOT/third_party/sdl"
if [ ! -f "$TARBALL" ]; then
  echo "Downloading SDL3 source: $URL"
  if command -v curl >/dev/null 2>&1; then curl -fL "$URL" -o "$TARBALL"
  elif command -v wget >/dev/null 2>&1; then wget "$URL" -O "$TARBALL"
  else echo "Need curl or wget to fetch SDL3. Place SDL3-3.4.10.tar.gz in third_party/sdl/." >&2; exit 1
  fi
fi

if [ ! -d "$SRC" ]; then
  rm -rf "$SRC.tmp"
  mkdir -p "$SRC.tmp"
  tar -xf "$TARBALL" -C "$SRC.tmp" --strip-components=1
  mv "$SRC.tmp" "$SRC"
fi

command -v cmake >/dev/null 2>&1 || { echo "cmake is required to build SDL3" >&2; exit 1; }
command -v "$CC" >/dev/null 2>&1 || { echo "missing compiler: $CC" >&2; exit 1; }
command -v "$CXX" >/dev/null 2>&1 || { echo "missing compiler: $CXX" >&2; exit 1; }
command -v "$RC" >/dev/null 2>&1 || { echo "missing resource compiler: $RC" >&2; exit 1; }

# CMake records absolute source/build paths in CMakeCache.txt. A bundled SDL
# build cache copied or moved from an older tree must be discarded, otherwise
# CMake refuses to configure with "source does not match" errors.
canon_path() {
  if [ -e "$1" ]; then
    (cd "$1" 2>/dev/null && pwd -P) || printf '%s\n' "$1"
  else
    printf '%s\n' "$1"
  fi
}

if [ -f "$BUILD/CMakeCache.txt" ]; then
  WANT_SRC="$(canon_path "$SRC")"
  WANT_BUILD="$(canon_path "$BUILD")"
  CACHE_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tail -n 1)"
  CACHE_BUILD="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tail -n 1)"
  CACHE_SRC_CANON="$(canon_path "${CACHE_SRC:-__missing__}")"
  CACHE_BUILD_CANON="$(canon_path "${CACHE_BUILD:-__missing__}")"
  if [ "$CACHE_SRC_CANON" != "$WANT_SRC" ] || [ "$CACHE_BUILD_CANON" != "$WANT_BUILD" ]; then
    echo "Discarding stale SDL3 CMake cache for previous source tree:" >&2
    echo "  cached source: ${CACHE_SRC:-unknown}" >&2
    echo "  current source: $WANT_SRC" >&2
    rm -rf "$BUILD"
  fi
fi

TOOLCHAIN="$BUILD/mingw-toolchain.cmake"
mkdir -p "$BUILD"
cat > "$TOOLCHAIN" <<TC
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER $RC)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TC

cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF \
  -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF
cmake --build "$BUILD" --parallel "${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
cmake --install "$BUILD"

echo "SDL3 installed to $PREFIX"
