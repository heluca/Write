#!/bin/bash
# Cross-compile static libcurl + mbedTLS for the Android NDK, for each ABI Write ships.
# Produces committed prebuilts under prebuilt/<abi>/{lib,include} consumed by the ndk-build
# Android.mk (see jni/curl). Run once when bumping versions; the .a/.h outputs are committed
# so normal app builds (local and CI) just link them.
#
# Usage:  ANDROID_NDK=~/Android/Sdk/ndk/27.0.12077973 ./build-curl.sh
set -euo pipefail

MBEDTLS_VER=v3.6.6
CURL_VER=curl-8_21_0
API=21   # minSdkVersion

NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}"
[ -z "$NDK" ] && { echo "set ANDROID_NDK to your NDK path"; exit 1; }
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/.curlbuild"
OUT="$HERE/prebuilt"
ABIS="${1:-arm64-v8a armeabi-v7a x86_64}"   # pass a single ABI as $1 to test one

mkdir -p "$WORK"

# --- fetch sources (pinned) ---
cd "$WORK"
[ -d mbedtls ] || git clone --depth 1 -b "$MBEDTLS_VER" https://github.com/Mbed-TLS/mbedtls
[ -d curl ] || git clone --depth 1 -b "$CURL_VER" https://github.com/curl/curl
# mbedtls needs its submodules (framework) for the 3.6 build
( cd mbedtls && git submodule update --init --depth 1 2>/dev/null || true )

# map ABI -> clang target triple used by the NDK toolchain
triple() {
  case "$1" in
    arm64-v8a)   echo aarch64-linux-android ;;
    armeabi-v7a) echo armv7a-linux-androideabi ;;
    x86_64)      echo x86_64-linux-android ;;
    x86)         echo i686-linux-android ;;
  esac
}

for ABI in $ABIS; do
  echo "=================  $ABI  ================="
  PREFIX="$WORK/out/$ABI"
  rm -rf "$PREFIX"; mkdir -p "$PREFIX"

  # --- mbedTLS (cmake + NDK toolchain) ---
  MB="$WORK/mbedtls/build-$ABI"
  rm -rf "$MB"; mkdir -p "$MB"
  cmake -S "$WORK/mbedtls" -B "$MB" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
  cmake --build "$MB" --target install -j"$(nproc)" >/dev/null
  echo "  mbedtls -> $(ls "$PREFIX"/lib/libmbed*.a | wc -l) libs"

  # --- curl (configure + make), mbedTLS backend, no extras we don't need ---
  TRIPLE="$(triple "$ABI")"
  export AR="$TOOLCHAIN/bin/llvm-ar"
  export AS="$TOOLCHAIN/bin/clang"
  export CC="$TOOLCHAIN/bin/${TRIPLE}${API}-clang"
  export CXX="$TOOLCHAIN/bin/${TRIPLE}${API}-clang++"
  export LD="$TOOLCHAIN/bin/ld"
  export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  export STRIP="$TOOLCHAIN/bin/llvm-strip"
  export CPPFLAGS="-I$PREFIX/include"
  export LDFLAGS="-L$PREFIX/lib"

  CB="$WORK/curl/build-$ABI"
  rm -rf "$CB"; mkdir -p "$CB"; cd "$CB"
  # autoreconf needed for a fresh git checkout
  [ -x "$WORK/curl/configure" ] || ( cd "$WORK/curl" && autoreconf -fi >/dev/null 2>&1 )
  "$WORK/curl/configure" \
    --host="$TRIPLE" \
    --prefix="$PREFIX" \
    --with-mbedtls="$PREFIX" \
    --enable-static --disable-shared \
    --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet \
    --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher \
    --disable-mqtt --disable-manual --disable-ntlm \
    --without-libpsl --without-zstd --without-brotli --without-zlib \
    >/dev/null
  make -j"$(nproc)" >/dev/null
  make install >/dev/null
  cd "$WORK"
  echo "  curl    -> $(ls "$PREFIX"/lib/libcurl.a)"

  # --- stage committed prebuilts: headers once, libs per ABI ---
  mkdir -p "$OUT/$ABI/lib"
  cp "$PREFIX"/lib/libcurl.a "$OUT/$ABI/lib/"
  cp "$PREFIX"/lib/libmbedtls.a "$PREFIX"/lib/libmbedx509.a "$PREFIX"/lib/libmbedcrypto.a "$OUT/$ABI/lib/"
  if [ ! -d "$OUT/include" ]; then
    mkdir -p "$OUT/include"
    cp -r "$PREFIX"/include/curl "$OUT/include/"
  fi
done

echo "Done. Prebuilts in $OUT (commit these)."
