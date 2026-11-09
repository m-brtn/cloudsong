#include "quench/Bindings.hlsli"
#include "PictureUniforms.h"

struct PictureOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

#if defined(QUENCH_ENTRY_picture_vs)
PictureOut picture_vs(uint vertex : SV_VertexID) {
    PictureOut o;
    o.uv = float2((vertex << 1) & 2, vertex & 2);
    o.position = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1);
    return o;
}
#endif

#if defined(QUENCH_ENTRY_picture_fs)
cbuffer PictureBlock : QUENCH_UNIFORM(0) { PictureUniforms picture; }
Texture2D<float4> sceneColor : QUENCH_TEXTURE(0);
SamplerState sceneSampler : QUENCH_SAMPLER(0);
Texture2D<float> sceneDepth : QUENCH_TEXTURE(1);
SamplerState depthSampler : QUENCH_SAMPLER(1);

float3 linearColor(float2 uv) {
    const float3 c = max(sceneColor.SampleLevel(sceneSampler, uv, 0).rgb, 0);
    return picture.effect.w > .5 ? pow(c, ((float3)(2.2))) : c;
}

float4 picture_fs(PictureOut i) : SV_Target0 {
    float3 color = linearColor(i.uv);
    if (picture.effect.x > 0) {
        const float depth = sceneDepth.SampleLevel(depthSampler, i.uv + picture.depth.xy, 0);
        const float4 previous = mul(picture.previousClip, float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, depth, 1));
        if (previous.w > 1e-5) {
            const float2 previousUv = float2(previous.x, -previous.y) / previous.w * .5 + .5;
            float2 span = (i.uv - previousUv) * picture.effect.x;
            const float pixels = length(span * picture.size.zw);
            span *= min(1.0, picture.effect.y / max(pixels, 1e-5));
            // Preserve nearby faces/cape, and ignore a resting camera's subpixel noise.
            span *= smoothstep(.35, 1.2, pixels) * lerp(.3, 1., smoothstep(2.5, 8., picture.depth.z / max(depth, 1e-6)));
            if (pixels > .35) {
                float weight = 1;
                [unroll] for (int tap = 0; tap < 4; ++tap) {
                    const float along = (float(tap) - 1.5) / 3.;
                    const float2 uv = clamp(i.uv + span * along, picture.size.xy * .5, 1 - picture.size.xy * .5);
                    const float other = sceneDepth.SampleLevel(depthSampler, uv + picture.depth.xy, 0);
                    // Reject samples across silhouettes instead of smearing sky over the player/trees.
                    const float edge = abs(other - depth) / max(max(other, depth), 1e-5);
                    const float w = (1 - smoothstep(.015, .06, edge)) * .5;
                    color += linearColor(uv) * w;
                    weight += w;
                }
                color /= weight;
            }
        }
    }
    const float luma = dot(color, float3(.2126, .7152, .0722));
    const float hi = max(color.r, max(color.g, color.b));
    const float lo = min(color.r, min(color.g, color.b));
    const float chroma = (hi - lo) / max(hi, 1e-4);
    // Muted foliage/sky get more colour; colourful capes and neutral clouds change less.
    color = clamp(luma + (color - luma) * (1 + picture.effect.z * (1 - .7 * chroma)), 0, picture.depth.w);
    if (picture.effect.w > .5) color = pow(color, ((float3)(1. / 2.2)));
    return float4(color, 1);
}
#endif
