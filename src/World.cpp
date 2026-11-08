#include "World.h"

#include <quench/core/Log.h>
#include <quench/physics/PhysicsWorld.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <format>

namespace cloudsong {

namespace render = quench::render;

namespace {

const glm::vec3 kAlt(0.0f, kAltitude, 0.0f);

float bumpAt(const Bump& b, glm::vec2 xz) {
    float radius = b.radius;
    if (b.jagged > 0.0f) {
        const glm::vec2 dir = glm::normalize(xz - b.at + glm::vec2(1e-5f));
        radius *= 1.0f + b.jagged * fbm(dir * 2.4f + b.at * 0.01f, 3, 97u);
    }
    const float d = glm::distance(xz, b.at) / radius;
    if (d >= 1.0f) return 0.0f;
    const float t = std::clamp((d - b.flat) / std::max(1.0f - b.flat, 1e-3f), 0.0f, 1.0f);
    return b.height * 0.5f * (1.0f + std::cos(kPi * t));
}

glm::vec3 stoneColor(Random& r) {
    const float v = r.uniform(0.85f, 1.1f);
    return srgb(172, 160, 146) * v * glm::vec3(1.0f, r.uniform(0.97f, 1.02f), r.uniform(0.94f, 1.0f));
}

} // namespace

float Isle::outline(float angle) const {
    const glm::vec2 c(std::cos(angle), std::sin(angle));
    return desc.radius * (1.0f + 0.16f * fbm(c * 1.6f + glm::vec2(float(desc.seed % 97)), 3, desc.seed) +
                          0.05f * fbm(c * 5.0f, 2, desc.seed + 7));
}

std::optional<float> Isle::heightAt(glm::vec2 xz) const {
    const glm::vec2 d = xz - desc.centre;
    const float r = glm::length(d);
    const float edge = outline(std::atan2(d.y, d.x));
    const float t = r / edge;
    if (t > 1.0f) return std::nullopt;
    float h = desc.base;
    const float rolling = fbm(xz / (desc.radius * 0.45f + 6.0f), 3, desc.seed + 3) * 0.5f + 0.5f;
    h += desc.hills * rolling * (1.0f - 0.7f * smoothstep(0.55f, 1.0f, t));
    for (const Bump& b : desc.bumps) h += bumpAt(b, xz);
    for (const glm::vec4& level : desc.levels)
        h = glm::mix(std::min(h, level.w), h,
                     smoothstep(level.z, level.z + 2.0f, glm::distance(xz, glm::vec2(level.x, level.y))));
    // A rounded lip at the rim.
    const float lip = smoothstep(0.86f, 1.0f, t);
    h -= std::min(1.6f, desc.radius * 0.07f) * lip * lip;
    return h;
}

float Isle::slope(glm::vec2 xz) const {
    const float e = 0.5f;
    const std::optional<float> x0 = heightAt(xz - glm::vec2(e, 0.0f)), x1 = heightAt(xz + glm::vec2(e, 0.0f));
    const std::optional<float> z0 = heightAt(xz - glm::vec2(0.0f, e)), z1 = heightAt(xz + glm::vec2(0.0f, e));
    if (!x0 || !x1 || !z0 || !z1) return 0.0f;
    return glm::length(glm::vec2(*x1 - *x0, *z1 - *z0)) / (2.0f * e);
}

glm::vec3 Isle::grassColor(glm::vec2 xz) const {
    const float a = fbm(xz / 22.0f, 3, desc.seed + 11) * 0.5f + 0.5f;
    const float b = fbm(xz / 6.0f, 2, desc.seed + 13) * 0.5f + 0.5f;
    glm::vec3 c = lerpColor(srgb(92, 140, 72), srgb(156, 178, 94), smoothstep(0.3f, 0.75f, a));
    c = lerpColor(c, srgb(76, 124, 80), smoothstep(0.62f, 0.95f, b) * 0.6f);
    return c;
}

World::World() {
    // The region's layout (local coordinates; y up, +z toward the temple).
    m_isles = {
        // The meadow, where the journey starts: a hill to glide from, a pond, ruins.
        {{{0.0f, 0.0f},
          0.0f,
          46.0f,
          3.5f,
          34.0f,
          11u,
          {{{14.0f, 16.0f}, 16.0f, 7.0f, 0.12f},
           {{-27.0f, 12.0f}, 10.0f, 3.0f},
           {{-15.0f, -10.0f}, 8.5f, -2.4f, 0.45f},
           {{4.0f, -22.0f}, 9.0f, 0.6f, 0.6f}},
          1.0f}},
        // The cave isle.
        {{{62.0f, 58.0f}, 3.0f, 30.0f, 1.0f, 28.0f, 23u, {{{64.0f, 63.0f}, 13.0f, 0.4f, 0.85f}}, 0.8f}},
        // Stepping stones toward the spirit isle.
        {{{-36.0f, 46.0f}, 3.0f, 9.0f, 0.8f, 18.0f, 31u, {}, 1.4f}},
        // The spirit isle.
        {{{-62.0f, 90.0f}, 1.5f, 13.0f, 1.2f, 24.0f, 37u, {}, 1.2f}},
        // The isle of the updraft.
        {{{-20.0f, 128.0f}, 0.5f, 15.0f, 0.8f, 26.0f, 41u, {{{-20.0f, 128.0f}, 6.0f, 0.3f, 0.7f}}, 1.0f}},
        // The temple's mountain.
        // The temple's mesa: sheer rock on a ring of meadow, a stair around it.
        {{{10.0f, 214.0f},
          40.0f,
          42.0f,
          1.5f,
          64.0f,
          53u,
          {{{10.0f, 218.0f}, 22.0f, 24.0f, 0.7f, 0.09f}, {{-16.0f, 202.0f}, 7.0f, 2.5f}, {{36.0f, 214.0f}, 6.0f, 2.0f}},
          0.7f}},
        // Far isles that frame the view.
        {{{125.0f, 140.0f}, 26.0f, 26.0f, 4.0f, 30.0f, 61u, {{{125.0f, 140.0f}, 14.0f, 9.0f}}, 0.6f}},
        {{{-125.0f, 30.0f}, -14.0f, 30.0f, 3.0f, 30.0f, 67u, {}, 0.6f}},
        {{{95.0f, -55.0f}, 8.0f, 18.0f, 2.0f, 20.0f, 71u, {}, 0.6f}},
        {{{-85.0f, -95.0f}, 22.0f, 22.0f, 3.0f, 26.0f, 73u, {{{-85.0f, -95.0f}, 10.0f, 6.0f}}, 0.6f}},
        {{{-150.0f, 190.0f}, 60.0f, 34.0f, 4.0f, 50.0f, 79u, {{{-150.0f, 190.0f}, 20.0f, 30.0f}}, 0.4f}},
    };

    Landmarks& l = m_landmarks;
    auto ground = [&](float x, float z, float above) {
        const std::optional<float> h = terrainHeight({x, z});
        return glm::vec3(x, (h ? *h : 0.0f) + above, z);
    };
    l.start = ground(2.0f, -36.0f, 0.0f);
    l.startYaw = 0.0f;
    l.pond = ground(-15.0f, -10.0f, 0.0f);
    l.pondRadius = 6.6f;
    l.caveCentre = ground(64.0f, 63.0f, 0.0f);
    l.caveRadius = 11.0f;
    l.caveShafts = {ground(63.0f, 60.5f, 0.0f), ground(67.5f, 65.5f, 0.0f), ground(61.0f, 67.0f, 0.0f)};
    // The shafts come down slanted from holes in the roof.
    for (const glm::vec3& f : l.caveShafts) l.caveHoles.push_back(f + glm::vec3(-1.6f, 7.2f, 0.8f));
    {
        const glm::vec2 out = glm::normalize(glm::vec2(-l.caveCentre.x, -l.caveCentre.z));
        l.caveEntrance = ground(l.caveCentre.x + out.x * 8.0f, l.caveCentre.z + out.y * 8.0f, 0.0f);
    }
    l.lights = {ground(-6.0f, -27.0f, 1.4f),    ground(18.0f, -10.0f, 1.5f),     ground(-31.0f, 4.0f, 1.5f),
                ground(14.0f, 16.0f, 2.2f),     ground(-10.0f, 22.0f, 1.4f),     glm::vec3(36.0f, 12.0f, 36.0f),
                ground(52.0f, 42.0f, 1.5f),     ground(76.0f, 70.0f, 1.5f),      ground(64.5f, 64.0f, 1.6f),
                glm::vec3(-28.4f, 3.4f, 36.2f), glm::vec3(-50.0f, 5.0f, 68.0f),  ground(-58.0f, 96.0f, 1.5f),
                ground(-24.0f, 122.0f, 1.5f),   glm::vec3(-6.0f, 97.0f, 160.0f), ground(4.0f, 178.0f, 1.6f),
                ground(15.0f, 211.0f, 1.6f)};
    // A candle before each spirit: the call lights it and wakes the spirit.
    l.candles = {ground(-20.4f, -15.2f, 1.05f), ground(60.8f, 64.2f, 1.05f), ground(-65.2f, 88.2f, 1.05f)};
    l.lanterns = {ground(5.0f, -33.0f, 1.05f), ground(-33.5f, 42.5f, 1.05f), ground(-14.5f, 123.0f, 1.05f)};
    l.spirits = {ground(-21.5f, -16.0f, 0.0f), ground(59.5f, 64.5f, 0.0f), ground(-66.0f, 87.0f, 0.0f)};
    l.spiritYaw = {0.9f, 1.8f, 0.6f};
    l.updraft = ground(-20.0f, 128.0f, 0.0f);
    l.updraftRadius = 3.2f;
    l.updraftTop = 104.0f;
    // The temple and the updraft's floor stand on level ground, at the lowest ground under them.
    auto level = [&](std::uint32_t seed, glm::vec2 at, float radius) {
        for (Isle& isle : m_isles)
            if (isle.desc.seed == seed) {
                float low = 1e9f;
                for (int i = 0; i < 48; ++i)
                    for (int k = 0; k <= 4; ++k) {
                        const float a = kTau * float(i) / 48.0f, r = radius * float(k) / 4.0f;
                        low = std::min(low, isle.heightAt(at + glm::vec2(std::cos(a), std::sin(a)) * r).value_or(low));
                    }
                isle.desc.levels.push_back({at.x, at.y, radius, low});
            }
    };
    level(53u, {10.0f, 218.0f}, 11.8f);
    level(41u, {-20.0f, 128.0f}, 5.2f);
    l.altar = ground(10.0f, 218.0f, 0.0f);
    l.beamBase = l.altar + glm::vec3(0.0f, 1.2f, 0.0f);

    m_clear = {{{-15.0f, -10.0f}, 6.9f}, {{4.0f, -22.0f}, 5.0f}, {{-20.0f, 128.0f}, 5.5f}, {{10.0f, 218.0f}, 9.0f}};
    m_stone = {{{4.0f, -22.0f}, 5.0f}, {{-20.0f, 128.0f}, 5.5f}, {{10.0f, 218.0f}, 9.0f}};
}

std::optional<float> World::terrainHeight(glm::vec2 xz) const {
    std::optional<float> best;
    for (const Isle& isle : m_isles)
        if (const std::optional<float> h = isle.heightAt(xz); h && (!best || *h > *best)) best = h;
    return best;
}

Ground World::groundAt(glm::vec3 local) const {
    for (const auto& [c, r] : m_stone)
        if (glm::distance(glm::vec2(local.x, local.z), c) < r) return Ground::Stone;
    const std::optional<float> h = terrainHeight({local.x, local.z});
    if (h && local.y < *h + 0.6f) return Ground::Grass;
    return Ground::Stone;
}

bool World::excluded(glm::vec2 xz) const {
    for (const auto& [c, r] : m_clear)
        if (glm::distance(xz, c) < r) return true;
    return false;
}

void World::addStatic(quench::PhysicsWorld& physics, const MeshData& mesh) {
    if (mesh.indices.empty()) return;
    // The triangles are kept around their middle: Jolt packs a mesh's bounds as half floats, too coarse 2 km up.
    glm::vec3 low(1e30f), high(-1e30f);
    for (const auto& v : mesh.vertices) {
        low = glm::min(low, v.position);
        high = glm::max(high, v.position);
    }
    const glm::vec3 middle = (low + high) * 0.5f;
    JPH::TriangleList triangles;
    triangles.reserve(mesh.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        auto p = [&](std::size_t k) {
            const glm::vec3 v = mesh.vertices[mesh.indices[i + k]].position - middle;
            return JPH::Float3(v.x, v.y, v.z);
        };
        const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
        if (glm::length(glm::cross(b - a, c - a)) < 1e-6f) continue; // degenerate (a dome's crown)
        triangles.push_back(JPH::Triangle(p(0), p(1), p(2)));
        addNavTriangle(a, b, c);
    }
    JPH::MeshShapeSettings settings(triangles);
    JPH::ShapeSettings::ShapeResult shape = settings.Create();
    if (shape.HasError()) {
        quench::log::error("game", "static mesh: {}", shape.GetError().c_str());
        return;
    }
    const JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(middle.x, middle.y, middle.z), JPH::Quat::sIdentity(),
                                         JPH::EMotionType::Static, quench::PhysicsLayers::kStatic);
    physics.bodies().CreateAndAddBody(body, JPH::EActivation::DontActivate);
}

