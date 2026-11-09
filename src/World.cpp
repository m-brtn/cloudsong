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
    const glm::vec2 delta = xz - b.at;
    const glm::vec2 local = glm::vec2(delta.x * std::cos(b.yaw) + delta.y * std::sin(b.yaw),
                                      -delta.x * std::sin(b.yaw) + delta.y * std::cos(b.yaw)) /
                            b.stretch;
    float radius = b.radius;
    if (b.jagged > 0.0f) {
        const glm::vec2 dir = glm::normalize(local + glm::vec2(1e-5f));
        radius *= 1.0f + b.jagged * fbm(dir * 2.4f + b.at * 0.01f, 3, 97u);
    }
    const float d = glm::length(local) / radius;
    if (d >= 1.0f) return 0.0f;
    const float t = std::clamp((d - b.flat) / std::max(1.0f - b.flat, 1e-3f), 0.0f, 1.0f);
    if (b.flat > .3f && b.height > 5.0f) {
        // Broad eroded shelves give tall mesas readable rock planes and grassy
        // landings; the profile remains continuous for the walkable mesh.
        return b.height * (1.0f - .18f * smoothstep(0.0f, .20f, t) - .47f * smoothstep(.24f, .52f, t) -
                           .35f * smoothstep(.60f, 1.0f, t));
    }
    return b.height * 0.5f * (1.0f + std::cos(kPi * t));
}

// Route grading belongs to the upper walkable surface. Isles overlap in horizontal
// projection; do not lift a lower island or duplicate its plants onto the one above it.
bool isTopSurface(const std::vector<Isle>& isles, const Isle& surface, glm::vec2 p, float height) {
    for (const auto& other : isles) {
        if (other.desc.seed == surface.desc.seed) continue;
        const auto offset = p - other.desc.centre;
        const float bound = other.desc.radius * 1.3f;
        if (glm::dot(offset, offset) > bound * bound) continue;
        if (const auto h = other.heightAt(p); h && *h > height + .01f) return false;
    }
    return true;
}

glm::vec3 stoneColor(Random& r) {
    const float v = r.uniform(0.85f, 1.1f);
    return srgb(180, 193, 187) * v * glm::vec3(1.0f, r.uniform(0.97f, 1.02f), r.uniform(0.94f, 1.0f));
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
    glm::vec3 c = lerpColor(srgb(68, 140, 89), srgb(117, 177, 112), smoothstep(0.3f, 0.75f, a));
    c = lerpColor(c, srgb(55, 126, 95), smoothstep(0.62f, 0.95f, b) * 0.6f);
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

    // Side routes open the region out into a broad, explorable archipelago. Their terraces are sculpted
    // terrain with matching physics, not scenery cards. The original pilgrimage remains in the middle.
    const std::vector<IsleDesc> region = {
        {{-98, 0},
         -2,
         46,
         3.5f,
         38,
         101u,
         {{{-104, 28}, 24, 13, .48f, .06f}, {{-123, -12}, 19, 8, .15f}, {{-79, 15}, 14, 4}},
         .75f},
        {{-163, 82}, 8, 40, 3, 52, 103u, {{{-171, 97}, 22, 16, .48f, .08f}, {{-144, 72}, 16, 5, .2f}}, .85f},
        {{-104, 152}, 23, 34, 2.5f, 48, 107u, {{{-110, 161}, 20, 11, .42f, .08f}}, .7f},
        {{137, 63}, 9, 36, 2.5f, 45, 109u, {{{143, 75}, 20, 10, .35f, .05f}, {{120, 48}, 12, 4}}, .9f},
        {{196, 142}, 24, 43, 3, 65, 113u, {{{202, 155}, 25, 21, .55f, .08f}, {{175, 132}, 16, 5}}, .65f},
        {{133, 242}, 43, 36, 2.5f, 56, 127u, {{{133, 250}, 21, 17, .5f, .06f}}, .65f}};
    for (auto desc : region) {
        // Broad asymmetric ridges, with smaller shoulders; each landmark has a different silhouette.
        if (desc.seed == 101u) {
            desc.bumps[0].stretch = {1.22f, .86f};
            desc.bumps[0].yaw = -.45f;
            desc.bumps.push_back({{-116, 21}, 16, 3, .15f, .18f, {1.5f, .7f}, -.5f});
        }
        if (desc.seed == 103u) {
            desc.bumps[0].stretch = {1.35f, .65f};
            desc.bumps[0].yaw = .4f;
            desc.bumps[0].flat = .3f;
            desc.bumps.push_back({{-184, 95}, 17, 6, .1f, .2f, {.8f, 1.2f}, 0});
        }
        if (desc.seed == 107u) {
            desc.bumps[0].stretch = {.75f, 1.3f};
            desc.bumps[0].flat = .25f;
        }
        if (desc.seed == 109u) {
            desc.bumps[0].stretch = {1.4f, .7f};
            desc.bumps[0].yaw = -.6f;
        }
        if (desc.seed == 113u) {
            desc.bumps[0].stretch = {1.25f, .85f};
            desc.bumps[0].yaw = .6f;
            desc.bumps.push_back({{186, 163}, 16, 7, .2f, .16f, {.7f, 1.2f}, .3f});
        }
        if (desc.seed == 127u) {
            desc.bumps[0].stretch = {.85f, 1.15f};
            desc.bumps[0].flat = .3f;
        }
        m_isles.push_back({std::move(desc)});
    }
    for (int i = 0; i < 24; ++i) {
        const float t = float(i) / 23;
        m_meadowSteps.push_back(glm::mix(glm::vec2(4, 0), glm::vec2(12.5f, 13), t) +
                                glm::vec2(std::sin(t * 6) * 1.2f, 0));
    }
    layoutPaths();

    Landmarks& l = m_landmarks;
    auto ground = [&](float x, float z, float above) {
        const std::optional<float> h = terrainHeight({x, z});
        return glm::vec3(x, (h ? *h : 0.0f) + above, z);
    };
    l.start = ground(2.0f, -36.0f, 0.0f);
    l.startYaw = 0.0f;
    l.pond = ground(-15.0f, -10.0f, 0.0f);
    l.pondRadius = 6.6f;
    l.cottage = ground(64, 63, 0);
    // A new cave is carved into the eastern mountain, separate from the old cave isle's house.
    const float caveFloor = terrainHeight({202, 132}).value_or(25);
    l.caveCentre = {202, caveFloor, 142};
    l.caveRadius = 5;
    l.caveHeight = 4.2f;
    l.caveEntrance = {202, caveFloor, 133};
    l.lights = {ground(-6.0f, -27.0f, 1.4f),    ground(18.0f, -10.0f, 1.5f),     ground(-31.0f, 4.0f, 1.5f),
                ground(14.0f, 16.0f, 2.2f),     ground(-10.0f, 22.0f, 1.4f),     glm::vec3(36.0f, 12.0f, 36.0f),
                ground(52.0f, 42.0f, 1.5f),     ground(76.0f, 70.0f, 1.5f),      ground(64.5f, 64.0f, 1.6f),
                glm::vec3(-28.4f, 3.4f, 36.2f), glm::vec3(-50.0f, 5.0f, 68.0f),  ground(-58.0f, 96.0f, 1.5f),
                ground(-24.0f, 122.0f, 1.5f),   glm::vec3(-6.0f, 97.0f, 160.0f), ground(4.0f, 178.0f, 1.6f),
                ground(15.0f, 211.0f, 1.6f)};
    // A candle before each spirit: the call lights it and wakes the spirit.
    l.candles = {ground(-20.4f, -15.2f, 1.05f), ground(64.2f, 64.9f, 1.25f), ground(-65.2f, 88.2f, 1.05f)};
    l.lanterns = {ground(5.0f, -33.0f, 1.05f), ground(-33.5f, 42.5f, 1.05f), ground(-14.5f, 123.0f, 1.05f)};
    l.spirits = {ground(-21.5f, -16.0f, 0.0f), ground(62.0f, 65.0f, .53f), ground(-66.0f, 87.0f, 0.0f)};
    l.spiritYaw = {0.9f, 2.5f, 0.6f};
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
    layoutGarden();
    layoutChimes();
    m_stone = {{{4.0f, -22.0f}, 5.0f}, {{-20.0f, 128.0f}, 5.5f}, {{10.0f, 218.0f}, 9.0f}};
    m_clear.emplace_back(glm::vec2(202, 132), 4.1f);
    m_stone.emplace_back(glm::vec2(202, 132), 4.1f);
    for (const auto pad : m_foundations) {
        m_clear.emplace_back(glm::vec2(pad.x, pad.y), pad.z - .5f);
        m_stone.emplace_back(glm::vec2(pad.x, pad.y), pad.z - .5f);
    }
}

