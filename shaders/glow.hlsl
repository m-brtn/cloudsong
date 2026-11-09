// Cloudsong's glow pass (render::PassStage::Transparent): light added to the HDR image, depth tested against the
// scene, thinned by the fog. The geometry comes from the vertex index: a beam is a camera-facing strip along its
// axis, the call's ring a band of quads around its centre, sprites camera-facing quads.

#include "quench/Fog.hlsli"
#include "quench/Frame.hlsli"

#include "GlowUniforms.h"

struct GlowOut {
    float4 position : SV_Position;
    float4 local : TEXCOORD0; // xy: the shape's coordinates, z: the fog's transmission, w: the type
    float4 color : TEXCOORD1; // rgb: radiance, w: sprites' core
};

cbuffer GlowBlock : QUENCH_UNIFORM(1) {
    GlowDraw glow;
}

#if defined(QUENCH_ENTRY_glow_vs)
static const float2 kCorners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                   float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};

GlowOut glow_vs(uint vertex : SV_VertexID) {
    const uint quad = vertex / 6u;
    const float2 c = kCorners[vertex % 6u];
    const int type = int(glow.color.w + 0.5);
    float3 world = float3(0.0, 0.0, 0.0);
    float4 local = float4(0.0, 0.0, 1.0, float(type));
    float4 color = float4(glow.color.rgb, 0.0);
    if (type == CS_GLOW_BEAM) {
        const float t = (float(quad) + c.y) / float(CS_BEAM_SEGMENTS);
        const float3 p = lerp(glow.a.xyz, glow.b.xyz, t);
        const float r = lerp(glow.a.w, glow.b.w, t);
        const float3 axis = normalize(glow.b.xyz - glow.a.xyz);
        const float3 toCamera = normalize(quenchFrame.cameraPos.xyz - p);
        float3 across = cross(axis, toCamera);
        const float l = length(across);
        across = l > 1e-4 ? across / l : glow.right.xyz;
        const float side = c.x * 2.0 - 1.0;
        world = p + across * side * r;
        // Seen along its axis a strip is thin: faded.
        local.xy = float2(side, t);
        color.rgb *= saturate(l * 1.5);
    } else if (type == CS_GLOW_RING) {
        const float a = 6.2831853 * (float(quad) + c.x) / float(CS_RING_SEGMENTS);
        world = glow.a.xyz + float3(cos(a), 0.0, sin(a)) * glow.a.w + float3(0.0, (c.y - 0.25) * glow.b.x, 0.0);
        local.xy = float2(c.x, c.y);
    } else {
        const float4 s = glow.sprite[quad];
        const float2 q = c * 2.0 - 1.0;
        world = s.xyz + (glow.right.xyz * q.x + glow.up.xyz * q.y) * s.w;
        local.xy = q;
        color = glow.spriteColor[quad];
    }
    GlowOut o;
    o.position = mul(quenchFrame.viewProj, float4(world, 1.0));
    local.z = 1.0 - fogAt(world, quenchFrame.cameraPos.xyz).a;
    // Beams are bright enough to shine through the haze from afar.
    if (type == CS_GLOW_BEAM) local.z = sqrt(local.z);
    o.local = local;
    o.color = color;
    return o;
}
#endif

#if defined(QUENCH_ENTRY_glow_fs)
float4 glow_fs(GlowOut i) : SV_Target0 {
    const int type = int(i.local.w + 0.5);
    float a = 0.0;
    if (type == CS_GLOW_BEAM) {
        const float side = i.local.x;
        const float t = i.local.y;
        a = exp(-side * side * 3.5) * smoothstep(0.0, max(glow.params.z, 1e-3), t) *
            (1.0 - smoothstep(glow.params.w, 1.0, t));
        // Reach zero at the strip boundary, including against a dark night sky.
        a *= 1.0 - smoothstep(0.65, 1.0, abs(side));
        // Dust in the light, drifting.
        const float time = glow.params.y;
        a *= 0.75 + 0.25 * sin(t * 37.0 - time * 0.9 + side * 2.0) * sin(t * 13.0 + time * 0.4);
    } else if (type == CS_GLOW_RING) {
        const float y = i.local.y;
        a = smoothstep(0.0, 0.25, y) * (1.0 - y) * (1.0 - y) * glow.b.y;
    } else {
        const float r2 = dot(i.local.xy, i.local.xy);
        if (r2 > 1.0) discard;
        const float soft = (1.0 - r2) * (1.0 - r2);
        a = soft * soft * (1.0 - i.color.w) + (exp(-r2 * 30.0) * 2.0 + soft * 0.4) * i.color.w;
    }
    return float4(i.color.rgb * a * i.local.z, 0.0);
}
#endif
