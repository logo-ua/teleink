#!/usr/bin/env bash
# Prepares everything needed to build Teleink on Linux x86_64:
#   - the PocketBook SDK 6.5 files we link against (InkView headers + libinkview.so, zlib)
#   - Zig 0.14.1 as the ARM cross compiler (TDLib needs C++17, the SDK's GCC 6.3 is too old)
#   - OpenSSL 3.0 and TDLib, built as static libraries for ARMv7 / glibc 2.23
# Everything goes into ./deps (override with DEPS=...). Steps already done are skipped.
#
# Host requirements: git, cmake, make, gperf, perl, python3 with pip, a C++ compiler.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${DEPS:-$ROOT/deps}"
JOBS="${JOBS:-$(nproc)}"

SDK_REPO="https://github.com/pocketbook/SDK_6.3.0"
SDK_BRANCH="6.5"
ZIG_VERSION="0.14.1"
OPENSSL_TAG="openssl-3.0.22"
TDLIB_REPO="https://github.com/tdlib/td"
TDLIB_COMMIT="03220f6b3043c6fd7785fbf0901d63f19ad19974"  # TDLib 1.8.70

SYSROOT_REL="SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot"
PREFIX="$DEPS/prefix"   # ARM headers and static libraries end up here
TC="$DEPS/toolchain"

mkdir -p "$DEPS" "$PREFIX/lib" "$PREFIX/include" "$TC"
log() { printf '\n==> %s\n' "$*"; }

# ---------------------------------------------------------------- PocketBook SDK
if [ ! -f "$DEPS/sdk/.done" ]; then
  log "Fetching PocketBook SDK files (branch $SDK_BRANCH)"
  rm -rf "$DEPS/sdk"
  # Only a handful of files are needed out of ~2.6 GB, so use a sparse, blob-less clone when possible.
  if git clone --depth 1 --filter=blob:none --no-checkout -b "$SDK_BRANCH" "$SDK_REPO" "$DEPS/sdk"; then
    git -C "$DEPS/sdk" sparse-checkout set --no-cone \
      "/$SYSROOT_REL/usr/local/include/" \
      "/$SYSROOT_REL/usr/local/lib/libinkview.so" \
      "/$SYSROOT_REL/usr/include/zlib.h" \
      "/$SYSROOT_REL/usr/include/zconf.h" \
      "/$SYSROOT_REL/usr/lib/libz.a"
    git -C "$DEPS/sdk" checkout "$SDK_BRANCH"
  else
    rm -rf "$DEPS/sdk"
    git clone --depth 1 -b "$SDK_BRANCH" "$SDK_REPO" "$DEPS/sdk"
  fi
  test -f "$DEPS/sdk/$SYSROOT_REL/usr/local/include/inkview.h"
  test -f "$DEPS/sdk/$SYSROOT_REL/usr/local/lib/libinkview.so"
  touch "$DEPS/sdk/.done"
fi
SYSROOT="$DEPS/sdk/$SYSROOT_REL"

# zlib from the SDK (the device ships the same version)
mkdir -p "$TC/inc"
cp "$SYSROOT/usr/include/zlib.h" "$SYSROOT/usr/include/zconf.h" "$TC/inc/"
cp "$SYSROOT/usr/include/zlib.h" "$SYSROOT/usr/include/zconf.h" "$PREFIX/include/"
cp "$SYSROOT/usr/lib/libz.a" "$PREFIX/lib/"

# ---------------------------------------------------------------- Zig toolchain
if [ ! -x "$DEPS/zig/ziglang/zig" ]; then
  log "Installing Zig $ZIG_VERSION"
  python3 -m pip install --quiet --target "$DEPS/zig" "ziglang==$ZIG_VERSION" ||
    python3 -m pip install --quiet --break-system-packages --target "$DEPS/zig" "ziglang==$ZIG_VERSION"
fi
ZIG="$DEPS/zig/ziglang/zig"

# Target: ARMv7 Cortex-A7, soft-float calling convention (as the firmware), glibc 2.23.
for tool in cc c++; do
  cat > "$TC/zig-$tool" <<EOF
