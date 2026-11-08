// Cloudsong's world surface: the isles' ground, the stone of the ruins and the caped figures, lit by the engine.
// MaterialDesc::params[0].x picks the look: 0 ground, 1 stone (moss on what faces up), 2 a figure (its cape's marks
// glow with the flight energy). A figure's InstanceDesc::custom: x the energy (marks lit), y the marks on the cape,
// z a flash (0..1), w how dormant it is (0 awake .. 1 grey and still).

#include "quench/Surface.hlsli"

#if defined(QUENCH_STAGE_FRAGMENT)
float csHash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float csNoise(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(csHash(i), csHash(i + float2(1.0, 0.0)), u.x),
                lerp(csHash(i + float2(0.0, 1.0)), csHash(i + float2(1.0, 1.0)), u.x), u.y);
}

// A soft light that wraps around the figures and the stone from behind and from the sky: the painted look of the
// lit edges.
float3 csRim(QuenchSurfaceInput i, float3 N, float strength) {
    const float facing = 1.0 - saturate(dot(N, i.view));
    const float towardSun = saturate(dot(-i.view, quenchFrame.sunDir.xyz) * 0.6 + 0.4);
    return quenchFrame.sunColor.rgb * pow(facing, 3.0) * towardSun * strength;
}

QuenchSurface worldSurface(QuenchSurfaceInput i) {
    QuenchSurface s = quenchStandardSurface(i);
    const float mode = quenchMaterial.params[0].x;
    if (mode < 0.5) {
        // The ground: soft variation at two scales.
        const float n = csNoise(i.world.xz * 0.31) * 0.6 + csNoise(i.world.xz * 1.9) * 0.4;
        s.albedo *= lerp(0.84, 1.1, n);
    } else if (mode < 1.5) {
        // Stone: weathered, with moss on its upper faces.
        const float3 p = i.world * 1.3;
        const float n = csNoise(p.xz + p.y * 0.7) * 0.55 + csNoise(p.xy * 2.7 + p.z) * 0.45;
        s.albedo *= lerp(0.78, 1.12, n);
        const float moss = smoothstep(0.45, 0.8, i.normal.y + (csNoise(i.world.xz * 2.1 + i.world.y) - 0.5) * 0.7);
        s.albedo = lerp(s.albedo, float3(0.16, 0.25, 0.06), moss * 0.85);
        s.roughness = lerp(s.roughness, 0.95, moss);
        s.emissive += csRim(i, s.normal, 0.05) * s.albedo;
    } else {
        // A figure. The cape (uv.x from 2): its inside darker, a band of diamond marks that glow with the energy and
        // a gold hem.
        const float dormant = saturate(i.custom.w);
        if (i.uv.x >= 1.5) {
            const float u = i.uv.x - 2.0;
            const float v = i.uv.y;
            if (!i.front) s.albedo *= 0.55;
            const float marks = max(i.custom.y, 1.0);
            const float cell = (u - 0.18) / 0.64 * marks;
            const float index = floor(cell);
            const float2 local = float2(frac(cell) - 0.5, (v - 0.42) * marks * 1.9);
            const float diamond = abs(local.x) + abs(local.y) * 0.75;
            const float inBand = step(0.0, cell) * step(cell, marks);
            const float shape = (1.0 - smoothstep(0.26, 0.31, diamond)) * inBand;
            const float lit = saturate(i.custom.x - index);
            const float3 gold = float3(1.0, 0.72, 0.32);
            s.albedo = lerp(s.albedo, gold * 0.9, shape * 0.8);
            const float hem = smoothstep(0.9, 0.93, v) * (1.0 - smoothstep(0.97, 0.99, v));
            s.albedo = lerp(s.albedo, gold * 0.85, hem);
            s.emissive += gold * shape * (lit * (3.5 + 6.0 * i.custom.z) * (1.0 - dormant)) * (i.front ? 1.0 : 0.3);
        }
        // Dormant: grey stone-like.
        const float grey = dot(s.albedo, float3(0.3, 0.55, 0.15));
        s.albedo = lerp(s.albedo, grey * float3(0.82, 0.84, 0.9), dormant * 0.85);
        s.emissive += csRim(i, s.normal, 0.35) * lerp(s.albedo + 0.15, 0.25, dormant);
        s.roughness = 0.8;
    }
    return s;
}
#endif

#define QUENCH_SURFACE worldSurface
#include "quench/SurfaceEntries.hlsli"