std::optional<float> World::terrainHeight(glm::vec2 xz) const {
    std::optional<float> best;
    for (const Isle& isle : m_isles)
        if (const std::optional<float> h = isle.heightAt(xz); h && (!best || *h > *best)) best = h;
    if (best) *best = gradedHeight(xz, *best);
    return best;
}

float World::terrainSlope(glm::vec2 p) const {
    constexpr float e = .3f;
    const auto x0 = terrainHeight(p - glm::vec2(e, 0)), x1 = terrainHeight(p + glm::vec2(e, 0));
    const auto z0 = terrainHeight(p - glm::vec2(0, e)), z1 = terrainHeight(p + glm::vec2(0, e));
    return x0 && x1 && z0 && z1 ? glm::length(glm::vec2(*x1 - *x0, *z1 - *z0)) / (2 * e) : 0.0f;
}

Ground World::groundAt(glm::vec3 local) const {
    if (onPath({local.x, local.z})) return Ground::Stone;
    for (const auto& [c, r] : m_stone)
        if (glm::distance(glm::vec2(local.x, local.z), c) < r) return Ground::Stone;
    const std::optional<float> h = terrainHeight({local.x, local.z});
    if (h && local.y < *h + 0.6f) return Ground::Grass;
    return Ground::Stone;
}

