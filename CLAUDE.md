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

Flags: `--days N` fast-forward, `--hour H` time of day, `--zoom Z`, `--select K` (K-th settler), `--narrate N` (narrate the selected settler N times, to show contrariness), `--question` (a settler asks the narrator something, shown open in the pending list), `--kit` (an orchard, flower beds, lanterns, a bench and a banner placed near camp and finished), `--review` (the last year-in-review card), `--ending haven|homeward|awakening|overruled|lost|divided` (end the run that way and show the ending screen), `--tab 0|1|2|3` (settlers, narrator, build, camp), `--seed N`, `--size WxH`, `--center X,Y` (tile to center on), `--show-journal`, `--links` (every settler's line to their work, as the C key toggles), `--gallery` (a sheet of every tree kind, season and growth stage instead of the world, for judging plant art), `--buildings` (a sheet of assembled buildings in three variants each, and a longhouse going up stage by stage), `--placement` (pick the first unlocked blueprint from the build menu and show the placement screen; `--pick X,Y` aims it elsewhere), `--title` (the title screen), `--menu` (the game paused with the load picker), `--save-dir DIR` (saves somewhere else), `--structures` (the same for the well, rack, watchtower, lighthouse, signal pyre, mine, dock, story circle, kiln and boatyard), `--people` (a sheet of settler figures: facings, looks, children, the walk cycle, and the far-away version at the default zoom), `--bench N` (times N whole frames and prints the average and worst, for any change that adds drawing), and `--pick X,Y`, which prints what a click at that window point (logical pixels, not the HiDPI PNG's) would select; a miss names the nearest settler and where they are.

For any rendering change, compare the fixed scenes in `tools/scenes.txt` before and after:

```sh
tools/shots.sh before        # on the old code
tools/shots.sh after         # on yours, then open .shots/index.html (git-ignored)
```

## Rules

- `src/sim` must not include raylib or anything graphical. It is deterministic for a given seed (behavior RNG is `Sim::gen`); keep it that way so tests stay meaningful.
- New player actions go through public `Sim` methods; the HUD only reads state and calls them.
- Art lives in `src/game/Art.cpp`, one function per object type, drawn in world pixels via `iso::wp` / `iso::P`. Keep objects independent so they can be swapped for sprites one at a time.
- Sizes come from `src/game/Scale.h` (metres; a tile is 2 m, a person 1.7 m). `game_tests` measures the drawn art against it, so a new object gets a row there and in the `scale` test.
- Draw order is not a depth number: `WorldView::collect` gives each drawable a ground footprint and its measured screen box, and `src/game/Depth` sorts them. New kinds of things go through `collect` with an honest footprint.
- Buildings are assembled from parts in `src/game/Building` (base, walls, door, windows, roof, chimney, porch, extras) by a recipe per type, seeded by the project id. Each part is a `solid::Solid` with a construction stage. A new type gets a recipe there and a row in `testBuildings` (must-have parts, footprint, height, variety).
- Paths (`Sim::findPath`) go round buildings: `walkGrid` marks started and finished footprints solid, except docks (walkable over water), crop plots and the story circle. A walk may leave the building it starts in and enter the one it is going to; anything else that must be stood on (a bed, a work spot) goes beside the footprint.
- Clicks hit the drawn shape (`draw::Probe`), and `src/game/Inspect` builds each card from sim state. A new object type needs a card there; the test checks every drawable has one.
- What a settler is doing for the map comes from `Person::focus`, set where the job is handed out (`setQ`'s last argument). A new job sets one; `sim_tests` checks everyone at work has a focus.
- The look: world colors pass through `draw::tone` and solid shapes get `draw::ink` outlines (both set in `WorldView::draw`), so pick ordinary colors and let the tone soften them. UI colors are the light `ui::T` theme; `game_tests` checks text contrast (WCAG AA).
- A tree that would hide a settler is drawn see-through (`WorldView::seeThrough`, into an offscreen buffer so its own layers do not show through each other). Standing buildings get trodden earth and a soft shadow at their foot in the ground pass, before the sorted drawables.
- Figures ease between poses (`figure::blend`, held per settler in `art::personAt`); tool swings use a shaped curve (slow lift, quick strike).
- 2D shapes are drawn with backface culling disabled (both windings occur); see `Game::frame`.
- People live in a `std::deque` and projects in a `std::deque` so pointers stay valid when new ones are added. Objects are a `std::vector`; refer to them by index in lambdas.
- Journal text: plain, warm sentences. No em dashes.
- Specialist buildings (a project with a gift) are placed by the player when `Sim::askPlacement` is on; `src/sim/SimSites.cpp` scores spots (`evaluateSite`, named factors) and settlers judge them by expertise (`opinions`, hashed so looking never changes the game). A building's effect scales with its `siteQuality` (`Sim::siteQ`). A new specialist building gets its factors there and a row in the site tests.
- Households and homes live in `src/sim/SimHomes.cpp`: a household owns one home, its status is the home's level (hut, cottage, house, manor), and `homeNeeds` lists what each level asks. Settlers build huts themselves; better homes come only by a hut rising in place (`setHomeLevel`). Goods are one table (`Sim::goods`, listed with their makers in `goodList`), and the kinds of food brought in are dated in `foodSeen`; a new good gets a row in `goodList`, a maker in the craft job, and a use somewhere.
- Petitions and the year's trials live in `src/sim/SimTrials.cpp`; a trial's effects scale with `trialHard(kind)`, 0 when none is running, so new effects multiply by it.
- Saving: `Sim::save`/`load` in `src/sim/SimSave.cpp` list every field once for both directions. A new field in `Sim` or any type it holds gets a line there, and `SAVE_VERSION` goes up when the layout changes (old saves are then refused with a reason). `sim_tests` checks a save round-trips byte for byte. Slots live in `src/game/Saves`.
