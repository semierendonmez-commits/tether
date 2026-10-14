#!/bin/bash
# TETHER: offline build on macOS. Everything goes to build-mac.log next to this script.
#   bash ~/Downloads/tether-offline/build-mac.sh
cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
LOG="$ROOT/build-mac.log"
exec > >(tee "$LOG") 2>&1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

echo "== $(date)  $(uname -m)  macOS $(sw_vers -productVersion 2>/dev/null)"
echo "== tools"
xcode-select -p || { echo "NO XCODE TOOLS: run  xcode-select --install  then this script again"; exit 1; }
clang++ --version | head -1
if ! command -v cmake >/dev/null; then
    if command -v brew >/dev/null; then echo "cmake missing: installing with brew"; brew install cmake; fi
fi
command -v cmake >/dev/null || { echo "NO CMAKE: install it (brew install cmake, or cmake.org) then run this script again"; exit 1; }
cmake --version | head -1
ls "$ROOT"

echo "== configure"
cd "$ROOT/tether" || exit 1
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTTH_LTO=OFF \
      -DJUCE_DIR="$ROOT/JUCE" -DFETCHCONTENT_SOURCE_DIR_FAMILY_SDK="$ROOT/family-sdk" \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON || { echo "== CONFIGURE FAILED"; exit 1; }

echo "== build (plug-ins first, tests after)"
N=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build build --config Release --parallel "$N" --target Tether_AU Tether_VST3 Tether_Standalone || { echo "== PLUG-IN BUILD FAILED"; exit 1; }
cmake --build build --config Release --parallel "$N" || echo "== (tests did not build; the plug-ins did)"

echo "== result"
ls build/Tether_artefacts/Release/AU build/Tether_artefacts/Release/VST3 build/Tether_artefacts/Release/Standalone

echo "== install"
bash scripts/dev.sh install
echo "== DONE"
