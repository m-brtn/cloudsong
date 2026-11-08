// Cloudsong's world surface: the isles' ground, the stone of the ruins and the caped figures, lit by the engine.
// MaterialDesc::params[0].x picks the look: 0 ground, 1 stone (moss on what faces up), 2 a figure (its cape's marks
// glow with the flight energy), 3 the cave's rock (mossy outside, dim inside). A figure's InstanceDesc::custom: x the
// energy (marks lit), y the marks on the cape, z a flash (0..1), w how dormant it is (0 awake .. 1 grey and still).

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
        // The ground: soft variation at two scales; where it is steep, rock in strata with cracks.
        const float n = csNoise(i.world.xz * 0.31) * 0.6 + csNoise(i.world.xz * 1.9) * 0.4;
        s.albedo *= lerp(0.84, 1.1, n);
        const float steep = 1.0 - smoothstep(0.45, 0.75, i.normal.y);
        if (steep > 0.0) {
            const float2 q = float2(i.world.x + i.world.z, i.world.y);
            const float strata = csNoise(float2(q.x * 0.08, q.y * 1.1)) * 0.6 + csNoise(q * float2(0.5, 3.0)) * 0.4;
            const float groove = csNoise(float2(q.x * 1.3, q.y * 0.15)) * 0.6 + csNoise(float2(q.x * 4.0, q.y * 0.4)) * 0.4;
            const float3 rock = float3(0.36, 0.32, 0.28) * lerp(0.7, 1.2, strata) * lerp(0.75, 1.08, groove);
            s.albedo = lerp(s.albedo, rock, steep * 0.85);
            s.roughness = lerp(s.roughness, 0.8, steep);
        }
    } else if (mode < 1.5) {
        // Stone: weathered, with moss on its upper faces.
        const float3 p = i.world * 1.3;
        const float n = csNoise(p.xz + p.y * 0.7) * 0.55 + csNoise(p.xy * 2.7 + p.z) * 0.45;
        s.albedo *= lerp(0.78, 1.12, n);
        // Moss in patches on what faces up, more toward the edges of the stone.
        const float patches = csNoise(i.world.xz * 0.9 + i.world.y * 0.3) * 0.65 + csNoise(i.world.xz * 3.7) * 0.35;
        const float moss = smoothstep(0.5, 0.75, i.normal.y) * smoothstep(0.52, 0.78, patches);
        s.albedo = lerp(s.albedo, float3(0.2, 0.3, 0.1), moss * 0.8);
        s.roughness = lerp(s.roughness, 0.95, moss);
        s.emissive += csRim(i, s.normal, 0.05) * s.albedo;
    } else if (mode > 2.5) {
        // The cave's rock: moss over its back outside; inside dim, green and damp, out of the sky's light.
        const float3 p = i.world * 0.8;
        const float n = csNoise(p.xz + p.y * 0.7) * 0.55 + csNoise(p.xy * 2.3 + p.z) * 0.45;
        s.albedo *= lerp(0.75, 1.15, n);
        if (i.front) {
            // Rock in strata on the steep sides, moss on the back.
            const float strata = csNoise(float2((i.world.x + i.world.z) * 0.1, i.world.y * 1.3)) * 0.6 +
                                 csNoise(float2(i.world.x - i.world.z, i.world.y * 4.0) * 0.5) * 0.4;
            s.albedo *= lerp(0.72, 1.18, strata);
            const float moss = smoothstep(0.45, 0.8, i.normal.y + (n - 0.5) * 0.5);
            s.albedo = lerp(s.albedo, float3(0.24, 0.34, 0.12), moss * 0.9);
            s.emissive += csRim(i, s.normal, 0.05) * s.albedo;
        } else {
            const float moss = smoothstep(0.35, 0.7, n + 0.25 * csNoise(i.world.xz * 0.5));
            s.albedo = lerp(s.albedo * float3(0.75, 0.85, 0.8), float3(0.12, 0.26, 0.14), moss * 0.8);
            s.occlusion = 0.3;
            s.roughness = lerp(0.9, 0.55, moss);
            // The moss gives back a little green light of its own.
            s.emissive += float3(0.015, 0.05, 0.025) * moss;
        }
    } else {
        // A figure. The cape (uv.x from 2): its inside darker, diamond marks down the back that glow with the energy
        // (dim once spent), a dark hem under a thin gold line.
        const float dormant = saturate(i.custom.w);
        if (i.uv.x >= 1.5) {
            const float u = i.uv.x - 2.0;
            const float v = i.uv.y;
            if (!i.front) s.albedo *= 0.5;
            // The cape's size in metres at this height (Character.cpp's capeRest): u across its arc, v down.
            const float flare = pow(v, 0.85);
            const float across = lerp(4.6, 4.0, sqrt(v)) * lerp(0.0925, 0.29, flare);
            const float down = 0.45;
            // Character.h's kMarkUv, in the order the marks light.
            static const float2 kMarks[8] = {float2(0.5, 0.2),   float2(0.5, 0.37), float2(0.5, 0.54),  float2(0.5, 0.71),
                                      float2(0.36, 0.3),  float2(0.64, 0.3), float2(0.34, 0.52), float2(0.66, 0.52)};
            const int count = int(clamp(i.custom.y, 0.0, 8.0) + 0.5);
            float shape = 0.0;
            float lit = 0.0;
            for (int k = 0; k < 8; ++k) {
                if (k >= count) break;
                const float2 d = abs((float2(u, v) - kMarks[k]) * float2(across, down));
                const float diamond = d.x / 0.03 + d.y / 0.042;
                const float m = 1.0 - smoothstep(0.8, 1.0, diamond);
                shape = max(shape, m);
                lit = max(lit, m * saturate(i.custom.x - float(k)));
            }
            const float3 gold = float3(1.0, 0.76, 0.36);
            s.albedo = lerp(s.albedo, lerp(s.albedo * 0.7 + gold * 0.15, gold, saturate(lit * 2.0)), shape * 0.9);
            const float trim = smoothstep(0.885, 0.9, v);
            const float seam = smoothstep(0.855, 0.865, v) * (1.0 - smoothstep(0.875, 0.885, v));
            s.albedo = lerp(s.albedo, s.albedo * 0.32, trim);
            s.albedo = lerp(s.albedo, gold * 0.8, seam);
            s.emissive += gold * lit * (6.0 + 10.0 * i.custom.z) * (1.0 - dormant) * (i.front ? 1.0 : 0.25);
        }
        // Dormant: grey stone-like.
        const float grey = dot(s.albedo, float3(0.3, 0.55, 0.15));
        s.albedo = lerp(s.albedo, grey * float3(0.82, 0.84, 0.9), dormant * 0.85);
        // A soft light round the edges that lifts the figures off the grass, strongest against the sun.
        const float edge = pow(1.0 - saturate(dot(s.normal, i.view)), 2.5);
        const float towardSun = saturate(dot(-i.view, quenchFrame.sunDir.xyz) * 0.5 + 0.5);
        s.emissive += quenchFrame.sunColor.rgb * edge * (0.08 + 0.4 * towardSun) * lerp(s.albedo + 0.15, 0.25, dormant);
        s.roughness = 0.8;
    }
    return s;
}
#endif

#define QUENCH_SURFACE worldSurface
#include "quench/SurfaceEntries.hlsli"
