# Driftwood: notes for working on this repo

C++20 colony sim on raylib. See README.md for the player-facing overview.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sim_tests                       # headless simulation tests, must pass
./build/game_tests                      # renderer geometry (projection, picking), no window, must pass
```

`ctest --test-dir build` runs both, as CI does.

Sim-only build (no raylib, fast): `cmake -S . -B build-sim -DDRIFTWOOD_BUILD_GAME=OFF`.

## Checking visuals without a display

The game has a capture mode. On a headless Linux box use Xvfb (Mesa software GL works):

```sh
cd build && xvfb-run -s "-screen 0 1600x900x24" ./driftwood --screenshot shot.png --days 30 --hour 11 --zoom 1.3 --select 1 --tab 0
```

Flags: `--days N` fast-forward, `--hour H` time of day, `--zoom Z`, `--select K` (K-th settler), `--tab 0|1|2`, `--seed N`, `--size WxH`, `--center X,Y` (tile to center on), `--show-journal`, and `--pick X,Y`, which prints what a click at that window point (logical pixels, not the HiDPI PNG's) would select; a miss names the nearest settler and where they are.

For any rendering change, compare the fixed scenes in `tools/scenes.txt` before and after:

```sh
tools/shots.sh before        # on the old code
tools/shots.sh after         # on yours, then open .shots/index.html (git-ignored)
```

## Rules

- `src/sim` must not include raylib or anything graphical. It is deterministic for a given seed (behavior RNG is `Sim::gen`); keep it that way so tests stay meaningful.
- New player actions go through public `Sim` methods; the HUD only reads state and calls them.
- Art lives in `src/game/Art.cpp`, one function per object type, drawn in world pixels via `iso::wp` / `iso::P`. Keep objects independent so they can be swapped for sprites one at a time.
- 2D shapes are drawn with backface culling disabled (both windings occur); see `Game::frame`.
- People live in a `std::deque` and projects in a `std::deque` so pointers stay valid when new ones are added. Objects are a `std::vector`; refer to them by index in lambdas.
- Journal text: plain, warm sentences. No em dashes.