void World::addNavTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    // Only the isles of the way (not the far ones); world to local.
    const glm::vec3 m = (a + b + c) / 3.0f - kAlt;
    if (m.x < -85.0f || m.x > 95.0f || m.z < -55.0f || m.z > 250.0f) return;
    const auto first = std::uint32_t(m_nav.vertices.size());
    for (const glm::vec3& v : {a, b, c}) m_nav.vertices.push_back(v - kAlt);
    m_nav.indices.insert(m_nav.indices.end(), {first, first + 1, first + 2});
}

void World::addBox(quench::PhysicsWorld& physics, glm::vec3 centre, glm::vec3 half, float yaw) {
    const JPH::BodyCreationSettings body(
        new JPH::BoxShape(JPH::Vec3(half.x, half.y, half.z), 0.02f), JPH::RVec3(centre.x, centre.y, centre.z),
        JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw), JPH::EMotionType::Static, quench::PhysicsLayers::kStatic);
    physics.bodies().CreateAndAddBody(body, JPH::EActivation::DontActivate);
    // Its faces for the navmesh (counter-clockwise from outside).
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), centre) *
                        glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                        glm::scale(glm::mat4(1.0f), half);
    auto corner = [&](int i) {
        return glm::vec3(m * glm::vec4(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f, 1.0f));
    };
    static constexpr int kFaces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                         {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (const auto& f : kFaces) {
        addNavTriangle(corner(f[0]), corner(f[1]), corner(f[2]));
        addNavTriangle(corner(f[0]), corner(f[2]), corner(f[3]));
    }
}

