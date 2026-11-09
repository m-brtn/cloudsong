// Cloudsong's world surface: the isles' ground, the stone of the ruins and the caped figures, lit by the engine.
// MaterialDesc::params[0].x picks the look: 0 ground, 1 stone (moss on what faces up), 2 a figure (its cape's marks
// glow with the flight energy), 3 the cave's rock (mossy outside, dim inside), 4 an isle's rock below its rim (uv.y:
// how far down, 0 at the rim .. 1), 5 the imported hero's energy marks. A figure's InstanceDesc::custom: x the
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

// Noise projected along the normal's main axes, so that no face stretches it.
float csTriNoise(float3 p, float3 w) {
    return csNoise(p.yz) * w.x + csNoise(p.xz + 31.0) * w.y + csNoise(p.xy + 67.0) * w.z;
}

// The weights of csTriNoise's planes for a normal.
float3 csTriWeights(float3 N) {
    const float3 w = pow(abs(N), 4.0);
    return w / (w.x + w.y + w.z);
}

// Soft stone over a base colour: broad patches and a fine grain, warmer where it faces the sun, cooler and lilac where
// it faces down onto the clouds.
float3 csStone(QuenchSurfaceInput i, float3 base) {
    const float3 w = csTriWeights(i.normal);
    const float big = csTriNoise(i.world * 0.12, w);
    const float mid = csTriNoise(i.world * 0.75 + 13.0, w);
    const float fine = csTriNoise(i.world * 9.0 + 5.0, w);
    // Restrained large masses; close stone detail comes from a filtered grain instead of
    // metre-wide blurry clouds painted over every surface.
    const float grainFade = 1.0 - smoothstep(.2, .8, max(length(ddx(i.world)), length(ddy(i.world))) * 9.0);
    float3 c = base * lerp(.96, 1.04, big) * lerp(.95, 1.04, mid) * lerp(1.0, lerp(.94, 1.05, fine), grainFade);
    c *= lerp(1.0, float3(1.06, 0.99, 0.9), saturate(dot(i.normal, quenchFrame.sunDir.xyz)) * 0.6);
    c *= lerp(1.0, float3(0.84, 0.86, 1.04), saturate(-i.normal.y) * 0.8);
    return c;
}

