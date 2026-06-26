#!/usr/bin/env bash
# Build the CyberHack web app from the SAME engine the Cardputer runs.
# Single source of truth: this compiles emulator/src/core/cyberhack_world.cpp
# (unchanged) + the thin wasm glue. Outputs cyberhack.js + cyberhack.wasm —
# static files; drop the web/ folder on GitHub Pages and it's live.
set -e
cd "$(dirname "$0")"
# Local: source emsdk. CI (setup-emsdk action): emcc is already on PATH.
# shellcheck disable=SC1090
[ -f "$HOME/emsdk/emsdk_env.sh" ] && source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true

emcc -O2 -I ../emulator/src \
  ../emulator/src/core/cyberhack_world.cpp cyberhack_wasm.cpp \
  -o cyberhack.js \
  -sEXPORTED_FUNCTIONS=_ch_start,_ch_advance,_ch_choose,_ch_running,_ch_state,_ch_chronicle,_ch_legends,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString \
  -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=CyberHackModule

echo "built: $(ls -la cyberhack.js cyberhack.wasm | awk '{print $5, $9}')"