void World::buildIsle(render::Scene& scene, quench::PhysicsWorld& physics, const Isle& isle) {
    const IsleDesc& d = isle.desc;
    const int segments = std::max(48, int(d.radius * 3.2f));
    const int rings = std::max(12, int(d.radius * 1.0f));
    const int under = 16;
    MeshData mesh;
    std::vector<float> rimY(std::size_t(segments + 1));
    // The top: rings of the outline from the centre out.
    for (int r = 0; r <= rings; ++r) {
        const float t = float(r) / float(rings);
        for (int s = 0; s <= segments; ++s) {
            const float a = kTau * float(s % segments) / float(segments);
            const float edge = isle.outline(a);
            const glm::vec2 xz = d.centre + glm::vec2(std::cos(a), std::sin(a)) * edge * std::min(t, 0.9995f);
            const float h = isle.heightAt(xz).value_or(d.base - 1.6f);
            glm::vec3 color = isle.grassColor(xz);
            // Steep ground shows its rock.
            const float rock = smoothstep(0.7f, 1.05f, isle.slope(xz));
            const float strata = fbm(glm::vec2(h * 0.8f, (xz.x + xz.y) * 0.05f), 2, d.seed + 41) * 0.5f + 0.5f;
            color = lerpColor(color, lerpColor(srgb(150, 138, 120), srgb(116, 108, 100), strata), rock);
            color *= shade(xz);
            // Darker grass over the lip, a little earth at its very edge.
            color = lerpColor(color, color * 0.72f, smoothstep(0.88f, 1.0f, t));
            color = lerpColor(color, srgb(112, 92, 68), smoothstep(0.985f, 1.0f, t) * 0.7f);
            mesh.vertices.push_back(
                {.position = glm::vec3(xz.x, h, xz.y) + kAlt, .uv = xz * 0.25f, .color = glm::vec4(color, 1.0f)});
            if (r == rings) rimY[std::size_t(s)] = h;
        }
    }
    // The rock below: from the rim down to a jagged point.
    for (int k = 1; k <= under; ++k) {
        const float s01 = float(k) / float(under);
        for (int s = 0; s <= segments; ++s) {
            const float a = kTau * float(s % segments) / float(segments);
            const glm::vec2 dir(std::cos(a), std::sin(a));
            const float edge = isle.outline(a);
            // Lumpy rock narrowing to a few hanging points, with ridges down its sides.
            const float n1 = fbm(dir * 2.2f + glm::vec2(s01 * 2.0f, float(d.seed % 13)), 3, d.seed + 17);
            const float n2 = fbm(dir * 1.7f + glm::vec2(float(d.seed % 7)), 3, d.seed + 19) * 0.5f + 0.5f;
            const float ridge = std::abs(fbm(glm::vec2(a * 4.0f, s01 * 0.7f), 2, d.seed + 21));
            const float bulge = k == 1 ? 1.03f : 1.0f;
            const float radius = edge * std::pow(1.0f - s01, 0.65f) *
                                 (1.0f + (0.1f + 0.25f * s01) * n1 - 0.08f * ridge * (0.3f + s01)) * bulge;
            const float y = rimY[std::size_t(s)] - 0.5f - d.depth * std::pow(s01, 1.1f) * (0.45f + 0.9f * n2 * n2) -
                            (k == 1 ? 0.6f : 0.0f);
            const glm::vec2 xz = d.centre + dir * radius;
            const float band = fbm(glm::vec2(y * 0.35f, a * 3.0f), 2, d.seed + 23) * 0.5f + 0.5f;
            glm::vec3 color = lerpColor(srgb(160, 142, 128), srgb(122, 110, 104), band);
            color = lerpColor(color, srgb(140, 132, 140), smoothstep(0.4f, 1.0f, s01) * 0.6f);
            color *= 1.0f - 0.25f * ridge;
            color = lerpColor(srgb(104, 84, 62), color, smoothstep(0.0f, 0.1f, s01));
            mesh.vertices.push_back({.position = glm::vec3(xz.x, y, xz.y) + kAlt,
                                     .uv = glm::vec2(a * d.radius * 0.25f, y * 0.25f),
                                     .color = glm::vec4(color, 1.0f)});
        }
    }
    const int rows = rings + 1 + under;
    for (int r = 0; r + 1 < rows; ++r)
        for (int s = 0; s < segments; ++s) {
            const auto a = std::uint32_t(r * (segments + 1) + s);
            const auto b = a + std::uint32_t(segments + 1);
            // Counter-clockwise seen from above on the top, from outside below.
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    computeNormals(mesh);
    // Seams at angle 0: the duplicated column shares its neighbour's normal.
    for (int r = 0; r < rows; ++r) {
        const auto first = std::size_t(r * (segments + 1));
        const glm::vec3 n =
            glm::normalize(mesh.vertices[first].normal + mesh.vertices[first + std::size_t(segments)].normal);
        mesh.vertices[first].normal = n;
        mesh.vertices[first + std::size_t(segments)].normal = n;
    }
    // The top's centre is one point repeated: its normals averaged.
    {
        glm::vec3 n(0.0f);
        for (int s = 0; s <= segments; ++s) n += mesh.vertices[std::size_t(s)].normal;
        for (int s = 0; s <= segments; ++s) mesh.vertices[std::size_t(s)].normal = glm::normalize(n);
    }
    computeTangents(mesh);
    const std::string name = std::format("isle {}", d.seed);
    Result<render::MeshId> id = scene.addMesh(mesh.desc(name));
    if (!id) {
        quench::log::error("game", "{}: {}", name, id.error());
        return;
    }
    scene.addInstance({.mesh = *id, .material = m_terrain});
    addStatic(physics, mesh);
}

void World::buildFoliage(render::Scene& scene, const Isle& isle, float density) {
    const IsleDesc& d = isle.desc;
    const float patch = 5.0f;
    const float step = 0.2f / std::sqrt(std::clamp(density, 0.2f, 1.5f));
    const int cells = int(std::ceil(d.radius * 1.3f / patch));
    Random random{d.seed * 2654435761u + 1u};
    const std::array<glm::vec3, 5> petals = {srgb(250, 248, 240), srgb(255, 214, 226), srgb(255, 236, 150),
                                             srgb(206, 222, 255), srgb(255, 250, 225)};
    for (int cz = -cells; cz < cells; ++cz)
        for (int cx = -cells; cx < cells; ++cx) {
            const glm::vec2 corner = d.centre + glm::vec2(float(cx), float(cz)) * patch;
            const glm::vec2 mid = corner + glm::vec2(patch * 0.5f);
            const std::optional<float> midH = isle.heightAt(mid);
            // Any part of the patch on the isle?
            bool any = midH.has_value();
            for (int k = 0; k < 4 && !any; ++k)
                any = isle.heightAt(corner + glm::vec2(float(k & 1), float(k >> 1)) * patch).has_value();
            if (!any) continue;
            const float originY = midH.value_or(d.base);
            const glm::vec3 origin(mid.x, originY, mid.y);
            MeshData nearMesh, farMesh;
            const int n = int(patch / step);
            for (int iz = 0; iz < n; ++iz)
                for (int ix = 0; ix < n; ++ix) {
                    const glm::vec2 p =
                        corner +
                        (glm::vec2(float(ix), float(iz)) + glm::vec2(random.uniform(), random.uniform())) * step;
                    const glm::vec2 rel = p - d.centre;
                    const float t = glm::length(rel) / isle.outline(std::atan2(rel.y, rel.x));
                    if (t > 0.93f || excluded(p)) continue;
                    const std::optional<float> h = isle.heightAt(p);
                    if (!h) continue;
                    const bool far = (ix % 2 == 0) && (iz % 2 == 0);
                    if (isle.slope(p) > 0.85f) continue;
                    const glm::vec3 base = isle.grassColor(p) * shade(p);
                    const float meadow = fbm(p / 9.0f, 2, d.seed + 29) * 0.5f + 0.5f;
                    const float tall = glm::mix(0.12f, 0.32f, smoothstep(0.2f, 0.8f, meadow)) *
                                       (1.0f - 0.5f * smoothstep(0.8f, 0.93f, t)) * glm::mix(0.3f, 1.0f, shade(p));
                    const glm::vec3 root(p.x - origin.x, *h - origin.y, p.y - origin.z);
                    const int blades = 5;
                    for (int b = 0; b < blades; ++b) {
                        const float a = random.uniform(0.0f, kTau);
                        const glm::vec2 dir(std::cos(a), std::sin(a));
                        const float height = tall * random.uniform(0.6f, 1.15f);
                        const float width = 0.024f * random.uniform(0.8f, 1.2f);
                        const float lean = random.uniform(0.05f, 0.35f) * height;
                        const glm::vec2 side(-dir.y, dir.x);
                        const glm::vec3 offset(dir.x * 0.04f * float(b), 0.0f, dir.y * 0.04f * float(b));
                        const glm::vec3 r0 = root + offset * 0.5f;
                        const glm::vec3 tip = r0 + glm::vec3(dir.x * lean, height, dir.y * lean);
                        const glm::vec3 tipColor =
                            base * glm::vec3(1.35f, 1.32f, 1.12f) + glm::vec3(0.03f, 0.03f, 0.01f);
                        const glm::vec3 rootColor = base * 0.6f;
                        const glm::vec3 up = glm::normalize(glm::vec3(dir.x * 0.25f, 1.0f, dir.y * 0.25f));
                        auto blade = [&](MeshData& m, float w) {
                            const auto first = std::uint32_t(m.vertices.size());
                            m.vertices.push_back({.position = r0 - glm::vec3(side.x, 0.0f, side.y) * w,
                                                  .normal = up,
                                                  .uv = {0.0f, r0.y},
                                                  .color = glm::vec4(rootColor, 1.0f)});
                            m.vertices.push_back({.position = r0 + glm::vec3(side.x, 0.0f, side.y) * w,
                                                  .normal = up,
                                                  .uv = {0.0f, r0.y},
                                                  .color = glm::vec4(rootColor, 1.0f)});
                            m.vertices.push_back({.position = tip,
                                                  .normal = up,
                                                  .uv = {1.0f, r0.y},
                                                  .color = glm::vec4(tipColor, 1.0f)});
                            m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
                        };
                        blade(nearMesh, width);
                        if (far && b < 3) blade(farMesh, width * 1.8f);
                    }
                    // Flowers in the flower fields.
                    const float field = fbm(p / 7.0f, 2, d.seed + 31) * 0.5f + 0.5f;
                    if (random.uniform() < d.flowers * (0.03f + 0.3f * smoothstep(0.45f, 0.7f, field))) {
                        const glm::vec3 color = petals[std::size_t(random.range(int(petals.size())))];
                        const float height = tall * random.uniform(1.0f, 1.5f) + 0.05f;
                        const float size = random.uniform(0.035f, 0.06f);
                        const glm::vec3 head =
                            root + glm::vec3(random.uniform(-0.03f, 0.03f), height, random.uniform(-0.03f, 0.03f));
                        auto flower = [&](MeshData& m) {
                            auto first = std::uint32_t(m.vertices.size());
                            // The stem.
                            m.vertices.push_back({.position = root + glm::vec3(-0.006f, 0.0f, 0.0f),
                                                  .uv = {0.0f, root.y},
                                                  .color = glm::vec4(base * 0.5f, 1.0f)});
                            m.vertices.push_back({.position = root + glm::vec3(0.006f, 0.0f, 0.0f),
                                                  .uv = {0.0f, root.y},
                                                  .color = glm::vec4(base * 0.5f, 1.0f)});
                            m.vertices.push_back(
                                {.position = head, .uv = {1.0f, root.y}, .color = glm::vec4(base * 1.2f, 1.0f)});
                            m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
                            // The head: five petals around a yellow eye, facing up.
                            first = std::uint32_t(m.vertices.size());
                            m.vertices.push_back({.position = head + glm::vec3(0.0f, 0.004f, 0.0f),
                                                  .normal = {0, 1, 0},
                                                  .uv = {3.0f, root.y},
                                                  .color = glm::vec4(srgb(255, 200, 60), 1.0f)});
                            const float spin = random.uniform(0.0f, kTau);
                            for (int k = 0; k < 10; ++k) {
                                const float a = spin + kTau * float(k) / 10.0f;
                                const float rr = (k % 2 == 0 ? 1.0f : 0.45f) * size;
                                m.vertices.push_back(
                                    {.position = head + glm::vec3(std::cos(a) * rr, 0.0f, std::sin(a) * rr),
                                     .normal = {0, 1, 0},
                                     .uv = {3.0f, root.y},
                                     .color = glm::vec4(color, 1.0f)});
                            }
                            for (int k = 0; k < 10; ++k)
                                m.indices.insert(m.indices.end(), {first, first + 1 + std::uint32_t((k + 1) % 10),
                                                                   first + 1 + std::uint32_t(k)});
                        };
                        flower(nearMesh);
                        if (far) flower(farMesh);
                    }
                }
            if (nearMesh.indices.empty()) continue;
            Patch pt;
            pt.centre = origin + kAlt;
            const glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin + kAlt);
            const glm::vec4 custom(0.35f, 1.0f, 0.012f, 0.0f);
            Result<render::MeshId> nearId = scene.addMesh(nearMesh.desc("grass"));
            if (!nearId) continue;
            pt.near = scene.addInstance({.mesh = *nearId,
                                         .material = m_grass,
                                         .transform = transform,
                                         .custom = custom,
                                         .castsShadows = false});
            if (!farMesh.indices.empty()) {
                if (Result<render::MeshId> farId = scene.addMesh(farMesh.desc("grass far")))
                    pt.far = scene.addInstance({.mesh = *farId,
                                                .material = m_grass,
                                                .transform = transform,
                                                .custom = custom,
                                                .visible = false,
                                                .castsShadows = false});
            }
            m_patches.push_back(pt);
        }
}

