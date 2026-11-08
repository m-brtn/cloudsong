# Cloudsong

A calm exploration game on the [Quench](https://github.com/m-brtn/Quench) engine: a small hooded figure with a long
cape on grassy isles that float at the golden hour above a sea of clouds. Walk, run, jump, glide and fly between the
isles, gather drifting lights, light the old candles, wake the sleeping spirits and bring them to the temple of light.
A short journey of about ten to fifteen minutes.

![The title over the meadow](docs/images/title.jpg)

Everything in it but one font is made by code: the isles, the grass and flowers, the ruins, the figures, their capes,
the light, the music and the sounds (see `CREDITS.md`).

## The journey

Start on the meadow. Drifting lights hang over the isles: gathering them adds marks to the cape, and each lit mark
is one flap in the air. The marks fill again on the ground near warm light (the lanterns, the lit candles). Three
cold candles stand by three sleeping spirits: by the pond, in the green cave and on an isle across the clouds. Call
beside a candle to light it; its spirit wakes and follows. With every candle lit, rising air opens on the isle of the
broken columns and carries the player up to the temple of light. Stand at its altar with the spirits.

## Controls

| | Keyboard and mouse | Gamepad | Touch (iPhone) |
|---|---|---|---|
| Move | WASD or the arrow keys | left stick | left thumb, anywhere on the left |
| Walk slowly | Left Shift | push the stick a little | a short push |
| Camera | drag with the mouse, or J L I K | right stick | right thumb |
| Jump; in the air: flap (uses a mark), hold: glide | Space | A (south) | the arrow button |
| Call (lights a candle near by) | E | X (west) | the rings button |
| Rest (pause, settings) | Escape | Start (Menu) | the pause button |

Settings (on the title and when resting): picture quality (Light, Balanced, Beautiful), music and sound volume,
camera speed and inverted camera. They are kept in the user directory (`settings.json`). Phones start at Balanced and
60 Hz, and lower their render scale when they grow hot.

## Build

Requirements: CMake 3.28+, Ninja, Xcode with the Metal toolchain. The first configure downloads the engine (pinned in
`CMakeLists.txt`) and its dependencies.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
build/release/cloudsong
tools/package.sh        # dist/: the macOS app in a zip (--ios: also the iOS app, signed by a paid team)
```

Engine flags (`--help` lists them all): `--quit-after S`, `--capture FILE`, `--script FILE`, `--frame-size WxH`,
`--gpu-backend metal|sdl`, `--no-audio`. Automated runs (`--script`, `--capture`, `--quit-after`) are offscreen.

## Layout

```
src/Game.cpp        the game: screens, camera, light, weather, input, the end, scenario commands
src/Front.*         the title, the pause menu and its settings, the controls' hint, the touch controls
src/Settings.*      the player's settings (settings.json)
src/World.*         the isles, grass and flowers, ruins, the cave, the temple, pond, candles (meshes, physics,
                    the navmesh's geometry)
src/Character.*     the procedural figures: skinned mesh, gaits, foot and head IK, the cape's cloth
src/Player.*        the player's movement on Jolt's virtual character: run, jump, glide, flap, updrafts
src/Journey.*       the drifting lights, the candles, the energy
src/Spirits.*       the spirits: asleep, waking, following on the navmesh or through the air, gathered
src/Sound.*         the score and the sounds, synthesized; the wind through the engine's weather sounds
src/Glow.*          an additive pass for beams, rings of light, motes and halos
shaders/            surfaces (ground, stone, figures; grass and flowers in the wind) and the glow
scenarios/          scripted checks (--script), run by ctest; views.txt captures the key views
tests/golden/       reference captures for the golden image checks
tools/package.sh    the macOS and iOS packages
devlog/             development log
```