bool World::excluded(glm::vec2 xz) const {
    if (const auto h = terrainHeight(xz); h && caveCut({xz.x, *h, xz.y}) < .12f) return true;
    for (const auto& [c, r] : m_clear)
        if (glm::distance(xz, c) < r) return true;
    return onPath(xz);
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
    // The expanded walkable region; world to local.
    const glm::vec3 m = (a + b + c) / 3.0f - kAlt;
    if (m.x < -210.0f || m.x > 250.0f || m.z < -130.0f || m.z > 285.0f) return;
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
    // Half-metre radial spacing preserves the graded shoulders at a player's eye level.
    // The same triangles are submitted to physics and navigation.
    const int segments = std::max(64, int(d.radius * 9.0f));
    const int rings = std::max(20, int(d.radius * 2.0f));
    MeshData mesh;
    std::vector<float> rimY(std::size_t(segments + 1));
    // The top: rings of the outline from the centre out.
    for (int r = 0; r <= rings; ++r) {
        const float t = float(r) / float(rings);
        for (int s = 0; s <= segments; ++s) {
            const float a = kTau * float(s % segments) / float(segments);
            const float edge = isle.outline(a);
            const glm::vec2 xz = d.centre + glm::vec2(std::cos(a), std::sin(a)) * edge * std::min(t, 0.9995f);
            const float natural = isle.heightAt(xz).value_or(d.base - 1.6f);
            const float h = isTopSurface(m_isles, isle, xz, natural) ? gradedHeight(xz, natural) : natural;
            glm::vec3 color = isle.grassColor(xz);
            // Steep ground shows its rock.
            const float road = pathCoverage(xz);
            const float rock = smoothstep(0.7f, 1.05f, isle.slope(xz)) * (1.0f - smoothstep(-.1f, .15f, road));
            const float strata = fbm(glm::vec2(h * 0.8f, (xz.x + xz.y) * 0.05f), 2, d.seed + 41) * 0.5f + 0.5f;
            color = lerpColor(color, lerpColor(srgb(116, 148, 161), srgb(88, 117, 137), strata), rock);
            color *= shade(xz);
            // A little darker over the lip, where the grass bends over the edge.
            color = lerpColor(color, color * 0.62f, smoothstep(0.9f, 1.0f, t));
            mesh.vertices.push_back(
                {.position = glm::vec3(xz.x, h, xz.y) + kAlt, .uv = {road, -10.0f}, .color = glm::vec4(color, 1.0f)});
            if (r == rings) rimY[std::size_t(s)] = h;
        }
    }
    // The rock below, a mesh of its own (the rock's look): a rounded lip of grass and earth, then soft stone that
    // narrows in steps (ledges on part of the way round) to a few hanging points. The profile's knots are fractions of
    // the rim's radius and of the depth; each angle moves them a little.
    struct Knot {
        float r, d;
    };
    static constexpr std::array<Knot, 12> kProfile = {{{1.0f, 0.0f},
                                                       {1.008f, 0.012f},
                                                       {.99f, .040f},
                                                       {.96f, .14f},
                                                       {.93f, .27f},
                                                       {.83f, .30f},
                                                       {.80f, .43f},
                                                       {.73f, .56f},
                                                       {.60f, .60f},
                                                       {.46f, .73f},
                                                       {.24f, .80f},
                                                       {0.0f, .83f}}};
    constexpr int kSteps = 3; // rings between two knots
    MeshData rock;
    const int rockRows = int(kProfile.size() - 1) * kSteps + 1;
    for (int k = 0; k < rockRows; ++k) {
        const int knot = k / kSteps;
        const float f = float(k % kSteps) / float(kSteps);
        for (int s = 0; s <= segments; ++s) {
            const float a = kTau * float(s % segments) / float(segments);
            const glm::vec2 dir(std::cos(a), std::sin(a));
            const float edge = isle.outline(a);
            const float ledges =
                smoothstep(-0.25f, 0.35f, fbm(dir * 1.3f + glm::vec2(float(d.seed % 11)), 2, d.seed + 5));
            const float hang = fbm(dir * 1.7f + glm::vec2(float(d.seed % 7)), 3, d.seed + 19) * 0.5f + 0.5f;
            auto at = [&](int i) {
                i = std::clamp(i, 0, int(kProfile.size()) - 1);
                Knot q = kProfile[std::size_t(i)];
                // Without a ledge here the step's inner knot sits on the line between its neighbours.
                if (i == 5) q.r = glm::mix(.91f, q.r, ledges);
                if (i == 8) q.r = glm::mix(.70f, q.r, ledges);
                // Lower down, some angles hang far deeper than others.
                // Local ledges erode unevenly but converge to one closed rounded keel.
                q.d += .07f * (hang - .5f) * std::sin(kPi * q.d / .83f);
                return glm::vec2(q.r, q.d);
            };
            // Catmull-Rom through the knots.
            const glm::vec2 p0 = at(knot - 1), p1 = at(knot), p2 = at(knot + 1), p3 = at(knot + 2);
            const float f2 = f * f, f3 = f2 * f;
            const glm::vec2 q = 0.5f * (2.0f * p1 + (p2 - p0) * f + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f2 +
                                        (3.0f * p1 - p0 - 3.0f * p2 + p3) * f3);
            const float s01 = float(k) / float(rockRows - 1);
            // Lumps that change with the depth; rounded buttresses down the sides, closer together lower down.
            const float lump = fbm(dir * 2.6f + glm::vec2(q.y * 3.2f, float(d.seed % 13)), 3, d.seed + 17);
            const float knob = fbm(dir * 7.0f + glm::vec2(q.y * 9.0f, 5.0f), 2, d.seed + 27);
            const float flute = std::abs(fbm(dir * (6.0f + 4.0f * s01) + glm::vec2(q.y * 1.2f), 2, d.seed + 21));
            const float buttress = 1.0f - flute;
            // The first rock ring is exactly the turf boundary. Noise grows below the seam;
            // it must never move the side away from the top and leave a daylight crack.
            const float radius =
                k == 0 ? edge * .9995f
                       : edge * std::max(q.x, 0.0f) *
                             (1.0f + smoothstep(0.0f, .12f, s01) * ((.04f + .14f * s01) * lump + .03f * knob * s01 +
                                                                    .07f * (buttress - .6f) * s01));
            // Every vertex of the last ring is the same point: varying its height
            // by angle used to leave long needle-shaped triangles under an isle.
            const float crown = glm::mix(rimY[std::size_t(s)], d.base - 1.0f, smoothstep(.45f, .83f, q.y));
            const float y = crown - d.depth * q.y;
            const glm::vec2 xz = d.centre + dir * radius;
            // Grass over the lip, a band of earth, then stone: warm above, cooler and lilac below, in broad patches,
            // the hollows between the buttresses darker.
            const float metres = d.depth * q.y;
            const float patch = fbm(dir * 2.0f + glm::vec2(q.y * 2.5f, 3.0f), 2, d.seed + 23) * 0.5f + 0.5f;
            glm::vec3 color = lerpColor(srgb(120, 150, 160), srgb(96, 126, 144), patch);
            color = lerpColor(color, srgb(97, 111, 145), smoothstep(0.35f, 0.95f, q.y) * 0.75f);
            color *= glm::mix(0.78f, 1.06f, buttress);
            color = lerpColor(srgb(69, 100, 96), color, smoothstep(0.9f, 2.2f, metres));
            color = lerpColor(isle.grassColor(xz) * 0.62f, color, smoothstep(0.25f, 0.8f, metres));
            rock.vertices.push_back({.position = glm::vec3(xz.x, y, xz.y) + kAlt,
                                     .uv = glm::vec2(0.0f, q.y),
                                     .color = glm::vec4(color, 1.0f)});
        }
    }
    // Both grids: rings of segments + 1 vertices (the last column repeats the first).
    auto finish = [&](MeshData& m, int rows, bool closedCentre, const std::string& name, render::MaterialId material) {
        for (int r = 0; r + 1 < rows; ++r)
            for (int s = 0; s < segments; ++s) {
                const auto a = std::uint32_t(r * (segments + 1) + s);
                const auto b = a + std::uint32_t(segments + 1);
                // Counter-clockwise seen from above on the top, from outside below.
                m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        computeNormals(m);
        // Seams at angle 0: the duplicated column shares its neighbour's normal.
        for (int r = 0; r < rows; ++r) {
            const auto first = std::size_t(r * (segments + 1));
            const glm::vec3 n =
                glm::normalize(m.vertices[first].normal + m.vertices[first + std::size_t(segments)].normal);
            m.vertices[first].normal = n;
            m.vertices[first + std::size_t(segments)].normal = n;
        }
        // The top's centre is one point repeated: its normals averaged.
        if (closedCentre) {
            glm::vec3 n(0.0f);
            for (int s = 0; s <= segments; ++s) n += m.vertices[std::size_t(s)].normal;
            for (int s = 0; s <= segments; ++s) m.vertices[std::size_t(s)].normal = glm::normalize(n);
        }
        if (closedCentre) {
            // Smooth normals of the height field remove the polar grid's alternating
            // triangular scallops along steep grassy ledges. Keep graded road normals.
            constexpr float e = .35f;
            for (auto& v : m.vertices) {
                const glm::vec2 p(v.position.x, v.position.z);
                if (pathCoverage(p) > -1.5f) continue;
                const auto x0 = isle.heightAt(p - glm::vec2(e, 0)), x1 = isle.heightAt(p + glm::vec2(e, 0));
                const auto z0 = isle.heightAt(p - glm::vec2(0, e)), z1 = isle.heightAt(p + glm::vec2(0, e));
                if (x0 && x1 && z0 && z1) {
                    bool pad = false;
                    for (auto f : m_foundations)
                        if (glm::distance(p, glm::vec2(f.x, f.y)) < f.z + 4) pad = true;
                    if (!pad) v.normal = glm::normalize(glm::vec3(*x0 - *x1, 2 * e, *z0 - *z1));
                }
            }
        }
        computeTangents(m);
        if (closedCentre && d.seed == 113u) {
            // Clip the terrain at the same implicit tunnel used by its walls. The
            // physics sees the opening too; no dark decal hides an intact hillside.
            // Locally refine faces before cutting: a long face can cross the
            // tunnel even when all three original corners are outside it.
            std::vector<std::uint32_t> refined;
            auto midpoint = [&](const Vertex& a, const Vertex& b) {
                Vertex v = a;
                v.position = (a.position + b.position) * .5f;
                const glm::vec2 p(v.position.x, v.position.z);
                if (const auto h = isle.heightAt(p)) v.position.y = kAltitude + gradedHeight(p, *h);
                v.normal = glm::normalize(a.normal + b.normal);
                v.uv = (a.uv + b.uv) * .5f;
                v.color = (a.color + b.color) * .5f;
                return v;
            };
            auto split = [&](auto&& self, Vertex a, Vertex b, Vertex c, int depth) -> void {
                const auto low = glm::min(glm::min(a.position, b.position), c.position) - kAlt;
                const auto high = glm::max(glm::max(a.position, b.position), c.position) - kAlt;
                const bool near = high.x > 197 && low.x < 207 && high.z > 130 && low.z < 149;
                if (near && depth < 6 &&
                    std::max({glm::distance(a.position, b.position), glm::distance(b.position, c.position),
                              glm::distance(c.position, a.position)}) > .18f) {
                    auto ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                    self(self, a, ab, ca, depth + 1);
                    self(self, ab, b, bc, depth + 1);
                    self(self, ca, bc, c, depth + 1);
                    self(self, ab, bc, ca, depth + 1);
                } else {
                    auto n = std::uint32_t(m.vertices.size());
                    m.vertices.insert(m.vertices.end(), {a, b, c});
                    refined.insert(refined.end(), {n, n + 1, n + 2});
                }
            };
            for (std::size_t i = 0; i < m.indices.size(); i += 3)
                split(split, m.vertices[m.indices[i]], m.vertices[m.indices[i + 1]], m.vertices[m.indices[i + 2]], 0);
            m.indices = std::move(refined);
            std::vector<std::uint32_t> indices;
            for (std::size_t i = 0; i < m.indices.size(); i += 3) {
                std::vector<Vertex> polygon;
                for (int k = 0; k < 3; ++k) {
                    const auto a = m.vertices[m.indices[i + k]], b = m.vertices[m.indices[i + (k + 1) % 3]];
                    const float fa = caveCut(a.position - kAlt), fb = caveCut(b.position - kAlt);
                    if (fa >= 0) polygon.push_back(a);
                    if ((fa >= 0) != (fb >= 0)) {
                        float low = 0, high = 1;
                        for (int j = 0; j < 15; ++j) {
                            const float t = (low + high) * .5f;
                            if ((caveCut(glm::mix(a.position, b.position, t) - kAlt) >= 0) == (fa >= 0))
                                low = t;
                            else
                                high = t;
                        }
                        const float t = (low + high) * .5f;
                        Vertex v = a;
                        v.position = glm::mix(a.position, b.position, t);
                        v.normal = glm::normalize(glm::mix(a.normal, b.normal, t));
                        v.uv = glm::mix(a.uv, b.uv, t);
                        v.color = glm::mix(a.color, b.color, t);
                        polygon.push_back(v);
                    }
                }
                if (polygon.size() == 3 && caveCut(m.vertices[m.indices[i]].position - kAlt) >= 0 &&
                    caveCut(m.vertices[m.indices[i + 1]].position - kAlt) >= 0 &&
                    caveCut(m.vertices[m.indices[i + 2]].position - kAlt) >= 0) {
                    indices.insert(indices.end(), m.indices.begin() + i, m.indices.begin() + i + 3);
                    continue;
                }
                const auto start = std::uint32_t(m.vertices.size());
                m.vertices.insert(m.vertices.end(), polygon.begin(), polygon.end());
                for (std::uint32_t k = 1; k + 1 < polygon.size(); ++k)
                    indices.insert(indices.end(), {start, start + k, start + k + 1});
            }
            m.indices = std::move(indices);
        }
        Result<render::MeshId> id = scene.addMesh(m.desc(name));
        if (!id) {
            quench::log::error("game", "{}: {}", name, id.error());
            return;
        }
        scene.addInstance({.mesh = *id, .material = material});
        addStatic(physics, m);
    };
    finish(mesh, rings + 1, true, std::format("isle {}", d.seed), m_terrain);
    finish(rock, rockRows, false, std::format("isle {} rock", d.seed), m_rock);
    buildHanging(scene, isle, rimY);
}

void World::buildHanging(render::Scene& scene, const Isle& isle, const std::vector<float>& rimY) {
    // Tufts of grass over the rim and roots hanging from it, swaying a little (the foliage's look; their root
    // height above each vertex keeps the wind's sway off, so they hang still).
    const IsleDesc& d = isle.desc;
    const int segments = int(rimY.size()) - 1;
    Random random{d.seed * 747796405u + 3u};
    MeshData m;
    const float circumference = kTau * d.radius;
    const int tufts = int(circumference / 0.45f);
    auto strip = [&](glm::vec3 top, glm::vec3 bottom, glm::vec3 side, glm::vec3 topColor, glm::vec3 bottomColor) {
        const auto first = std::uint32_t(m.vertices.size());
        const glm::vec3 n = glm::normalize(glm::cross(side, bottom - top));
        for (const glm::vec3& p : {top - side, top + side})
            m.vertices.push_back(
                {.position = p, .normal = n, .uv = {0.0f, top.y + 1.0f}, .color = glm::vec4(topColor, 1.0f)});
        m.vertices.push_back(
            {.position = bottom, .normal = n, .uv = {1.0f, top.y + 1.0f}, .color = glm::vec4(bottomColor, 1.0f)});
        m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
    };
    for (int t = 0; t < tufts; ++t) {
        const float a = kTau * (float(t) + random.uniform()) / float(tufts);
        const glm::vec2 dir(std::cos(a), std::sin(a));
        const float edge = isle.outline(a);
        const float fs = a / kTau * float(segments);
        const int s0 = std::clamp(int(fs), 0, segments - 1);
        const float y = glm::mix(rimY[std::size_t(s0)], rimY[std::size_t(s0 + 1)], fs - float(s0));
        const glm::vec2 xz = d.centre + dir * edge * .999f;
        const glm::vec3 rim = glm::vec3(xz.x, y - 0.05f, xz.y) + kAlt;
        const glm::vec3 out(dir.x, 0.0f, dir.y);
        const glm::vec3 across(-dir.y, 0.0f, dir.x);
        const glm::vec3 green = isle.grassColor(xz);
        // Tufts in clumps, some long, gaps between: blades that curl over the lip and hang.
        const float clump = fbm(dir * 11.0f + glm::vec2(float(d.seed % 5)), 2, d.seed + 33) * 0.5f + 0.5f;
        const int blades = int(float(2 + random.range(5)) * smoothstep(0.3f, 0.7f, clump) + 0.5f);
        for (int b = 0; b < blades; ++b) {
            const glm::vec3 root = rim + across * random.uniform(-0.25f, 0.25f);
            const float length = random.uniform(0.15f, 0.5f) * (0.6f + clump);
            const glm::vec3 tip = root + out * random.uniform(0.06f, 0.2f) + glm::vec3(0.0f, -length, 0.0f) +
                                  across * random.uniform(-0.15f, 0.15f);
            strip(root, tip, across * random.uniform(0.02f, 0.04f), green * 0.55f, green * 0.85f);
        }
        // Now and then a root or a vine, longer, that hangs below the lip.
        if (random.uniform() < 0.04f) {
            const bool vine = random.uniform() < 0.6f;
            const float length = random.uniform(.45f, vine ? 1.4f : .9f);
            const int pieces = int(length / 0.4f) + 1;
            const glm::vec3 color = vine ? green * 0.75f : srgb(104, 82, 62);
            glm::vec3 from = rim + out * 0.05f;
            for (int k = 0; k < pieces; ++k) {
                const float sway = std::sin(float(k) * 1.3f + a * 17.0f) * 0.06f;
                const glm::vec3 to = from + glm::vec3(0.0f, -length / float(pieces), 0.0f) + across * sway +
                                     out * (k < 2 ? 0.08f : 0.0f);
                const float width = (vine ? 0.03f : 0.025f) * (1.0f - 0.6f * float(k) / float(pieces));
                const auto first = std::uint32_t(m.vertices.size());
                for (const glm::vec3& p :
                     {from - across * width, from + across * width, to - across * width, to + across * width})
                    m.vertices.push_back({.position = p,
                                          .normal = out,
                                          .uv = {0.0f, rim.y + 1.0f},
                                          .color = glm::vec4(color * (p.y < from.y - 0.01f ? 1.05f : 1.0f), 1.0f)});
                m.indices.insert(m.indices.end(), {first, first + 2, first + 1, first + 1, first + 2, first + 3});
                // Leaves along a vine.
                if (vine && k % 2 == 1) {
                    const glm::vec3 leaf = to + across * random.uniform(-1.0f, 1.0f) * 0.12f;
                    strip(to, leaf + glm::vec3(0.0f, -0.12f, 0.0f) + out * 0.04f, across * 0.05f, green * 0.7f,
                          green * glm::vec3(1.1f, 1.2f, 0.9f));
                }
                from = to;
            }
        }
    }
    if (m.indices.empty()) return;
    if (Result<render::MeshId> id = scene.addMesh(m.desc(std::format("isle {} lip", d.seed))))
        scene.addInstance({.mesh = *id, .material = m_grass, .castsShadows = false});
}

void World::buildFoliage(render::Scene& scene, const Isle& isle, float density) {
    const IsleDesc& d = isle.desc;
    auto surfaceHeight = [&](glm::vec2 p) -> std::optional<float> {
        auto h = isle.heightAt(p);
        if (h && isTopSurface(m_isles, isle, p, *h)) *h = gradedHeight(p, *h);
        return h;
    };
    auto surfaceSlope = [&](glm::vec2 p) {
        constexpr float e = .3f;
        const auto x0 = surfaceHeight(p - glm::vec2(e, 0)), x1 = surfaceHeight(p + glm::vec2(e, 0));
        const auto z0 = surfaceHeight(p - glm::vec2(0, e)), z1 = surfaceHeight(p + glm::vec2(0, e));
        return x0 && x1 && z0 && z1 ? glm::length(glm::vec2(*x1 - *x0, *z1 - *z0)) / (2 * e) : 0.0f;
    };
    const float patch = 5.0f;
    const float step = 0.2f / std::sqrt(std::clamp(density, 0.08f, 1.5f));
    const int cells = int(std::ceil(d.radius * 1.3f / patch));
    Random random{d.seed * 2654435761u + 1u};
    const std::array<glm::vec3, 5> petals = {srgb(250, 248, 240), srgb(255, 214, 226), srgb(255, 236, 150),
                                             srgb(206, 222, 255), srgb(255, 250, 225)};
    for (int cz = -cells; cz < cells; ++cz)
        for (int cx = -cells; cx < cells; ++cx) {
            const glm::vec2 corner = d.centre + glm::vec2(float(cx), float(cz)) * patch;
            const glm::vec2 mid = corner + glm::vec2(patch * 0.5f);
            const std::optional<float> midH = surfaceHeight(mid);
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
                    if (t > 0.93f) continue;
                    const auto natural = isle.heightAt(p);
                    if (!natural) continue;
                    const bool top = isTopSurface(m_isles, isle, p, *natural);
                    if (top && excluded(p)) continue;
                    const std::optional<float> h = top ? gradedHeight(p, *natural) : *natural;
                    const bool far = (ix % 2 == 0) && (iz % 2 == 0);
                    if (surfaceSlope(p) > 0.85f) continue;
                    const glm::vec3 base = isle.grassColor(p) * shade(p);
                    const float meadow = fbm(p / 9.0f, 2, d.seed + 29) * 0.5f + 0.5f;
                    const float tall = glm::mix(0.09f, 0.24f, smoothstep(0.2f, 0.8f, meadow)) *
                                       (1.0f - 0.5f * smoothstep(0.8f, 0.93f, t)) * glm::mix(0.3f, 1.0f, shade(p));
                    const glm::vec3 root(p.x - origin.x, *h - origin.y, p.y - origin.z);
                    // Broad quiet areas between tufts: the green ground carries the meadow at distance.
                    const float cover = smoothstep(0.3f, 0.68f, meadow);
                    const int blades = random.uniform() < glm::mix(0.18f, 0.8f, cover) ? 3 : 0;
                    for (int b = 0; b < blades; ++b) {
                        const float a = random.uniform(0.0f, kTau);
                        const glm::vec2 dir(std::cos(a), std::sin(a));
                        const float height = tall * random.uniform(0.6f, 1.15f);
                        const float width = 0.008f * random.uniform(0.8f, 1.2f);
                        const float lean = random.uniform(0.05f, 0.35f) * height;
                        const glm::vec2 side(-dir.y, dir.x);
                        const glm::vec3 offset(dir.x * 0.04f * float(b), 0.0f, dir.y * 0.04f * float(b));
                        const glm::vec3 r0 = root + offset * 0.5f;
                        const glm::vec3 tip = r0 + glm::vec3(dir.x * lean, height, dir.y * lean);
                        const glm::vec3 tipColor =
                            base * glm::vec3(1.12f, 1.2f, 1.16f) + glm::vec3(0.008f, 0.02f, 0.014f);
                        const glm::vec3 rootColor = base * 0.85f;
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
                            const glm::vec3 middle =
                                r0 + glm::vec3(dir.x * lean * 0.3f, height * 0.6f, dir.y * lean * 0.3f);
                            for (const float sign : {-1.0f, 1.0f})
                                m.vertices.push_back(
                                    {.position = middle + glm::vec3(side.x, 0.0f, side.y) * w * 0.72f * sign,
                                     .normal = up,
                                     .uv = {0.6f, r0.y},
                                     .color = glm::vec4(glm::mix(rootColor, tipColor, 0.6f), 1.0f)});
                            m.vertices.push_back({.position = tip,
                                                  .normal = up,
                                                  .uv = {1.0f, r0.y},
                                                  .color = glm::vec4(tipColor, 1.0f)});
                            m.indices.insert(m.indices.end(), {first, first + 1, first + 2, first + 1, first + 3,
                                                               first + 2, first + 2, first + 3, first + 4});
                        };
                        blade(nearMesh, width);
                        if (far && b < 3) blade(farMesh, width * 1.25f);
                    }
                    // Flowers in the flower fields.
                    const float field = fbm(p / 7.0f, 2, d.seed + 31) * 0.5f + 0.5f;
                    if (random.uniform() < d.flowers * (0.65f * smoothstep(0.63f, 0.78f, field))) {
                        // Related colours within a field, with occasional pale flowers at its edge.
                        const float palette = fbm(p / 15.0f, 2, d.seed + 49) * 0.5f + 0.5f;
                        const glm::vec3 color =
                            (d.seed == 103u ? glm::mix(srgb(124, 225, 226), srgb(179, 188, 255), palette)
                                            : petals[std::size_t(std::clamp(int(palette * 5.0f), 0, 4))]);
                        const float height = tall * random.uniform(1.0f, 1.5f) + 0.05f;
                        const float size = random.uniform(0.035f, 0.065f);
                        const float spin = random.uniform(0.0f, kTau);
                        const glm::vec3 headNormal =
                            glm::normalize(glm::vec3(random.uniform(-0.5f, 0.5f), 1.0f, random.uniform(-0.5f, 0.5f)));
                        const glm::vec3 headX = glm::normalize(glm::cross(headNormal, glm::vec3(0, 0, 1)));
                        const glm::vec3 headZ = glm::cross(headX, headNormal);
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
                            // A shallow four-lobed cup: rounded petals, not the old five-point star.
                            first = std::uint32_t(m.vertices.size());
                            m.vertices.push_back({.position = head - headNormal * size * 0.18f,
                                                  .normal = headNormal,
                                                  .uv = {3.0f, root.y},
                                                  .color = glm::vec4(color * 0.82f, 1.0f)});
                            constexpr int segments = 16;
                            for (int k = 0; k < segments; ++k) {
                                const float a = spin + kTau * float(k) / float(segments);
                                const float rr = size * (0.82f + 0.18f * std::cos(4.0f * (a - spin)));
                                m.vertices.push_back(
                                    {.position = head + (headX * std::cos(a) + headZ * std::sin(a)) * rr,
                                     .normal = headNormal,
                                     .uv = {3.0f, root.y},
                                     .color = glm::vec4(color, 1.0f)});
                            }
                            for (int k = 0; k < segments; ++k)
                                m.indices.insert(m.indices.end(), {first, first + 1 + std::uint32_t((k + 1) % segments),
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
            const glm::vec4 custom(0.35f, 1.0f, 0.012f, d.seed == 103u ? 1.4f : 0.0f);
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
            column(glm::vec3(p.x, groundY(p.x, p.z) - .3f, p.z), height + .3f, .32f, false);
        }
        // Curved masonry with rounded cross-section edges: continuous arches instead of overlapping boxes.
        const int pieces = 9, slices = 5;
        const float radius = span * 0.5f;
        const glm::vec2 section[] = {{-0.22f, -0.31f}, {-0.16f, -0.38f}, {0.16f, -0.38f}, {0.22f, -0.31f},
                                     {0.22f, 0.31f},   {0.16f, 0.38f},   {-0.16f, 0.38f}, {-0.22f, 0.31f}};
        for (int i = 0; i < pieces; ++i) {
            MeshData wedge;
            for (int j = 0; j <= slices; ++j) {
                const float a = kPi * (float(i) + glm::mix(0.008f, 0.992f, float(j) / float(slices))) / float(pieces);
                for (const glm::vec2 q : section)
                    wedge.vertices.push_back({.position = {(radius + q.x) * std::cos(a),
                                                           height + (radius + q.x) * std::sin(a) * 0.8f, q.y}});
            }
            for (int j = 0; j < slices; ++j)
                for (int k = 0; k < 8; ++k) {
                    const auto a = std::uint32_t(j * 8 + k), b = std::uint32_t(j * 8 + (k + 1) % 8);
                    wedge.indices.insert(wedge.indices.end(), {a, b, b + 8, a, b + 8, a + 8});
                }
            for (int k = 1; k < 7; ++k) {
                wedge.indices.insert(wedge.indices.end(), {0, std::uint32_t(k + 1), std::uint32_t(k)});
                const auto end = std::uint32_t(slices * 8);
                wedge.indices.insert(wedge.indices.end(), {end, end + std::uint32_t(k), end + std::uint32_t(k + 1)});
            }
            for (std::size_t k = 0; k < wedge.indices.size(); k += 3)
                std::swap(wedge.indices[k + 1], wedge.indices[k + 2]);
            computeNormals(wedge);
            const glm::mat4 m =
                glm::translate(glm::mat4(1.0f), centre + kAlt) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
            append(stones, wedge, m, glm::vec4(stoneColor(random), 1.0f));
        }
    };

    // The carved starting ruin replaces the old approach arch. Keep the first
    // vista open instead of stacking another monumental doorway in front of it.
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
    // The starting terrace is an imported carved ruin.
    // The lantern housings are imported; flames and their lights remain in the game.
    // Candle pedestals.
    for (const glm::vec3& candle : m_landmarks.candles) {
        if (glm::distance(glm::vec2(candle.x, candle.z), glm::vec2(m_landmarks.cottage.x, m_landmarks.cottage.z)) < 4)
            continue;
        const float y = candle.y - 1.05f;
        block(glm::vec3(candle.x, y + 0.4f, candle.z), glm::vec3(0.22f, 0.4f, 0.22f), 0.3f);
        block(glm::vec3(candle.x, y + 0.85f, candle.z), glm::vec3(0.3f, 0.06f, 0.3f), 0.3f, false);
    }
    // Worn stone stair: low chamfered risers, aligned with the climb and
    // seated below the terrain. The same centreline clears and colours the path.
    for (std::size_t i = 0; i < m_meadowSteps.size(); ++i) {
        const auto p = m_meadowSteps[i];
        const auto direction = m_meadowSteps[std::min(i + 1, m_meadowSteps.size() - 1)] - m_meadowSteps[i ? i - 1 : i];
        const float yaw = std::atan2(direction.x, direction.y);
        const float top = groundY(p.x, p.y) + .09f;
        float bottom = top - .24f;
        for (float x : {-1.0f, 1.0f})
            for (float z : {-.45f, .45f}) {
                const auto q =
                    p + glm::vec2(std::cos(yaw) * x + std::sin(yaw) * z, -std::sin(yaw) * x + std::cos(yaw) * z);
                bottom = std::min(bottom, groundY(q.x, q.y) - .10f);
            }
        MeshData step;
        append(step, chamferedBlock({.90f + .025f * float(i % 3), (top - bottom) * .5f, .42f}, .055f),
               placed(glm::vec3(p.x, (top + bottom) * .5f, p.y) + kAlt, yaw), glm::vec4(srgb(169, 181, 174), 1));
        append(stones, step);
        addStatic(physics, step);
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
    // Two low approach steps support the sanctuary model; its own mesh supplies the inner floor.
    {
        // Steps of 0.35 m from the lowest ground under them, so that the player can walk up from anywhere.
        float low = m_landmarks.altar.y;
        for (int i = 0; i < 48; ++i)
            for (const float r : {0.0f, 5.0f, 9.0f, 11.2f}) {
                const float a = kTau * float(i) / 48.0f;
                low = std::min(low,
                               groundY(m_landmarks.altar.x + std::cos(a) * r, m_landmarks.altar.z + std::sin(a) * r));
            }
        const std::array<std::pair<float, float>, 2> tiers = {{{11.0f, .28f}, {9.8f, .56f}}};
        m_landmarks.altar.y = low;
        const glm::vec3 c = m_landmarks.altar;
        for (const auto& [radius, top] : tiers) {
            // A closed drum: its side, a narrow bevel and its top.
            MeshData tier = lathe({{radius, -1.0f}, {radius, top - 0.04f}, {radius - 0.04f, top}, {1e-4f, top}}, 48);
            computeNormals(tier);
            append(stones, tier, glm::translate(glm::mat4(1.0f), c + kAlt), glm::vec4(stoneColor(random), 1.0f));
            for (Vertex& v : tier.vertices) v.position += c + kAlt;
            addStatic(physics, tier);
        }
        block(c + glm::vec3(0.0f, 2.15f, 0.0f), glm::vec3(0.8f, 0.4f, 0.8f), 0.0f);
        m_landmarks.altar.y = c.y + 1.75f;
        m_landmarks.beamBase = c + glm::vec3(0.0f, 2.55f, 0.0f);
    }

    computeTangents(stones);
    if (Result<render::MeshId> id = scene.addMesh(stones.desc("ruins")))
        scene.addInstance({.mesh = *id, .material = m_stoneMaterial});
}

float World::shade(glm::vec2) const { return 1.0f; }

void World::addLights(render::Scene& scene) {
    const Landmarks& l = m_landmarks;
    for (const glm::vec3& p : l.lanterns)
        scene.addLight({.type = render::LightType::Point,
                        .color = glm::vec3(1.0f, 0.62f, 0.3f),
                        .intensity = 3.0f,
                        .position = p + kAlt,
                        .range = 7.0f,
                        .castsShadows = false});
    // Real local light inside the cottage and cool reflected glow in the fossil chamber.
    scene.addLight({.type = render::LightType::Point,
                    .color = {1.0f, .67f, .36f},
                    .intensity = 2.2f,
                    .position = l.cottage + glm::vec3(1.45f, 1.4f, 1.95f) + kAlt,
                    .range = 5.5f,
                    .castsShadows = true});
    scene.addLight({.type = render::LightType::Point,
                    .color = {.18f, .8f, .72f},
                    .intensity = 1.8f,
                    .position = l.caveCentre + glm::vec3(0, 2, 1) + kAlt,
                    .range = 7.0f,
                    .castsShadows = true});
}

Result<void> World::build(render::Scene& scene, quench::PhysicsWorld& physics, render::SurfaceId world,
                          render::SurfaceId foliage, float grassDensity) {
    m_worldSurface = world;
    m_foliageSurface = foliage;
    render::MaterialDesc terrain{.surface = world, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.92f};
    terrain.params[0] = glm::vec4(0.0f);
    m_terrain = scene.addMaterial(terrain);
    render::MaterialDesc rock{.surface = world, .baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .roughness = 0.9f};
    rock.params[0] = glm::vec4(4.0f, 0.0f, 0.0f, 0.0f);
    m_rock = scene.addMaterial(rock);
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
    for (const Isle& isle : m_isles) buildFoliage(scene, isle, grassDensity * (isle.desc.seed >= 101u ? 0.24f : 1.0f));
    buildRuins(scene, physics);
    buildCottage(scene, physics);
    buildCave(scene, physics);
    buildRegion(scene, physics);
    addLights(scene);

    // Small tangible ornaments, separate from the floating lights that grow the cape.
    {
        auto ring = scene.addMesh(render::torusMesh(.20f, .025f, 32, 8).desc("wind chime rim"));
        auto seed = scene.addMesh(ellipsoid({.085f, .13f, .045f}, 12, 16).desc("wind chime seed"));
        const auto rimMaterial = scene.addMaterial(
            {.baseColor = {.84f, .57f, .17f, 1}, .emissive = {.18f, .09f, .015f}, .roughness = .35f, .metallic = .55f});
        const auto seedMaterial =
            scene.addMaterial({.baseColor = {.19f, .65f, .58f, 1}, .emissive = {.12f, .45f, .30f}, .roughness = .48f});
        if (!ring || !seed) return quench::Error{"could not create wind chime meshes"};
        for (std::size_t i = 0; i < m_landmarks.chimes.size(); ++i)
            m_chimes.push_back({scene.addInstance({.mesh = *ring, .material = rimMaterial}),
                                scene.addInstance({.mesh = *seed, .material = seedMaterial})});
    }

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
        const float distance = glm::distance(glm::vec2(p.centre.x, p.centre.z), glm::vec2(camera.x, camera.z));
        const int detail = distance > 115.0f ? 0 : (distance < m_detailDistance || !scene.contains(p.far) ? 2 : 1);
        if (detail == p.detail) continue;
        p.detail = detail;
        scene.setVisible(p.near, detail == 2);
        if (scene.contains(p.far)) scene.setVisible(p.far, detail == 1);
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

namespace cloudsong {
void World::setChime(render::Scene& scene, std::size_t index, float age, float time) {
    if (index >= m_chimes.size()) return;
    const auto centre = m_landmarks.chimes[index] + kAlt + glm::vec3(0, .10f * std::sin(time * 1.4f + float(index)), 0);
    const float scale = age < 0 ? 1.0f : std::max(0.0f, 1.0f - age / .35f);
    auto transform = glm::translate(glm::mat4(1), centre + glm::vec3(0, age < 0 ? 0 : age * .7f, 0)) *
                     glm::rotate(glm::mat4(1), time * .7f + float(index), glm::vec3(0, 1, 0)) *
                     glm::scale(glm::mat4(1), glm::vec3(std::max(scale, .001f)));
    scene.setTransform(m_chimes[index][0], transform * glm::rotate(glm::mat4(1), kPi * .5f, glm::vec3(1, 0, 0)));
    scene.setTransform(m_chimes[index][1], transform);
    for (auto instance : m_chimes[index]) scene.setVisible(instance, scale > 0);
}
} // namespace cloudsong
