# tinywfc

Native Rust tiled wave function collapse on 2D and 3D grids. No dependencies, no C++ toolchain, and no unsafe code. MIT licensed. Requires Rust 1.85 or newer.

```rust
use tinywfc::{Rules, Settings, Size, Solver};

let mut rules = Rules::new(2)?;
rules.allow_everywhere(0, 1)?;
let mut solver = Solver::new(&rules, Settings {
    size: Size::new(16, 1, 16),
    seed: 42,
    ..Settings::default()
})?;
solver.run()?;
let tiles = solver.result().unwrap();
assert_eq!(tiles.len(), 256);
# Ok::<(), tinywfc::Error>(())
```

## Use

```toml
[dependencies]
tinywfc = "0.1"
```

The crate lives alongside the C++ implementation in `rust/`. To use a local checkout, replace the version with `{ path = "/path/to/tinywfc/rust" }`.

[crates.io](https://crates.io/crates/tinywfc) · [API documentation](https://docs.rs/tinywfc)

## Rules and solving

- `Rules::new(tile_count)` creates tiles with weight 1 and no allowed neighbors. `allow(a, direction, b)` also enables the reciprocal pair. `set_weight` accepts positive finite `f32` weights.
- `SocketTile` and `rules_from_sockets` build adjacency from equal touching sockets. `rotate_y` rotates the four horizontal faces; vertical faces stay fixed.
- `restrict(coordinate, tiles)` and `ban(coordinate, tile)` constrain a cell. Changes before the first step survive restarts; later changes may be undone by backtracking.
- `set_priority` biases observation order. `step()` returns `Ok(true)` while work remains, `Ok(false)` when complete, or an error when the attempt fails. `run()` also tries restarts.
- `tile`, `is_possible`, `possibility_count`, `collapsed_count` and `result` expose state. `result()` returns `None` until every cell has one tile.

Cells are stored x-fastest, then z, then y. Y is up. An axis of length 1 has no neighbors along that axis, including on periodic grids.

The port retains packed domains, grouped AC-4 support counters, a lazy entropy heap, a removal trail, exponential backjumping, and xoshiro256** random numbers. Clones copy search state and share immutable tables. Native parallel restarts select the lowest successful attempt, so changing the worker count does not change the result. `threads = 0` chooses available CPUs. WebAssembly runs serially through the same API.

Collapse/reset callbacks accept `Send + Sync` closures. Worker attempts do not call callbacks; inspect the completed solver after parallel `run()`. Closures are shared between solver clones. Reset callbacks indicate that cells previously reported as collapsed may have changed.

Rules and coordinates are checked through `Result`. A contradiction may mean the search budget was exhausted; it does not establish that the rules are unsatisfiable. The overlapping/image-learning model is not included.

## Check and run

Run from `rust/`:

```sh
cargo test
cargo run --example checkerboard
cargo clippy --all-targets -- -D warnings
cargo package --allow-dirty
```

Tests cover 2D/3D periodic grids, sockets, constraints, backtracking, impossible rules, priorities, callbacks, and serial/parallel agreement. C++ reference fixtures compare seeded results and winning-attempt statistics. To regenerate them, run `python tools/generate_reference.py` with a C++23 compiler (`CXX` selects it). `--check` verifies stored samples on the same toolchain. Normal Cargo builds do not need C++.

CI checks native platforms, Rust 1.85, WebAssembly, packaging and freshly generated C++ reference samples.
