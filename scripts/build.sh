#!/usr/bin/env bash
# Builds "Teleink.app" and the release zip. Run scripts/setup.sh first.
#
# Optional environment:
#   TG_API_ID / TG_API_HASH  built-in Telegram API keys (users then don't have to enter their own)
#   VERSION                  version string for the zip name (default: dev)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${DEPS:-$ROOT/deps}"
OUT="${OUT:-$ROOT/out}"
VERSION="${VERSION:-dev}"
APP_NAME="Teleink"

TC="$DEPS/toolchain"
PREFIX="$DEPS/prefix"
SYSROOT="$DEPS/sdk/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot"
[ -x "$TC/zig-c++" ] && [ -f "$PREFIX/lib/libtdcore.a" ] || { echo "Run scripts/setup.sh first" >&2; exit 1; }

DEFS=()
if [ -n "${TG_API_ID:-}" ] && [ -n "${TG_API_HASH:-}" ]; then
  DEFS+=("-DTG_API_ID=${TG_API_ID}" "-DTG_API_HASH=\"${TG_API_HASH}\"")
  echo "Building with built-in API keys"
fi

mkdir -p "$OUT/obj"
"$TC/zig-c++" -std=c++17 -fno-rtti -Os -ffunction-sections -fdata-sections -Wall -Wno-unused-function \
  "${DEFS[@]}" -I"$SYSROOT/usr/local/include" -I"$TC/inc" -I"$PREFIX/include" \
  -c "$ROOT/src/main.cpp" -o "$OUT/obj/main.o"

L="$PREFIX/lib"
"$TC/zig-c++" -o "$OUT/$APP_NAME.app" "$OUT/obj/main.o" \
  "$L/libtdclient.a" "$L/libtdcore.a" "$L/libtdapi.a" "$L/libtde2e.a" "$L/libtdmtproto.a" "$L/libtdnet.a" \
  "$L/libtddb.a" "$L/libtdsqlite.a" "$L/libtdactor.a" "$L/libtdutils.a" \
  "$L/libssl.a" "$L/libcrypto.a" "$L/libz.a" \
  "$SYSROOT/usr/local/lib/libinkview.so" \
  -lpthread -ldl -lm -Wl,--gc-sections -s

# release zip: copy its contents to the root of the reader's memory
PKG="$OUT/pkg"
rm -rf "$PKG"
mkdir -p "$PKG/applications"
cp "$OUT/$APP_NAME.app" "$PKG/applications/"
cp "$ROOT/packaging/README.txt" "$PKG/README.txt"
# the key template is only useful when the build has no keys of its own
[ ${#DEFS[@]} -eq 0 ] && cp "$ROOT/packaging/teleink-api.txt" "$PKG/teleink-api.txt"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$PKG/THIRD_PARTY_NOTICES.md"
ZIP="$OUT/Teleink-$VERSION.zip"
rm -f "$ZIP"
(cd "$PKG" && zip -qr -9 "$ZIP" .)
ls -la "$OUT/$APP_NAME.app" "$ZIP"
