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
`windgarden` (collectible uniqueness, three-chime unlock, grounded/airborne activation, pause and cooldown),
`meadow` (creature approach/wait/release, both bubble pushes and pause), `water` (animated water entry, hover,
shore landing, jump without flap energy, pause), `places` (cottage doorway and cave traversal, bird idle),
`region` (expanded landscape captures, bridge traversal, sanctuary collision, day/night trails),
`hero` (imported model/textures, finite skinning/attachments and forward-facing boots through run, glide, flap and landing),
`garments` (a complete idle loop, movement, settling and glide; per-frame peak motion and jitter of the cape and hood),
`settings` (saved preferences and compatibility), `picture` (camera blur eligibility, menus, resize, quality switches
and bypass), `daylight` (frozen/running time, midnight, pause and settings),
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
`time_of_day HOUR`, `day_cycle on|off`, `day_length MINUTES`, `picture_effects BLUR COLOUR` (0..1, 0..0.3),
`hero_pose bind|animated` (rig diagnosis),
`mood KEY VALUES` (the light, sky and haze, for tuning; fog is a tint of the daylight fog), `debug_ui on|off`,
`visit_chime I` (teleport to an item, without granting it),
`garment_audit_reset` (clear the per-frame peak garment motion and jitter). Settings scenario points: `@morning`, `@noon`, `@evening`, `@day_cycle_toggle`, `@settings_back`
(visible controls only; scroll first when needed). Values: `hero_ready hero_animated hero_facing_forward hero_jump_leg_separation hero_knee_bend hero_feet_lateral_error hero_foot_clearance hero_toe_flex path_max_grade path_max_cross_slope jump_crouch landing_crouch hero_pose_valid hero_feet_forward hero_knees_forward hero_hood_bend hero_cape_motion hero_hood_motion hero_cape_jitter hero_hood_jitter hero_cape_motion_peak hero_hood_motion_peak hero_cape_jitter_peak hero_hood_jitter_peak hero_ground_slope hero_toe_slope_error settings_open time_of_day day_cycle sun_elevation x y z grounded glide energy
max_energy lights chimes chime_total wind_song_unlocked wind_song wind_cooldown candles warm awake walking seated nav_ready updraft ending screen quality render_scale altar_dist
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

`Picture` reads depth in PostHdr and draws a final camera-motion/colour pass before the UI. Its separate target
must never be written into the renderer's TAA history. Camera blur uses a short fixed exposure, a bounded sample
span and depth rejection at silhouettes; it is off for menus, cuts and stalls. `picture_blur` reports eligibility,
not the magnitude of motion. No object-motion blur is generated. The pass supports SDR and Apple's extended-linear
HDR; it bypasses HDR10/PQ, and skips blur if an upscaler does not expose depth. Zeroing both settings bypasses it.

Packages: `tools/package.sh` (Release, ctest, `dist/Cloudsong-<version>-<hash>-macos.zip`, a smoke run from the
unzipped app); `--ios` also builds the iOS app and IPA signed by a paid team (`--team ID` or `QUENCH_IOS_TEAM`, never written
into files) into `dist/`. Packages include `BUILD-INFO.txt` with game and engine commits; the iOS bundle's assets
must match the mobile staging directory. Installing it on a phone is a separate step, only when the user asks.

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

## Imported player

`art/character-v3/export/hero-rigged.glb` is the untouched approved Hyper3D export. `prepared/hero.glb` is its
normalized game source, cooked with embedded textures by `quench_cook_assets`. `tools/prepare_hero.py` (offline,
Python with NumPy and Pillow) regenerates that source and `src/HeroData.h`. Keep them together. The tool keeps the
strongest four of the export's 16 influences, corrects its misplaced hood chain, restricts central tunic arm
weights, rebinds the cape independently of limbs, welds skin weights across UV seams, and projects separate energy
marks onto the cape. The knee bind planes face forward; boot shafts and heels stay rigid while ToeBase bends
only the forefoot. The original export and provenance remain intact.
`Hero` retains the model through `SceneAssets` and plays the audited idle/jog torso and arm rhythm through
`AnimationSystem`. `tools/prepare_hero_motion.py` prepares in-place loops. The stock sideways run, obstacle jump
and asymmetric walking clip are not selected by the gameplay graph. Foot support, heel rise and knee-lifting
return arcs share the jog phase, with per-foot terrain placement. The ankle/heel use Foot, the tall boot shaft
stays on Leg, and ToeBase flexes the forefoot only. Jump phases are anticipation, extension, an asymmetric
leading-knee tuck, and landing compression. Forward knee poles prevent axial retargeting twists.
The imported cape uses a constrained particle cage with inertia, air drag, bending limits and body collision.
Cape and hood simulations use a fixed 120 Hz clock and a nearby translation-only origin, avoiding millimetre
rounding feedback at the isles' 2200 m altitude. Interpolated poses follow the current sewn body frames; wind is
filtered and synthetic flutter is limited to movement/flight. Front overlapping panels remain predominantly on
the torso. Motion metrics are maximum cage speeds relative to Chest/Head in m/s; jitter is the residual after
an 80 ms velocity filter. Peak metrics accumulate every frame until `garment_audit_reset`. Cloth mobility tapers over 12 cm of mesh distance
from sewn body boundaries; triangular cage weights keep the four-influence skin continuous. The narrower folded hood crown has a separate
three-segment cloth chain, while the face and aperture stay fixed to Head. The spirits keep Character's original
cloth and procedural body. Trail/glow anchors follow the imported skin. `scenarios/hero.txt` checks full run phases,
actual foot clearance and toe flex, slope placement, jumps and finite garment/attachment transforms.

## Region architecture

`src/Region.cpp` authors the side routes, graded road profiles, bridge decks, foundation pads and garden masonry;
`World.cpp` owns the 17-isle terrain and its collision mesh. Paths are a signed-distance field on the terrain's
own triangles, so junctions cannot overlap in depth. Profiles keep their landing heights and limit longitudinal
grade; cross-sections are level and blend into shoulders. Bridge decks and their bank cuts use the same profile.
Rock and turf share the exact rim boundary. Overlapping lower islands retain their own ungraded terrain and
foliage heights. Foliage excludes paths, bridge decks and building foundations;
new-isle flora has a separate density budget and patches beyond 115 m are hidden. The northern flower meadow uses
foliage `custom.w` for restrained night emission.
The original Rodin sanctuary is `art/world/sanctuary/sanctuary.glb`, cooked as `sanctuary.qmodel`. Rendering shares
the hero's single `SceneAssets` owner; collision decodes the same cooked model during world initialization.
Do not create a second SceneAssets for the same AssetManager. Provenance: `art/world/sanctuary/CREDITS.md`.

## Windgarden

`art/world/windgarden` preserves the generated willow and wind-harp PBR exports and provenance. The world cooks
and shares one asset per type. `layoutGarden()` keeps roots off roads, away from spirits/ponds and on gentle
ground; the same placements drive cleared grass, rendering and mesh collision. Large flat-topped hills have
three shelves, and island underside rings close at a common height instead of splitting into needle triangles.
Twelve gold-ring chimes are distinct from the original light balls. Journey tracks collection once per run;
three unlock Player’s 1.2 s glide gust with a 5 s cooldown, activated by the existing call input.
The folded hood rest mesh, its corrected bind chain, HeroData and prepared motion source must be regenerated
together by both preparation scripts. Runtime springs preserve this rest silhouette rather than crushing a cone.

## Authored places and residents

The cottage entrance has three low wooden steps and no ramp. Bird source/rig
`bd0f8ae7-5d72-4c2b-ac1c-e5ef9fd437cb` was explicitly selected by the user despite its feet; `bird.glb` equals
`bird-source.glb` byte-for-byte. Do not reshape its feet or switch to later rejected birds. Its enlarged cage
sits at cottage offset (-2, 0, -4.9), outside the porch/planters; bird yaw is pi so it faces away from the porch.
The original Idle Standing clip plays through the shared animation system.

`MeadowLife` owns the curious quadruped, bounded wandering/approach behavior, two buoyant pushable soap bubbles
and short violet eye trails attached to the animated Head palette. It shares Hero's SceneAssets and
AnimationSystem, preparing parameters before the system's single update and drawing after it. Simulation and
clip time pause with the menu. The approved source and rig are in the windgarden credits.

Pond levitation is a Player state with a damped height target, no teleport. Entry raises the feet, leaving water
settles them briefly onto the bank, and jumping exits without spending flap energy. Character blends a one-leg
tuck with spread arms; Hero's cloth continues to simulate. The optional water elevation is supplied only over
the visible pond where the ground is submerged. It does not apply over the cloud sea. The sailboat is a static
model beside the pond centre, with matching mesh collision and its keel half a metre below the waterline; it no
longer orbits the horizon.

The cave's terrain cut, interior wall profile and traversable floor agree; the generated rock entrance surrounds
them. Never cover a mismatched entrance with a ramp. Every road junction shares endpoint elevations; cubic
shoulder weights prevent unrelated neighbouring roads from pulling a centreline sideways. The region test
checks actual finished terrain grade and cross-slope as well as bridge traversal.
