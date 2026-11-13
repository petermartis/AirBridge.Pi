#!/bin/sh
# Run inside a Linux environment containing the matching OpenWrt SDK.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${OPENWRT_SDK:?Set OPENWRT_SDK to the extracted matching SDK directory}"
set -- "$OPENWRT_SDK"/staging_dir/toolchain-aarch64_cortex-a76_gcc-*_musl/bin/aarch64-openwrt-linux-musl-gcc
if [ "$#" -ne 1 ] || [ ! -x "$1" ]; then
    printf '%s\n' "Expected exactly one aarch64_cortex-a76 musl compiler in OPENWRT_SDK" >&2
    exit 1
fi
compiler=$1
export STAGING_DIR="$OPENWRT_SDK/staging_dir"
mkdir -p "$here/build"
"$compiler" -Os -std=c11 -Wall -Wextra -Werror \
    -o "$here/build/airbridge-lcd" "$here/lcd.c"
printf 'ARM64 output: %s\n' "$here/build/airbridge-lcd"