// The normal bent by soft bumps of stone (noise of `scale` per metre).
float3 csBump(QuenchSurfaceInput i, float3 N, float scale, float strength) {
    const float3 w = csTriWeights(N);
    const float3 p = i.world * scale;
    strength *= 1.0 - smoothstep(.25, .9, max(length(ddx(p)), length(ddy(p))));
    const float e = 0.15;
    const float f = csTriNoise(p, w);
    const float3 g = float3(csTriNoise(p + float3(e, 0.0, 0.0), w), csTriNoise(p + float3(0.0, e, 0.0), w),
                            csTriNoise(p + float3(0.0, 0.0, e), w)) -
                     f;
    return normalize(N - strength * (g - dot(g, N) * N) / e);
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
    if (mode > 5.5) {
        // Thin-film colour follows the viewing angle; almost clear through the centre.
        const float facing = saturate(abs(dot(i.normal,i.view)));
        const float rim = pow(1.0-facing,2.7);
        const float film = (1.0-facing)*2.2 + i.normal.y*.22;
        const float3 rainbow = .55 + .45*cos(6.2831853*(film + float3(0,.33,.67)));
        s.albedo = lerp(float3(.8,.93,1),rainbow,.65);
        s.opacity = .012 + rim*.48;
        s.roughness = .07;
        s.metallic = .30;
        s.emissive = rainbow*rim*.32;
    } else if (mode < 0.5) {
        // The ground: soft variation at two scales; where it is steep, soft stone.
        const float n = csNoise(i.world.xz * 0.31) * 0.6 + csNoise(i.world.xz * 1.9) * 0.4;
        s.albedo *= lerp(0.84, 1.1, n);
        const float steep = 1.0 - smoothstep(0.45, 0.75, i.normal.y);
        if (steep > 0.0) {
            // Calm rock planes with sparse, broken bedding seams. Large bump noise made these
            // cliffs look like a blurred plaster texture at walking distance.
            const float layer = (i.world.y - 2200.0 + .18 * csNoise(i.world.xz * .21)) * 1.15;
            const float distanceToSeam = abs(frac(layer) - .5);
            const float seamAA = max(.002, fwidth(layer));
            const float seam = (1.0 - smoothstep(.009 - seamAA, .009 + seamAA, distanceToSeam)) *
                smoothstep(.32, .7, csNoise(float2((i.world.x + i.world.z) * .35, floor(layer) * 1.73))) *
                (1.0 - smoothstep(.08,.3,fwidth(layer)));
            const float grainFade = 1.0 - smoothstep(.2,.8,max(length(ddx(i.world)),length(ddy(i.world))) * 16.0);
            const float grain = lerp(1.0, lerp(.985,1.015,csTriNoise(i.world * 16.0,csTriWeights(i.normal))),grainFade);
            const float3 rock = float3(.29,.36,.38) * grain * (1.0 - .12 * seam);
            s.albedo = lerp(s.albedo, rock, steep);
            s.normal = normalize(lerp(s.normal, csBump(i, s.normal, 16.0, .007), steep));
            s.roughness = lerp(s.roughness, 0.85, steep);
        }
        // Terrain-integrated paths share the ground's depth and topology, including junctions.
        if (i.uv.y < -9.0) {
            const float path = smoothstep(-.1, .12, i.uv.x);
            const float grit = csNoise(i.world.xz * 14.0);
            s.albedo = lerp(s.albedo, float3(.43, .46, .39) * lerp(.96, 1.035, grit), path);
            s.roughness = lerp(s.roughness, .96, path);
        }
    } else if (mode > 4.5) {
        // Separate skinned diamonds projected onto the imported cape at authoring time.
        const float k = floor(i.uv.x + 0.5);
        if (k >= i.custom.y) discard;
        const float visible = 1.0;
        const float lit = saturate(i.custom.x - k) * visible;
        const float3 gold = float3(1.0, 0.76, 0.36);
        s.albedo = gold * lerp(0.08, 0.75, lit);
        const float daylight = smoothstep(0.0, 0.42261826, quenchFrame.sunDir.y);
        s.emissive = gold * lit * lerp(1.2, 6.0, daylight) * (1.0 + 1.666667 * i.custom.z);
        s.roughness = 0.9;
    } else if (mode > 3.5) {
        // An isle's rock below the rim: soft stone, moss on the ledges that face up, a rim of light against the sun.
        s.albedo = csStone(i, s.albedo);
        // Small mineral grain, not broad smeared normals on the underside.
        s.normal = csBump(i, s.normal, 12.0, 0.012);
        const float bedding = (i.world.y - 2200.0 + .30 * csNoise(i.world.xz * .10)) * .72;
        const float seamWidth = max(.002, fwidth(bedding));
        const float seam = 1.0 - smoothstep(.014 - seamWidth, .014 + seamWidth, abs(frac(bedding) - .5));
        const float broken = smoothstep(.35, .7, csNoise(float2((i.world.x+i.world.z)*.3,floor(bedding))));
        s.albedo *= 1.0 - .15 * seam * broken * (1.0 - smoothstep(.08,.3,fwidth(bedding)));
        const float patches = csNoise(i.world.xz * 0.35 + i.world.y * 0.2) * 0.6 + csNoise(i.world.xz * 1.7) * 0.4;
        const float moss = smoothstep(0.2, 0.55, s.normal.y + (patches - 0.5) * 0.4) * step(0.04, i.uv.y);
        s.albedo = lerp(s.albedo, float3(0.09, 0.27, 0.18) * lerp(0.8, 1.2, patches), moss * 0.85);
        s.emissive += csRim(i, s.normal, 0.08) * s.albedo;
    } else if (mode < 1.5) {
        // Stone: weathered, with moss on its upper faces.
        const float3 p = i.world * 1.3;
        const float n = csNoise(p.xz + p.y * 0.7) * 0.55 + csNoise(p.xy * 2.7 + p.z) * 0.45;
        s.albedo *= lerp(0.95, 1.04, n);
        s.albedo = csStone(i, s.albedo);
        s.normal = csBump(i, s.normal, 8.0, .025);
        // Moss in patches on what faces up, more toward the edges of the stone.
        const float patches = csNoise(i.world.xz * 0.9 + i.world.y * 0.3) * 0.65 + csNoise(i.world.xz * 3.7) * 0.35;
        const float moss = smoothstep(0.5, 0.75, i.normal.y) * smoothstep(0.52, 0.78, patches);
        s.albedo = lerp(s.albedo, float3(0.09, 0.27, 0.18), moss * 0.24);
        s.roughness = lerp(s.roughness, 0.95, moss);
        s.emissive += csRim(i, s.normal, 0.05) * s.albedo;
    } else if (mode > 2.5) {
        // The cave's rock: moss over its back outside; inside dim, green and damp, out of the sky's light.
        const float3 p = i.world * 0.8;
        const float n = csNoise(p.xz + p.y * 0.7) * 0.55 + csNoise(p.xy * 2.3 + p.z) * 0.45;
        s.albedo *= lerp(0.75, 1.15, n);
        if (i.front) {
            // Soft stone on the steep sides, moss on the back.
            s.albedo = csStone(i, s.albedo);
            s.normal = csBump(i, s.normal, 0.5, 0.3);
            const float moss = smoothstep(0.45, 0.8, i.normal.y + (n - 0.5) * 0.5);
            s.albedo = lerp(s.albedo, float3(0.12, 0.3, 0.19), moss * 0.9);
            s.emissive += csRim(i, s.normal, 0.05) * s.albedo;
        } else {
            // Soft moss in wide patches, pale damp stone between them; specks that glow in the moss.
            s.normal = csBump(i, s.normal, 0.6, 0.35);
            const float moss = smoothstep(0.3, 0.7, n + 0.35 * (csNoise(i.world.xz * 0.5 + i.world.y * 0.3) - 0.3));
            s.albedo = lerp(s.albedo * float3(0.72, 0.8, 0.78), float3(0.1, 0.22, 0.13), moss * 0.85);
            s.occlusion = 0.3;
            s.roughness = lerp(0.9, 0.55, moss);
            const float3 q = i.world * 3.0;
            const float3 cell = floor(q);
            const float speck = step(0.95, csHash(cell.xz + cell.y * float2(17.1, 5.3))) *
                                (1.0 - smoothstep(0.08, 0.16, length(frac(q) - 0.5)));
            s.emissive += float3(0.015, 0.05, 0.03) * moss + float3(0.25, 0.9, 0.6) * speck * moss;
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
            const float flare = sin(v * 1.57079633);
            const float across = lerp(5.0, 4.5, sqrt(v)) * lerp(0.1, 0.285, flare);
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
            const float trim = smoothstep(0.955, 0.975, v);
            const float seam = smoothstep(0.925, 0.933, v) * (1.0 - smoothstep(0.942, 0.95, v));
            s.albedo = lerp(s.albedo, s.albedo * 0.55, trim);
            s.albedo = lerp(s.albedo, gold * 0.8, seam);
            // Preserve a readable diamond at night without flooding its surrounding cloth with bloom.
            const float markDaylight = smoothstep(0.0, 0.42261826, quenchFrame.sunDir.y);
            const float markLight = lerp(1.2, 6.0, markDaylight);
            s.emissive += gold * lit * markLight * (1.0 + 1.666667 * i.custom.z) * (1.0 - dormant) *
                          (i.front ? 1.0 : 0.25);
        }
        // Dormant: grey stone-like.
        const float grey = dot(s.albedo, float3(0.3, 0.55, 0.15));
        s.albedo = lerp(s.albedo, grey * float3(0.82, 0.84, 0.9), dormant * 0.85);
        // A soft light round the edges that lifts the figures off the grass, strongest against the sun.
        const float edge = pow(1.0 - saturate(dot(s.normal, i.view)), 2.5);
        const float towardSun = saturate(dot(-i.view, quenchFrame.sunDir.xyz) * 0.5 + 0.5);
        s.emissive += quenchFrame.sunColor.rgb * edge * (0.04 + 0.16 * towardSun) * lerp(s.albedo + 0.15, 0.25, dormant);
        if (i.uv.x >= 1.5) {
            // Thin cloth carries a little warm light through its back, unlike the opaque tunic and hood.
            const float through = saturate(dot(-s.normal, quenchFrame.sunDir.xyz));
            s.emissive += s.albedo * quenchFrame.sunColor.rgb * through * 0.075 * (1.0 - dormant);
        }
        // A restrained weave, filtered away before it can shimmer at a distance.
        const float2 clothUv = i.uv * float2(150.0, 110.0);
        const float weaveFade = 1.0 - smoothstep(0.2, 0.75, max(fwidth(clothUv.x), fwidth(clothUv.y)));
        s.albedo *= 1.0 + 0.025 * sin(clothUv.x * 6.2831853) * sin(clothUv.y * 6.2831853) * weaveFade;
        s.roughness = 0.92;
    }
    return s;
}
#endif

#define QUENCH_SURFACE worldSurface
#include "quench/SurfaceEntries.hlsli"