#!/usr/bin/env bash
args=()
for a in "\$@"; do
  case "\$a" in
    -Wl,--exclude-libs,ALL) ;;   # not supported by zig's linker driver
    *) args+=("\$a") ;;
  esac
done
exec "$ZIG" $tool -target arm-linux-gnueabi.2.23 -mcpu=cortex_a7 "\${args[@]}"
EOF
done
printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$TC/zig-ar"
printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$TC/zig-ranlib"
chmod +x "$TC"/zig-*

cat > "$TC/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER $TC/zig-cc)
set(CMAKE_CXX_COMPILER $TC/zig-c++)
set(CMAKE_AR $TC/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB $TC/zig-ranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH $PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

# ---------------------------------------------------------------- OpenSSL
if [ ! -f "$PREFIX/lib/libssl.a" ]; then
  log "Building $OPENSSL_TAG"
  rm -rf "$DEPS/openssl-src"
  git -c advice.detachedHead=false clone --depth 1 -b "$OPENSSL_TAG" https://github.com/openssl/openssl "$DEPS/openssl-src"
  (
    cd "$DEPS/openssl-src"
    ./Configure linux-armv4 no-shared no-tests no-engine no-dso no-async --prefix="$PREFIX" --libdir=lib \
      CC="$TC/zig-cc" AR="$TC/zig-ar" RANLIB="$TC/zig-ranlib" -O2
    make -j"$JOBS" build_libs
    make install_dev
  )
  rm -rf "$DEPS/openssl-src"
fi

# ---------------------------------------------------------------- TDLib
if [ ! -f "$PREFIX/lib/libtdcore.a" ]; then
  log "Building TDLib ($TDLIB_COMMIT) - this takes a while"
  TD="$DEPS/td"
  if [ ! -d "$TD/.git" ]; then
    rm -rf "$TD"
    git init -q "$TD"
    git -C "$TD" remote add origin "$TDLIB_REPO"
    git -C "$TD" fetch --depth 1 origin "$TDLIB_COMMIT"
    git -C "$TD" -c advice.detachedHead=false checkout FETCH_HEAD
  fi
  # 1. host build that generates the TL sources
  cmake -S "$TD" -B "$TD/build-native" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$TD/build-native" --target prepare_cross_compiling -j"$JOBS"
  # 2. cross build of the static libraries
  cmake -S "$TD" -B "$TD/build-arm" -DCMAKE_TOOLCHAIN_FILE="$TC/toolchain.cmake" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DOPENSSL_ROOT_DIR="$PREFIX" -DOPENSSL_USE_STATIC_LIBS=ON -DZLIB_ROOT="$PREFIX" -DZLIB_USE_STATIC_LIBS=ON \
    -DTD_ENABLE_LTO=OFF
  cmake --build "$TD/build-arm" --target tdclient -j"$JOBS"
  # 3. collect libraries and the headers the client needs, then drop the (large) build trees
  B="$TD/build-arm"
  cp "$B/libtdclient.a" "$B/libtdcore.a" "$B/libtdapi.a" "$B/libtdmtproto.a" "$B/tde2e/libtde2e.a" \
     "$B/tdnet/libtdnet.a" "$B/tddb/libtddb.a" "$B/sqlite/libtdsqlite.a" "$B/tdactor/libtdactor.a" \
     "$B/tdutils/libtdutils.a" "$PREFIX/lib/"
  (cd "$TD" && find td/telegram -maxdepth 1 -name 'Client.h' -exec cp --parents {} "$PREFIX/include/" \;)
  (cd "$TD/td/generate/auto" && cp --parents td/telegram/td_api.h td/telegram/td_api.hpp "$PREFIX/include/")
  (cd "$TD" && find td/tl -name '*.h' -exec cp --parents {} "$PREFIX/include/" \;)
  (cd "$TD/tdutils" && find td -name '*.h' -exec cp --parents {} "$PREFIX/include/" \;)
  (cd "$B/tdutils" && find td -name '*.h' -exec cp --parents {} "$PREFIX/include/" \;)
  rm -rf "$TD"
fi

log "Dependencies are ready in $DEPS"
