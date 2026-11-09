# Cloudsong screenshots

The JPEG files in this directory are in-game captures of Cloudsong. The project owner's requested uses include
this game's README and the [Quench engine showcase](https://github.com/m-brtn/Quench). Artwork sources and their
terms remain in the [game credits](../../CREDITS.md), including the original Hyper3D character and world models.

`cloudsong.jpg` was captured on 2026-11-09 from Cloudsong 0.1.1, with the runtime and artwork of commit
`08cbbbf9866181625c730a29b2f0b074ebee5bc5` and the published Quench 0.12.1 pin. It shows the starting meadow, carved
ruins, player and floating islands. The capture uses native Metal on an Apple M4 Pro Mac, the Beautiful picture
preset, frozen 11:30 daylight, the default picture effects and a fixed camera, with the interface hidden.

The original is 1920 x 1080; the README image is resized to 1600 x 900 with Lanczos filtering and saved as an
optimized JPEG at quality 90. No colour retouching or compositing. The same JPEG is used in Quench's README.
The other JPEGs are retained captures from earlier game builds.

## Reproduce the hero image

From the repository root, after building Release:

```sh
build/release/cloudsong --gpu-backend metal --frame-size 1920x1080 \
  --audio-device null --user-dir build/showcase --script scenarios/showcase.txt
```

This offscreen run writes `build/showcase/cloudsong.png` and exits. The scenario waits for the hero and navigation
assets before settling the camera. Asynchronous loading can shift transient particles between runs; this is a
showcase view, not a golden-image test. No phone appearance or performance claim is made by this Mac capture.
