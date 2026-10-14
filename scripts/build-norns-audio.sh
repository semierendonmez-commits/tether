#!/usr/bin/env bash
# Builds tether-audio for norns (32-bit ARM, as every norns image so far, and 64-bit ARM) with zig,
# against glibc 2.28 (norns on buster; bullseye has 2.31): the binaries run on any norns image and
# need nothing installed there but JACK, which norns has. libc++ is linked in statically.
#   pip install ziglang       (once)
#   scripts/build-norns-audio.sh
set -eo pipefail
cd "$(dirname "$0")/.."
ZIG="python3 -m ziglang"
$ZIG version >/dev/null 2>&1 || { echo "zig not found: pip install ziglang"; exit 1; }
SRC=norns/tether-audio/tether_audio.cpp
OUT=norns/tether/bin
mkdir -p "$OUT"
FLAGS="-std=c++17 -O2 -fno-exceptions -I Source -s -ldl -lpthread"
$ZIG c++ -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 $FLAGS "$SRC" -o "$OUT/tether-audio-armhf"
$ZIG c++ -target aarch64-linux-gnu.2.28 $FLAGS "$SRC" -o "$OUT/tether-audio-arm64"
ls -l "$OUT"