void World::buildRuins(render::Scene& scene, quench::PhysicsWorld& physics) {
    MeshData stones;
    Random random{97u};
    auto block = [&](glm::vec3 centre, glm::vec3 half, float yaw, bool collide = true) {
        MeshData b = render::boxMesh(half);
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), centre + kAlt) *
                            glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        append(stones, b, m, glm::vec4(stoneColor(random), 1.0f));
        if (collide) addBox(physics, centre + kAlt, half, yaw);
    };
    auto column = [&](glm::vec3 base, float height, float radius, bool broken) {
        std::vector<glm::vec2> profile = {{1e-4f, 0.0f},           {radius * 1.35f, 0.0f},
                                          {radius * 1.35f, 0.25f}, {radius * 1.1f, 0.32f},
                                          {radius, 0.45f},         {radius * 0.92f, height - 0.35f}};
        if (!broken) {
            profile.insert(profile.end(), {{radius * 1.25f, height - 0.2f}, {radius * 1.35f, height}, {1e-4f, height}});
        } else {
            profile.insert(profile.end(), {{radius * 0.9f, height}, {1e-4f, height + 0.05f}});
        }
        MeshData c = lathe(profile, 14);
        if (broken)
            for (Vertex& v : c.vertices)
                if (v.position.y > height - 0.4f)
                    v.position.y += 0.25f * std::sin(v.position.x * 30.0f) * std::cos(v.position.z * 23.0f);
        computeNormals(c);
        append(stones, c, glm::translate(glm::mat4(1.0f), base + kAlt), glm::vec4(stoneColor(random), 1.0f));
        addBox(physics, base + kAlt + glm::vec3(0.0f, height * 0.5f, 0.0f),
               glm::vec3(radius * 1.1f, height * 0.5f, radius * 1.1f), 0.0f);
    };
    auto groundY = [&](float x, float z) { return terrainHeight({x, z}).value_or(0.0f); };
    auto arch = [&](glm::vec3 centre, float yaw, float span, float height) {
        const glm::vec3 across(std::cos(yaw), 0.0f, -std::sin(yaw));
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 p = centre + across * (span * 0.5f) * side;
            block(glm::vec3(p.x, groundY(p.x, p.z) + height * 0.5f - 0.3f, p.z),
                  glm::vec3(0.32f, height * 0.5f + 0.3f, 0.38f), yaw);
        }
        // The curve: wedge blocks along a half circle.
        const int pieces = 9;
        const float radius = span * 0.5f;
        const float top = centre.y + height;
        for (int i = 0; i < pieces; ++i) {
            const float a = kPi * (float(i) + 0.5f) / float(pieces);
            const glm::vec3 p = centre + across * (std::cos(a) * radius) +
                                glm::vec3(0.0f, top - centre.y + std::sin(a) * radius * 0.8f, 0.0f);
            MeshData b = render::boxMesh(glm::vec3(0.36f, 0.22f, 0.4f));
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), p + kAlt) *
                                glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::rotate(glm::mat4(1.0f), a - kPi * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
            append(stones, b, m, glm::vec4(stoneColor(random), 1.0f));
        }
    };

    // The meadow: an arch over the way from the start, columns around the pond, a terrace with the first candle,
    // slabs up the hill and a ring of stones on its top.
    {
        const glm::vec3 c(2.0f, groundY(2.0f, -27.0f), -27.0f);
        arch(c, 0.0f, 3.6f, 2.6f);
    }
    for (int i = 0; i < 7; ++i) {
        const float a = kTau * float(i) / 7.0f + 0.3f;
        const glm::vec2 p = glm::vec2(-15.0f, -10.0f) + glm::vec2(std::cos(a), std::sin(a)) * 8.6f;
        const float h = random.uniform(1.0f, 3.6f);
        column(glm::vec3(p.x, groundY(p.x, p.y) - 0.15f, p.y), h, 0.3f, h < 3.0f);
    }
    // A fallen drum by the pond.
    {
        MeshData drum =
            lathe({{1e-4f, -0.5f}, {0.27f, -0.5f}, {0.3f, -0.47f}, {0.3f, 0.47f}, {0.27f, 0.5f}, {1e-4f, 0.5f}}, 14);
        computeNormals(drum);
        const glm::vec3 p(-8.0f, groundY(-8.0f, -15.0f) + 0.22f, -15.0f);
        append(stones, drum,
               glm::translate(glm::mat4(1.0f), p + kAlt) *
                   glm::rotate(glm::mat4(1.0f), 1.5f, glm::vec3(0.3f, 0.0f, 1.0f)),
               glm::vec4(stoneColor(random), 1.0f));
        addBox(physics, p + kAlt, glm::vec3(0.5f, 0.3f, 0.3f), 0.0f);
    }
    // The terrace.
    {
        const glm::vec3 c(4.0f, groundY(4.0f, -22.0f), -22.0f);
        block(c + glm::vec3(0.0f, -0.15f, 0.0f), glm::vec3(4.2f, 0.3f, 4.2f), 0.12f);
        for (int i = 0; i < 4; ++i) {
            const float a = 0.12f + kPi * 0.5f * float(i) + 0.785f;
            const glm::vec3 p = c + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 5.0f;
            block(glm::vec3(p.x, groundY(p.x, p.z) + 0.4f, p.z), glm::vec3(0.9f, 0.55f + 0.3f * float(i % 2), 0.3f),
                  -a + kPi * 0.5f);
        }
    }
    // Stone lanterns: a post, a hollow head on four corners, a roof.
    for (const glm::vec3& lantern : m_landmarks.lanterns) {
        const float y = lantern.y - 1.05f;
        block(glm::vec3(lantern.x, y + 0.42f, lantern.z), glm::vec3(0.16f, 0.5f, 0.16f), 0.2f);
        block(glm::vec3(lantern.x, y + 0.86f, lantern.z), glm::vec3(0.27f, 0.05f, 0.27f), 0.2f, false);
        for (int k = 0; k < 4; ++k) {
            const float a = 0.2f + kPi * 0.5f * float(k) + kPi * 0.25f;
            const glm::vec3 p =
                glm::vec3(lantern.x, y + 1.06f, lantern.z) + glm::vec3(std::cos(a), 0.0f, -std::sin(a)) * 0.29f;
            block(p, glm::vec3(0.045f, 0.16f, 0.045f), 0.2f, false);
        }
        MeshData roof = lathe({{1e-4f, 0.0f}, {0.42f, 0.0f}, {0.4f, 0.06f}, {1e-4f, 0.3f}}, 4);
        append(stones, roof,
               glm::translate(glm::mat4(1.0f), glm::vec3(lantern.x, y + 1.22f, lantern.z) + kAlt) *
                   glm::rotate(glm::mat4(1.0f), 0.2f + kPi * 0.25f, glm::vec3(0.0f, 1.0f, 0.0f)),
               glm::vec4(stoneColor(random), 1.0f));
    }
    // Candle pedestals.
    for (const glm::vec3& candle : m_landmarks.candles) {
        const float y = candle.y - 1.05f;
        block(glm::vec3(candle.x, y + 0.4f, candle.z), glm::vec3(0.22f, 0.4f, 0.22f), 0.3f);
        block(glm::vec3(candle.x, y + 0.85f, candle.z), glm::vec3(0.3f, 0.06f, 0.3f), 0.3f, false);
    }
    // Slabs up the hill.
    for (int i = 0; i < 14; ++i) {
        const float t = float(i) / 13.0f;
        const glm::vec2 p =
            glm::mix(glm::vec2(4.0f, 0.0f), glm::vec2(12.5f, 13.0f), t) + glm::vec2(std::sin(t * 6.0f) * 1.2f, 0.0f);
        block(glm::vec3(p.x, groundY(p.x, p.y) + 0.02f, p.y), glm::vec3(0.55f, 0.12f, 0.4f),
              random.uniform(-0.3f, 0.3f) + 0.6f);
    }
    for (int i = 0; i < 5; ++i) {
        const float a = kTau * float(i) / 5.0f;
        const glm::vec2 p = glm::vec2(14.0f, 16.0f) + glm::vec2(std::cos(a), std::sin(a)) * 3.2f;
        const float h = random.uniform(0.8f, 1.6f);
        block(glm::vec3(p.x, groundY(p.x, p.y) + h * 0.5f - 0.2f, p.y), glm::vec3(0.35f, h * 0.5f, 0.28f), -a);
    }
    // The spirit isle's arch and the updraft's three pillars.
    {
        const glm::vec3 c(-63.0f, groundY(-63.0f, 84.0f), 84.0f);
        arch(c, 0.5f, 3.2f, 2.3f);
    }
    for (int i = 0; i < 3; ++i) {
        const float a = kTau * float(i) / 3.0f + 0.5f;
        const glm::vec2 p = glm::vec2(-20.0f, 128.0f) + glm::vec2(std::cos(a), std::sin(a)) * 4.4f;
        column(glm::vec3(p.x, groundY(p.x, p.y) - 0.1f, p.y), 3.2f, 0.34f, false);
    }
    {
        const glm::vec3 c = m_landmarks.updraft;
        MeshData disc = lathe({{5.0f, -0.4f}, {5.0f, 0.11f}, {4.96f, 0.15f}, {1e-4f, 0.15f}}, 40);
        computeNormals(disc);
        append(stones, disc, glm::translate(glm::mat4(1.0f), c + kAlt + glm::vec3(0.0f, -0.05f, 0.0f)),
               glm::vec4(stoneColor(random), 1.0f));
        addBox(physics, c + kAlt + glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(3.6f, 0.15f, 3.6f), 0.0f);
    }
    // Ruins on the far isles, for the views: a broken arch, a row of columns, a lone pair.
    arch(glm::vec3(-122.0f, groundY(-122.0f, 34.0f), 34.0f), 1.1f, 4.0f, 3.2f);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 p(92.0f + float(i) * 2.6f, -58.0f + float(i) * 1.4f);
        column(glm::vec3(p.x, groundY(p.x, p.y) - 0.1f, p.y), 2.2f + 0.9f * float((i * 7) % 3), 0.32f, i % 2 == 1);
    }
    for (int i = 0; i < 2; ++i) {
        const glm::vec2 p(-88.0f + float(i) * 4.5f, -92.0f);
        column(glm::vec3(p.x, groundY(p.x, p.y) - 0.1f, p.y), 3.4f, 0.34f, i == 1);
    }
    // The stair around the mesa: from the meadow ring below the way from the updraft, up along the cliff and in over
    // a landing onto the top. Each step stands on its own block down to the ground.
    {
        const glm::vec2 mesa(10.0f, 218.0f);
        const float start = std::atan2(128.0f - 218.0f, -20.0f - 10.0f) + 0.5f;
        const int steps = 76;
        const float sweep = 2.7f;
        const float bottom = groundY(mesa.x + std::cos(start) * 25.0f, mesa.y + std::sin(start) * 25.0f);
        const float top = groundY(mesa.x, mesa.y);
        // The feet walk a smooth ramp through the steps' middles (the character would catch on each riser).
        MeshData ramp;
        for (int i = 0; i < steps; ++i) {
            const float t = float(i) / float(steps - 1);
            const float a = start + sweep * t;
            const glm::vec2 dir(std::cos(a), std::sin(a));
            const glm::vec2 xz = mesa + dir * 25.0f;
            const float y = glm::mix(bottom + 0.2f, top - 0.05f, t);
            const float floor = std::min(groundY(xz.x, xz.y), y) - 0.3f;
            block(glm::vec3(xz.x, (y + floor) * 0.5f, xz.y), glm::vec3(1.25f, (y - floor) * 0.5f, 0.55f), -a, false);
            const auto first = std::uint32_t(ramp.vertices.size());
            for (const float r : {23.75f, 26.25f}) {
                const glm::vec2 p = mesa + dir * r;
                ramp.vertices.push_back({.position = glm::vec3(p.x, y - 0.12f, p.y) + kAlt});
            }
            if (i > 0)
                ramp.indices.insert(ramp.indices.end(), {first - 2, first, first - 1, first - 1, first, first + 1});
        }
        addStatic(physics, ramp);
        const float a = start + sweep;
        for (int i = 0; i < 7; ++i) {
            const glm::vec2 xz = mesa + glm::vec2(std::cos(a), std::sin(a)) * (23.6f - float(i) * 1.4f);
            const float floor = std::min(groundY(xz.x, xz.y), top) - 0.3f;
            block(glm::vec3(xz.x, (top - 0.05f + floor) * 0.5f, xz.y),
                  glm::vec3(0.75f, (top - 0.05f - floor) * 0.5f, 1.3f), -a);
        }
    }
    // The cave's portal: two pillars and a lintel over its low mouth, half sunk into the mound.
    {
        const Landmarks& l = m_landmarks;
        const glm::vec2 out =
            glm::normalize(glm::vec2(l.caveEntrance.x - l.caveCentre.x, l.caveEntrance.z - l.caveCentre.z));
        const glm::vec2 across(-out.y, out.x);
        const float yaw = std::atan2(-out.x, -out.y);
        const glm::vec2 mid = glm::vec2(l.caveCentre.x, l.caveCentre.z) + out * (l.caveRadius + 0.3f);
        const float base = groundY(mid.x, mid.y);
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec2 p = mid + across * 3.7f * side;
            const float floor = std::min(groundY(p.x, p.y), base) - 0.4f;
            block(glm::vec3(p.x, (base + 2.8f + floor) * 0.5f, p.y),
                  glm::vec3(0.5f, (base + 2.8f - floor) * 0.5f, 0.6f), yaw);
        }
        block(glm::vec3(mid.x, base + 3.2f, mid.y), glm::vec3(4.5f, 0.42f, 0.7f), yaw);
        // A stone fallen from it, in the grass.
        const glm::vec2 fallen = mid + out * 2.6f + across * 4.6f;
        block(glm::vec3(fallen.x, groundY(fallen.x, fallen.y) + 0.15f, fallen.y), glm::vec3(1.1f, 0.35f, 0.5f),
              yaw + 0.5f);
    }
    // The temple: three round tiers, a ring of columns under a ring of stone, an altar.
    {
        // Steps of 0.35 m from the lowest ground under them, so that the player can walk up from anywhere.
        float low = m_landmarks.altar.y;
        for (int i = 0; i < 48; ++i)
            for (const float r : {0.0f, 5.0f, 9.0f, 11.2f}) {
                const float a = kTau * float(i) / 48.0f;
                low = std::min(low,
                               groundY(m_landmarks.altar.x + std::cos(a) * r, m_landmarks.altar.z + std::sin(a) * r));
            }
        const std::array<std::pair<float, float>, 5> tiers = {
            {{11.0f, 0.35f}, {9.8f, 0.7f}, {8.6f, 1.05f}, {7.4f, 1.4f}, {6.2f, 1.75f}}};
        m_landmarks.altar.y = low;
        const glm::vec3 c = m_landmarks.altar;
        for (const auto& [radius, top] : tiers) {
            // A closed drum: its side, a narrow bevel and its top.
            MeshData tier = lathe({{radius, -1.0f}, {radius, top - 0.04f}, {radius - 0.04f, top}, {1e-4f, top}}, 48);
            computeNormals(tier);
            append(stones, tier, glm::translate(glm::mat4(1.0f), c + kAlt), glm::vec4(stoneColor(random), 1.0f));
        }
        // The feet walk a smooth cone over the steps' edges (the character would catch on each riser).
        MeshData collider = lathe({{0.01f, -1.0f}, {11.4f, -1.0f}, {11.2f, -0.1f}, {6.2f, 1.75f}, {0.01f, 1.75f}}, 32);
        for (Vertex& v : collider.vertices) v.position += c + kAlt;
        addStatic(physics, collider);
        for (int i = 0; i < 10; ++i) {
            const float a = kTau * float(i) / 10.0f;
            column(c + glm::vec3(std::cos(a) * 5.4f, 1.75f, std::sin(a) * 5.4f), 6.2f, 0.4f, false);
        }
        MeshData ring = render::torusMesh(5.4f, 0.55f, 64, 12);
        for (Vertex& v : ring.vertices) v.position.y *= 0.6f;
        computeNormals(ring);
        append(stones, ring, glm::translate(glm::mat4(1.0f), c + kAlt + glm::vec3(0.0f, 8.25f, 0.0f)),
               glm::vec4(stoneColor(random), 1.0f));
        block(c + glm::vec3(0.0f, 2.15f, 0.0f), glm::vec3(0.8f, 0.4f, 0.8f), 0.0f);
        m_landmarks.altar.y = c.y + 1.75f;
        m_landmarks.beamBase = c + glm::vec3(0.0f, 2.55f, 0.0f);
    }

    computeTangents(stones);
    if (Result<render::MeshId> id = scene.addMesh(stones.desc("ruins")))
        scene.addInstance({.mesh = *id, .material = m_stoneMaterial});
}

