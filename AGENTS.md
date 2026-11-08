# AGENTS.md

Instructions for coding agents working on Cloudsong, a game on the Quench engine
(https://github.com/m-brtn/Quench). The overview for people is in `README.md`, the daily development log in `devlog/`.

## Build and test

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
build/debug/cloudsong                                         # play
build/debug/cloudsong --capture /tmp/cloudsong.png --capture-at 1 --quit-after 1
build/debug/cloudsong --help                                  # engine flags
```

Automated runs (`--script`, `--capture`, `--quit-after`: ctest, captures, scripts) are offscreen on desktops: no window
on the screen, no input from real devices, no sound (the engine's `docs/app.md`, "Offscreen runs"). `--onscreen`
shows the window, for example to watch a scenario or to measure a perf region.

iOS: build the shader compiler and the asset cooker for the Mac first
(`cmake --preset host-tools && cmake --build --preset host-tools`),
then `ios-sim-release` (Simulator, ad-hoc signed) or `ios-xcode` (device, Xcode Automatic signing with
`-DQUENCH_IOS_TEAM=<team id>`). Device runs: install and launch with `xcrun devicectl`.

Gates before a commit: build `debug` and `release`, `ctest` for both, take a capture and look at it (do not assume).
Changes that can behave differently on a phone: run on the device, or ask the user to, and log their report.

The checks (`ctest`, all offscreen, their files in `build/<preset>/captures/`): `smoke` (keys move and jump), `flight`
(glide and flap between isles), `journey` (the whole way, candles, spirits on the navmesh, the updraft, the end),
`settings` (saved preferences and compatibility), `daylight` (frozen/running time, midnight, pause and settings),
`sound` (the mix measured: levels, the call's pitch, footsteps, no clipping), `guide` (the call's guide: none before
a call, then toward the next goal, lingering, gone after its fade, one at a time, circling at the goal; the particles
and trails per picture setting in its log), `front` (the title, resting, the
quality, the touch controls under scripted fingers), `captures` and `golden_<view>` (six views at fixed frames
compared with `tests/golden/metal/`, captured on the default backend, native Metal, on the M4 Pro Mac). A golden image
changes only with an intended change of the look: look at both, copy `build/release/captures/golden/<view>.png`
over it, and write the reason in the devlog. Spirits are not in the golden views: their navmesh is built on worker
threads, so the frame it is ready on varies. `scenarios/views.txt` captures the showcase views at any size;
`scenarios/perf.txt` measures frame and GPU times at five places per picture setting (offscreen: an A/B of costs
before and after a change in the same session, not frame rates on a display).

Game scenario commands (besides the engine's): `teleport X Y Z [YAW]`, `camera YAW PITCH DIST`, `orbit DYAW PITCH
DIST`, `aim X Z`, `view EX EY EZ TX TY TZ` / `view off`, `move X Y`, `jump down|up`, `call`, `light I`, `clouds
layer|off|volumetric`, `quality low|medium|high`, `screen title|play|pause|settings`, `touch_controls on|off`,
`time_of_day HOUR`, `day_cycle on|off`, `day_length MINUTES`,
`mood KEY VALUES` (the light, sky and haze, for tuning; fog is a tint of the daylight fog), `debug_ui on|off`. Settings scenario points: `@morning`, `@noon`, `@evening`, `@day_cycle_toggle`, `@settings_back`
(visible controls only; scroll first when needed). Values: `settings_open time_of_day day_cycle sun_elevation x y z grounded glide energy
max_energy lights candles warm awake walking seated nav_ready updraft ending screen quality render_scale altar_dist
candle_dist spirit_far vx vy cape_up cape_back glow_draws particles particle_systems trails wing_trail guide
guide_dist guide_goal` (`particles` and `trails` as the renderer drew them; `wing_trail` 1 while the cape's ribbons
are on; `guide` 1 while the call's guide is out, `guide_dist` from its first head to its goal, 0 while it circles the
figure, `guide_goal` the last call's goal: a candle 0..2, 3 the updraft, 4 the altar, -1 at hand). Coordinates are
local: the isles float at `kAltitude` (2200 m).

The light effects are the engine's particles and trails (`src/Particles.h`): the cape's ribbons while gliding or
flapping; the call's guide (a few heads with soft ribbons that rise beside the figure, fly over the isles to the
next goal, into the cave through its mouth, linger and fade; with the goal at hand they circle the figure once);
sparse twinkling points round the camera over the flower fields, round the lanterns, along the isles' rims and in the
cave (denser as the sun goes down); a few motes in the beam, the open updraft and the cave. Their counts follow the
picture setting. The glow pass (`src/Glow.h`) draws the beams, the call's ring, the drifting lights, the small
creatures by every other light until it is gathered. Lanterns and candles use emissive flame meshes with bloom
and local illumination, without large sprite halos.

Packages: `tools/package.sh` (Release, ctest, `dist/Cloudsong-<version>-<hash>-macos.zip`, a smoke run from the
unzipped app); `--ios` also builds the iOS app signed by a paid team (`--team ID` or `QUENCH_IOS_TEAM`, never written
into files) into `dist/`; installing it on a phone is a separate step, only when the user asks.

## Engine

- How to build a game on the engine (modules, assets, scenarios, captures, iOS, pitfalls): the agent skill in
  `.agents/skills/quench-game/`, copied from the engine by `tools/new_game.py`. When the engine pin moves, copy it
  again from the engine's `.agents/skills/quench-game/` at that commit.
- The engine is fetched at the commit pinned in `CMakeLists.txt` (`GIT_TAG`). For engine work at the same time,
  configure with `-DFETCHCONTENT_SOURCE_DIR_QUENCH=<engine checkout>`; before committing the game, pin a pushed engine
  commit and build without the override.
- The engine's own rules, flags and scenario commands: its `AGENTS.md` and `docs/app.md`.
- Read assets and write user files only through `quench::fs` (assets come from the bundle on iOS); no `std::ifstream`,
  `std::ofstream` or `fopen` in game code.
- Graphics only through `quench::gpu` and `quench::render`, never SDL GPU directly. Uniform blocks are defined once in `shaders/*.h` with
  16-byte members only.
- No blocking `while (running)` loop: the engine owns `main` and calls the game (`quench::app::Game`).
- Right-handed, Y up, meters, kilograms, seconds. Reversed depth (near 1, far 0).

## Conventions

- All repository content is in English: code, comments, UI strings, logs, commit messages, docs and the devlog.
- Keep a devlog in `devlog/YYYY-MM-DD.md` (template in `devlog/README.md`): verified results with numbers, separate
  from what was not verified.
- Every asset needs a licence that allows its use, recorded in a `CREDITS.md` next to the files.
- Commit and push only when the user asks; never commit `build/`, captures or secrets.
