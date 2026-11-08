# Cloudsong

A calm exploration game on the [Quench](https://github.com/m-brtn/Quench) engine: a small hooded figure with a long
cape on grassy isles that float at the golden hour above a sea of clouds. Walk, run, jump, glide and fly between the
isles, gather drifting lights, light the old candles, wake the sleeping spirits and bring them to the temple of light.

Everything in it is made by code: the isles, the grass and flowers, the ruins, the figures, their capes, the light
and the sounds. There are no third-party art assets (see `CREDITS.md`).

## Controls

| | Keyboard and mouse | Gamepad |
|---|---|---|
| Move | WASD or the arrow keys | left stick |
| Walk slowly | Left Shift | |
| Camera | drag with the mouse, or J L I K | right stick |
| Jump; in the air: flap upward (uses a light), hold: glide | Space | A (south) |

## Build

Requirements: CMake 3.28+, Ninja, Xcode with the Metal toolchain. The first configure downloads the engine (pinned in
`CMakeLists.txt`) and its dependencies.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
build/release/cloudsong
```

Engine flags (`--help` lists them all): `--quit-after S`, `--capture FILE`, `--script FILE`, `--frame-size WxH`,
`--gpu-backend metal|sdl`, `--no-audio`. Automated runs (`--script`, `--capture`, `--quit-after`) are offscreen.

## Layout

```
src/Game.cpp        the game: camera, light, weather, input, scenario commands
src/World.*         the isles, grass and flowers, ruins, pond, candles (meshes and physics)
src/Character.*     the procedural figures: skinned mesh, gaits, foot and head IK, the cape's cloth
src/Player.*        the player's movement on Jolt's virtual character: run, jump, glide, flap, updrafts
src/Glow.*          an additive pass for beams, rings of light, motes and halos
shaders/            surfaces (ground, stone, figures; grass and flowers in the wind) and the glow
scenarios/          scripted checks (--script), run by ctest; views.txt captures the key views
devlog/             development log
```