float World::shade(glm::vec2 xz) const {
    const Landmarks& l = m_landmarks;
    const float d = glm::distance(xz, glm::vec2(l.caveCentre.x, l.caveCentre.z));
    float inside = 1.0f - smoothstep(l.caveRadius - 2.5f, l.caveRadius + 0.8f, d);
    // Brighter toward the mouth and under the holes.
    const glm::vec2 mouth(l.caveEntrance.x, l.caveEntrance.z);
    inside *= 0.45f + 0.55f * smoothstep(1.0f, 6.0f, glm::distance(xz, mouth));
    for (const glm::vec3& f : l.caveShafts)
        inside *= 0.4f + 0.6f * smoothstep(0.8f, 2.6f, glm::distance(xz, glm::vec2(f.x, f.z)));
    return 1.0f - 0.72f * inside;
}

void World::buildCave(render::Scene& scene, quench::PhysicsWorld& physics) {
    const Landmarks& l = m_landmarks;
    const glm::vec3 c = l.caveCentre;
    const float radius = l.caveRadius + 0.6f;
    const float height = l.caveHeight;
    const glm::vec2 toMouth = glm::normalize(glm::vec2(l.caveEntrance.x - c.x, l.caveEntrance.z - c.z));
    const float mouthAngle = std::atan2(toMouth.y, toMouth.x);
    constexpr int segments = 80;
    constexpr int rings = 20;
    // A dome of rock: rings from the ground up to the crown, lumpy, its foot sunk into the floor.
    MeshData shell;
    std::vector<glm::vec2> params;
    for (int r = 0; r <= rings; ++r) {
        const float phi = 0.5f * kPi * float(r) / float(rings);
        for (int k = 0; k <= segments; ++k) {
            const float a = kTau * float(k % segments) / float(segments);
            const glm::vec2 dir(std::cos(a), std::sin(a));
            const float lump = fbm(dir * 1.9f + glm::vec2(phi * 1.7f, 3.0f), 3, 211u);
            const float rr = radius * std::pow(std::max(std::cos(phi), 0.0f), 0.7f) * (1.0f + 0.14f * lump);
            const float y =
                c.y - 1.4f + height * std::sin(phi) * (1.0f + 0.1f * std::cos(phi) * fbm(dir * 2.3f + 7.0f, 2, 223u));
            const float band = fbm(glm::vec2(y * 0.6f, a * 5.0f), 2, 227u) * 0.5f + 0.5f;
            const glm::vec3 color = lerpColor(srgb(150, 140, 126), srgb(118, 112, 104), band);
            shell.vertices.push_back({.position = glm::vec3(c.x + dir.x * rr, y, c.z + dir.y * rr) + kAlt,
                                      .uv = glm::vec2(a * radius * 0.3f, y * 0.3f),
                                      .color = glm::vec4(color, 1.0f)});
            params.push_back({a, phi});
        }
    }
    // The mouth (an arch toward the meadow) and the holes the light comes through.
    auto open = [&](float a, float phi, glm::vec3 p) {
        const float da = wrapAngle(a - mouthAngle) / 0.3f;
        if (da * da + (phi / 0.62f) * (phi / 0.62f) < 1.0f) return true;
        if (phi > 0.45f)
            for (const glm::vec3& h : l.caveHoles)
                if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(h.x, h.z)) < 1.25f) return true;
        return false;
    };
    for (int r = 0; r < rings; ++r)
        for (int k = 0; k < segments; ++k) {
            const auto a = std::uint32_t(r * (segments + 1) + k);
            const auto b = a + std::uint32_t(segments + 1);
            const glm::vec3 mid = (shell.vertices[a].position + shell.vertices[b + 1].position) * 0.5f - kAlt;
            const glm::vec2 pa = (params[a] + params[b + 1]) * 0.5f;
            if (open(pa.x, pa.y, mid)) continue;
            // Counter-clockwise from outside.
            shell.indices.insert(shell.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    computeNormals(shell);
    computeTangents(shell);
    if (Result<render::MeshId> id = scene.addMesh(shell.desc("cave")))
        scene.addInstance({.mesh = *id, .material = m_caveMaterial});
    addStatic(physics, shell);

    // Glowing caps along the inside of the wall.
    Random random{229u};
    MeshData caps;
    for (int i = 0; i < 26; ++i) {
        const float a = mouthAngle + 0.5f + random.uniform(0.0f, kTau - 1.0f);
        const float rr = radius * random.uniform(0.62f, 0.86f);
        const glm::vec2 xz = glm::vec2(c.x, c.z) + glm::vec2(std::cos(a), std::sin(a)) * rr;
        const float y = terrainHeight(xz).value_or(c.y);
        const float h = random.uniform(0.08f, 0.22f);
        MeshData stem = render::cylinderMesh(0.012f, h * 0.5f, 6);
        append(caps, stem, glm::translate(glm::mat4(1.0f), glm::vec3(xz.x, y + h * 0.5f, xz.y) + kAlt),
               glm::vec4(0.5f, 0.6f, 0.55f, 1.0f));
        MeshData cap = ellipsoid(glm::vec3(0.05f, 0.025f, 0.05f) * random.uniform(0.8f, 1.6f), 5, 8);
        append(caps, cap, glm::translate(glm::mat4(1.0f), glm::vec3(xz.x, y + h, xz.y) + kAlt), glm::vec4(1.0f));
    }
    if (Result<render::MeshId> id = scene.addMesh(caps.desc("glow caps")))
        scene.addInstance({.mesh = *id, .material = m_glowCap, .castsShadows = false});
}

void World::addLights(render::Scene& scene) {
    const Landmarks& l = m_landmarks;
    for (const glm::vec3& p : l.lanterns)
        scene.addLight({.type = render::LightType::Point,
                        .color = glm::vec3(1.0f, 0.62f, 0.3f),
                        .intensity = 3.0f,
                        .position = p + kAlt,
                        .range = 7.0f,
                        .castsShadows = false});
    for (std::size_t i = 0; i < l.caveHoles.size(); ++i) {
        const glm::vec3 from = l.caveHoles[i] + glm::vec3(-0.4f, 1.8f, 0.2f);
        const glm::vec3 dir = glm::normalize(l.caveShafts[i] - from);
        scene.addLight({.type = render::LightType::Spot,
                        .color = glm::vec3(1.0f, 0.9f, 0.7f),
                        .intensity = 10.0f,
                        .position = from + kAlt,
                        .direction = dir,
                        .range = 14.0f,
                        .innerAngle = 0.1f,
                        .outerAngle = 0.2f,
                        .castsShadows = false});
    }
    // The cave's own dim green light, as from the moss.
    scene.addLight({.type = render::LightType::Point,
                    .color = glm::vec3(0.45f, 0.85f, 0.6f),
                    .intensity = 5.0f,
                    .position = l.caveCentre + glm::vec3(0.0f, 3.0f, 0.0f) + kAlt,
                    .range = 15.0f,
                    .castsShadows = false});
}

Result<void> World::build(render::Scene& scene, quench::PhysicsWorld& physics, render::SurfaceId world,
                          render::SurfaceId foliage, float grassDensity) {
    m_worldSurface = world;
    m_foliageSurface = foliage;
    render::MaterialDesc terrain{.surface = world, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.92f};
    terrain.params[0] = glm::vec4(0.0f);
    m_terrain = scene.addMaterial(terrain);
    render::MaterialDesc stone{.surface = world, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.85f};
    stone.params[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    m_stoneMaterial = scene.addMaterial(stone);
    m_grassDesc = {.surface = foliage, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.75f, .doubleSided = true};
    m_grassDesc.params[0] = glm::vec4(0.0f, -1000.0f, 0.0f, 1000.0f);
    m_grass = scene.addMaterial(m_grassDesc);
    m_water = scene.addMaterial({.baseColor = {0.03f, 0.075f, 0.08f, 1.0f}, .roughness = 0.03f, .water = true});
    m_flame = scene.addMaterial(
        {.baseColor = {1.0f, 0.8f, 0.5f, 1.0f}, .emissive = {14.0f, 7.0f, 2.4f}, .shadows = render::ShadowMode::None});
    m_flameOff = scene.addMaterial({.baseColor = {0.9f, 0.86f, 0.78f, 1.0f}, .roughness = 0.6f});

    render::MaterialDesc cave{
        .surface = world, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.9f, .doubleSided = true};
    cave.params[0] = glm::vec4(3.0f, 0.0f, 0.0f, 0.0f);
    m_caveMaterial = scene.addMaterial(cave);
    m_glowCap = scene.addMaterial(
        {.baseColor = {0.5f, 0.9f, 0.8f, 1.0f}, .emissive = {0.9f, 2.6f, 2.0f}, .shadows = render::ShadowMode::None});

    for (const Isle& isle : m_isles) buildIsle(scene, physics, isle);
    for (const Isle& isle : m_isles) buildFoliage(scene, isle, grassDensity);
    buildRuins(scene, physics);
    buildCave(scene, physics);
    addLights(scene);

    // The pond's water.
    {
        const Landmarks& l = m_landmarks;
        const float level = terrainHeight({l.pond.x + l.pondRadius, l.pond.z}).value_or(l.pond.y) - 0.25f;
        MeshData disc = render::diskMesh(l.pondRadius + 0.6f, 64);
        MeshData water;
        append(water, disc, glm::translate(glm::mat4(1.0f), glm::vec3(l.pond.x, level, l.pond.z) + kAlt));
        if (Result<render::MeshId> id = scene.addMesh(water.desc("pond")))
            scene.addInstance({.mesh = *id, .material = m_water, .castsShadows = false});
        m_landmarks.pond.y = level;
    }
    // Candles: wax and the flame (shown once lit).
    {
        MeshData wax = render::cylinderMesh(0.06f, 0.12f, 12);
        MeshData flame = ellipsoid({0.035f, 0.08f, 0.035f}, 8, 10);
        Result<render::MeshId> waxId = scene.addMesh(wax.desc("candle"));
        Result<render::MeshId> flameId = scene.addMesh(flame.desc("flame"));
        if (waxId && flameId)
            for (const glm::vec3& c : m_landmarks.candles) {
                scene.addInstance(
                    {.mesh = *waxId,
                     .material = m_flameOff,
                     .transform = glm::translate(glm::mat4(1.0f), c + kAlt + glm::vec3(0.0f, -0.12f, 0.0f))});
                m_flames.push_back(scene.addInstance(
                    {.mesh = *flameId,
                     .material = m_flame,
                     .transform = glm::translate(glm::mat4(1.0f), c + kAlt + glm::vec3(0.0f, 0.07f, 0.0f)),
                     .visible = false,
                     .castsShadows = false}));
            }
    }
    // The lanterns' flames.
    {
        MeshData flame = ellipsoid({0.07f, 0.11f, 0.07f}, 8, 10);
        if (Result<render::MeshId> id = scene.addMesh(flame.desc("lantern flame")))
            for (const glm::vec3& c : m_landmarks.lanterns)
                scene.addInstance({.mesh = *id,
                                   .material = m_flame,
                                   .transform = glm::translate(glm::mat4(1.0f), c + kAlt),
                                   .castsShadows = false});
    }
    quench::log::info("game", "world: {} isles, {} grass patches", m_isles.size(), m_patches.size());
    return {};
}

void World::updateDetail(render::Scene& scene, glm::vec3 camera) {
    for (Patch& p : m_patches) {
        const bool near =
            glm::distance(glm::vec2(p.centre.x, p.centre.z), glm::vec2(camera.x, camera.z)) < m_detailDistance ||
            !scene.contains(p.far);
        if (near == p.nearShown) continue;
        p.nearShown = near;
        scene.setVisible(p.near, near);
        if (scene.contains(p.far)) scene.setVisible(p.far, !near);
    }
}

void World::setCallRing(render::Scene& scene, glm::vec3 centre, float age) {
    m_grassDesc.params[0] = glm::vec4(centre, age);
    scene.setMaterial(m_grass, m_grassDesc);
}

void World::setCandleLit(render::Scene& scene, int candle, bool lit) {
    if (candle >= 0 && std::size_t(candle) < m_flames.size()) scene.setVisible(m_flames[std::size_t(candle)], lit);
}

} // namespace cloudsong
