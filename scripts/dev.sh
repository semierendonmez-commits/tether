#!/usr/bin/env bash
# TETHER development helper, same workflow as the sibling plug-ins.
#   scripts/dev.sh setup            configure (ccache used automatically, LTO off for speed)
#   scripts/dev.sh build            build VST3 / AU / Standalone + tests
#   scripts/dev.sh test             engine + mod + processor tests (lua5.3 or lua5.4 for the mod)
#   scripts/dev.sh shots [out-dir]  screenshots of the editor against the stand-in norns
#   scripts/dev.sh install          copy AU + VST3 into ~/Library/Audio/Plug-Ins (macOS), auval
#   scripts/dev.sh mod [host]       copy the tether mod to norns (default norns.local, user we)
#   scripts/dev.sh run              start the standalone app
#   scripts/dev.sh release          clean configure with LTO and build
set -eo pipefail
cd "$(dirname "$0")/.."
BUILD=build
ART="$BUILD/Tether_artefacts/Release"
export TETHER_TESTS_DIR="$PWD/tests"

install_plugins() {
    [ -d "$ART/VST3/TETHER.vst3" ] || { echo "No build in $ART: run 'scripts/dev.sh build' first."; exit 1; }
    mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
    rm -rf ~/Library/Audio/Plug-Ins/Components/TETHER.component ~/Library/Audio/Plug-Ins/VST3/TETHER.vst3
    [ -d "$ART/AU" ] && cp -R "$ART"/AU/*.component ~/Library/Audio/Plug-Ins/Components/
    cp -R "$ART"/VST3/*.vst3 ~/Library/Audio/Plug-Ins/VST3/
    for p in ~/Library/Audio/Plug-Ins/Components/TETHER.component ~/Library/Audio/Plug-Ins/VST3/TETHER.vst3; do
        codesign --force --deep -s - "$p" >/dev/null 2>&1 || true
    done
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval -v aumu Tthr Semi 2>/dev/null | tail -3 || true
    echo "Installed. Restart the DAW (or rescan plug-ins) to load the new build."
}

install_mod() {
    local host="${1:-norns.local}"
    echo "Copying the mod to we@$host:~/dust/code/tether (password: sleep)"
    ssh "we@$host" "mkdir -p ~/dust/code/tether/lib"
    scp norns/tether/lib/mod.lua "we@$host:~/dust/code/tether/lib/mod.lua"
    echo "On norns: SYSTEM > MODS > TETHER, E3 to enable (+), then SYSTEM > RESTART."
}

case "${1:-}" in
  setup)   command -v ccache >/dev/null || echo "Tip: 'brew install ccache' makes even full rebuilds fast."
           command -v lua5.4 >/dev/null || command -v lua5.3 >/dev/null || command -v lua >/dev/null || echo "Tip: 'brew install lua' runs the norns mod in the tests."
           [ -d .git ] || { git init -q && git add -A && git commit -qm "baseline"; }
           cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DTTH_LTO=OFF -DTTH_BUILD_TOOLS=ON ${JUCE_DIR:+-DJUCE_DIR=$JUCE_DIR} ;;
  build)   cmake --build "$BUILD" --config Release --parallel ;;
  test)    cmake --build "$BUILD" --config Release --target TetherEngineTests TetherProcessorTests --parallel
           "$BUILD/TetherEngineTests"
           (command -v lua5.4 || command -v lua5.3 || command -v lua) >/dev/null && "$(command -v lua5.4 || command -v lua5.3 || command -v lua)" tests/mod_test.lua
           "$BUILD/TetherProcessorTests_artefacts/Release/TetherProcessorTests" ;;
  shots)   cmake --build "$BUILD" --config Release --target TetherSnapshot --parallel
           "$BUILD/TetherSnapshot_artefacts/Release/TetherSnapshot" "${2:-screenshots}" ;;
  install) install_plugins ;;
  mod)     install_mod "${2:-}" ;;
  run)     open "$ART/Standalone/TETHER.app" 2>/dev/null || "$ART/Standalone/TETHER" ;;
  release) rm -rf "$BUILD" && cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DTTH_LTO=ON && cmake --build "$BUILD" --config Release --parallel ;;
  *)       sed -n '2,11p' "$0" ;;
esac
