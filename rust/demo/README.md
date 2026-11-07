# tinywfc interactive Rust demo

[Open the browser demo](https://varnholt.github.io/tinywfc/rust/)

![tinywfc Rust demo](screenshot.png)

- The C++ city socket designs: streets, junctions, canals, bridges, parks, buildings, three roof types, ramps and elevated roads.
- Seed and grid controls, incremental playback, pause/step, progress and search statistics.
- Layer-by-layer constraints, shaded procedural city geometry, orbit/pan/zoom and auto orbit.
- Incremental solving uses a per-frame time budget and a smaller per-attempt backtracking budget to keep the interface responsive.

## Run natively

The demo uses Rust **1.95** and eframe 0.36.2. The library still supports Rust 1.85 and has no rendering dependencies. This is a separate, unpublished Cargo package; it does not add dependencies to the published crate.

From the repository root:

```sh
rustup toolchain install 1.95
cargo +1.95 run --release --manifest-path rust/demo/Cargo.toml
```

Linux also needs windowing libraries, for example `libxkbcommon-dev`, `libwayland-dev`, `libx11-dev`, `libgl1-mesa-dev` and `libegl1-mesa-dev` on Ubuntu. Rendering uses OpenGL on native platforms and WebGL 2 in browsers.

## Build for the browser

```sh
rustup target add wasm32-unknown-unknown --toolchain 1.95
cargo +1.95 install wasm-bindgen-cli --version 0.2.130 --locked
python rust/demo/tools/build_web.py
python -m http.server 8080 --directory rust/demo/dist
```

Open `http://localhost:8080/`. The generated `dist/` directory is a standalone static site. The binding generator version is pinned to match the Wasm dependency. Graphics initialization failures are shown on the loading screen.

## Verify

```sh
cargo +1.95 fmt --manifest-path rust/demo/Cargo.toml --check
cargo +1.95 clippy --manifest-path rust/demo/Cargo.toml --locked --all-targets -- -D warnings
cargo +1.95 test --manifest-path rust/demo/Cargo.toml --locked
```

CI builds and tests native demos on Linux, Windows and macOS, builds the browser application, and exercises rendered scenes and pointer input in headless Chromium. Browser screenshots are uploaded as CI artifacts. Core library tests remain independent of the demo.

For browser smoke checks, build `dist/`, run `npm install --no-save --package-lock=false playwright@1.65.0` and `npx playwright install chromium` from `rust/demo/`, then run `node tools/smoke_web.mjs`. The script starts a local server; `WEB_DEMO_URL` can point it at an already running copy. `CHROMIUM_PATH` can select an installed Chromium executable.

Camera controls: drag to orbit, right-drag to pan, scroll to zoom.

Use `--seed 42 --complete 1` for a completed city or `--paused 1` to start paused. Browser equivalents: `?seed=42&complete=1`.

For reproducible native captures, build with `--features screenshot` and set `EFRAME_SCREENSHOT_TO=out.png`; the application saves a frame and exits.
