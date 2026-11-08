#ifndef CLOUDSONG_GLOW_UNIFORMS_H
#define CLOUDSONG_GLOW_UNIFORMS_H

// The glow pass (shaders/glow.hlsl, src/Glow.cpp): light added to the HDR image after the transparent surfaces,
// depth tested against the scene: shafts and beams of light, the call's ring, drifting motes and soft sprites.
// Members are 16-byte types only.

#include "quench/Shared.h"

#define CS_MAX_SPRITES 32
#define CS_GLOW_BEAM 0
#define CS_GLOW_RING 1
#define CS_GLOW_MOTES 2
#define CS_GLOW_SPRITES 3
#define CS_BEAM_SEGMENTS 16
#define CS_RING_SEGMENTS 72

QUENCH_SHARED_BEGIN

struct QUENCH_ALIGN16 GlowDraw {
    // Beam: xyz the start, w its radius there. Ring: xyz the centre on the ground, w the radius. Motes: xyz the
    // centre of their box, w how many.
    float4 a;
    // Beam: xyz the end, w its radius there. Ring: x its height, y its fade. Motes: xyz the box's half size, w a
    // mote's radius.
    float4 b;
    float4 color;  // rgb: linear HDR radiance, w: the type (CS_GLOW_*)
    float4 params; // x: seed, y: time (s), z: beam: fade in along it (0..1), motes: drift (m); w: beam: fade out
    float4 right;  // xyz: the camera's right (world)
    float4 up;     // xyz: the camera's up (world)
    float4 sprite[CS_MAX_SPRITES];      // xyz: centre, w: radius (m)
    float4 spriteColor[CS_MAX_SPRITES]; // rgb: radiance, w: core (0..1: a bright centre)
};

QUENCH_SHARED_END

#endif
