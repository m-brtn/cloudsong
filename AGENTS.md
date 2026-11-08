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
`sound` (the mix measured: levels, the call's pitch, footsteps, no clipping), `front` (the title, resting, the
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
`mood KEY VALUES` (the light, sky and haze, for tuning), `debug_ui on|off`. Values: `x y z grounded glide energy
max_energy lights candles warm awake walking seated nav_ready updraft ending screen quality render_scale altar_dist
candle_dist spirit_far vx vy cape_up cape_back glow_draws`. Coordinates are local: the isles float at
`kAltitude` (2200 m).

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
