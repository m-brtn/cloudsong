# Cloudsong screenshots

The JPEG files in this directory are in-game captures of Cloudsong. The project owner's requested uses include
this game's README and the [Quench engine showcase](https://github.com/m-brtn/Quench). Artwork sources and their
terms remain in the [game credits](../../CREDITS.md), including the original Hyper3D character and world models.

`cloudsong.jpg` is the sunset view supplied by the project owner on 2026-11-09 as a README candidate. It shows the
red-caped traveller, a flower meadow, winding paths and a hilltop pavilion above the clouds. The source attachment
is `codex-clipboard-87caa8e4-46c1-47af-9f5d-3a80e717a4d3.png`; its exact runtime/capture settings were not recorded.

The original is 2388 x 1286; the README image is resized to 1600 x 862 with Lanczos filtering and saved as an
optimized JPEG at quality 90. No cropping, colour retouching or compositing. The same JPEG is used in Quench's README.
The other JPEGs are retained captures from earlier game builds.

## Automated daylight view

The earlier README view of the starting ruins is still available through `scenarios/showcase.txt`. It uses
the Beautiful preset, frozen 11:30 daylight and a fixed camera with the interface hidden. It is a different view
from the user-supplied sunset image above.

From the repository root, after building Release:

```sh
build/release/cloudsong --gpu-backend metal --frame-size 1920x1080 \
  --audio-device null --user-dir build/showcase --script scenarios/showcase.txt
```

This offscreen run writes `build/showcase/cloudsong.png` and exits. The scenario waits for the hero and navigation
assets before settling the camera. Asynchronous loading can shift transient particles between runs; this is a
showcase view, not a golden-image test. No phone appearance or performance claim is made by this Mac capture.
