# CyberHack — web build

The CyberHack game, playable in a mobile browser. **Single source of truth:** the
game logic is the *exact* engine the Cardputer runs
(`../emulator/src/core/cyberhack_world.cpp`), compiled to WebAssembly. There is no
second JavaScript engine to keep in sync — `cyberhack_wasm.cpp` is only a thin glue
shell (drive the `Sim`, serialize state to JSON, reuse the engine's own
`legends_serialize`/`deserialize` for saves).

## Files
- `index.html` — the mobile UI (touch decision cards, ASCII net map, chronicle).
- `cyberhack_wasm.cpp` — thin `extern "C"` glue (no game logic).
- `build.sh` — compiles the engine + glue → `cyberhack.js` + `cyberhack.wasm`.
- `cyberhack.js` / `cyberhack.wasm` — build outputs (git-ignored; built in CI).

## Build locally
Needs [emsdk](https://emscripten.org/docs/getting_started/downloads.html) at
`~/emsdk` (or `emcc` on PATH):
```sh
./build.sh
```

## Preview locally
WASM must be served over HTTP (not `file://`):
```sh
python3 -m http.server -d . 8000
# open http://localhost:8000
```

## Deploy to GitHub Pages
The page deploys automatically via `.github/workflows/pages.yml`, which builds the
WASM from the engine in CI (so the live game can never drift from the firmware).
One-time: **repo Settings → Pages → Source: "GitHub Actions"**, then push to `main`.

## Shareable dives
The RNG is deterministic, so a run is a URL: `?cnet=<n>&seed=<n>` replays the exact
same dive. (Seeds are shareable web↔web; not yet bit-identical to the device — that
needs a 64-bit seed expansion in the JS RNG seeding.)
