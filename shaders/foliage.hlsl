// Cloudsong's grass and flowers: patches of blades that sway in the weather's wind (quench/Wind.hlsli), each blade
// about the ground below it (its root height in uv.y; uv.x 0 at the root, 1 at the tip, 3 on a flower's head), lit
// through from behind toward the sun. The call's ring of light (MaterialDesc::params[0]: xyz its centre, w the
// seconds since the call) passes over the flowers and leaves them glowing for a while. InstanceDesc::custom: x the
// grass's height (m), y its stiffness, z its leaves' flutter (m).

#include "quench/Surface.hlsli"
#include "quench/Wind.hlsli"

void foliageSway(inout QuenchVertexIn v, InstanceData instance) {
    const float3 root = float3(v.position.x, v.uv.y, v.position.z);
    quenchWindSwayAbout(v.position, v.normal, instance, root, instance.custom.x, instance.custom.y, instance.custom.z);
}

#if defined(QUENCH_STAGE_FRAGMENT)
QuenchSurface foliage(QuenchSurfaceInput i) {
    QuenchSurface s = quenchStandardSurface(i);
    // Blades use the meadow's upward shading normal on both sides, avoiding black back-facing needles.
    s.normal = normalize(float3(s.normal.x * 0.35, abs(s.normal.y), s.normal.z * 0.35));
    const float along = saturate(i.uv.x);
    // Light through the blades when the sun is behind them.
    const float back = saturate(dot(-i.view, quenchFrame.sunDir.xyz));
    s.emissive += s.albedo * quenchFrame.sunColor.rgb * (pow(back, 3.0) * 0.35 + 0.04) * along;
    if (i.uv.x > 2.5) {
        // A secluded northern meadow: pale cyan flowers slowly reveal their light after sunset.
        const float night = 1.0 - smoothstep(-0.08, 0.16, quenchFrame.sunDir.y);
        s.emissive += i.color.rgb * i.custom.w * night;
        const float4 ring = quenchMaterial.params[0];
        const float d = distance(i.world.xz, ring.xz);
        const float front = ring.w * 9.0;
        const float wave = exp(-abs(d - front) * 0.8) * saturate(1.0 - ring.w / 5.0);
        const float after = step(d, front) * saturate(1.0 - d / 40.0) * saturate(1.0 - ring.w / 9.0);
        s.emissive += i.color.rgb * (wave * 6.0 + after * 1.5);
    }
    return s;
}
#endif

#define QUENCH_SURFACE foliage
#define QUENCH_SURFACE_VERTEX foliageSway
#include "quench/SurfaceEntries.hlsli"
