#ifndef CLOUDSONG_PICTURE_UNIFORMS_H
#define CLOUDSONG_PICTURE_UNIFORMS_H
#include "quench/Shared.h"

QUENCH_SHARED_BEGIN
struct QUENCH_ALIGN16 PictureUniforms {
    float4x4 previousClip; // stable current clip -> previous clip, computed in double precision
    float4 size;           // xy: inverse output size; zw: output size
    float4 effect;         // x: exposure/frame duration; y: maximum span in pixels; z: colour boost; w: gamma encoding
    float4 depth;          // xy: depth jitter in UV; z: near plane; w: maximum linear display value
};
QUENCH_SHARED_END
#endif
