#include "World.h"
namespace cloudsong {
namespace render = quench::render;
namespace {
const glm::vec3 alt(0, kAltitude, 0);
float caveSeal(float z) { return 1 - .99f * smoothstep(145.5f, 148, z); }
float caveRadius(float z) { return (2.65f + 1.55f * smoothstep(135, 141, z)) * caveSeal(z); }
float caveRoofRadius(float z) { return glm::mix(3.3f, 2.6f, smoothstep(134, 139, z)) * caveSeal(z); }
float caveRoofCentre(float z) { return glm::mix(2.0f, 1.6f, smoothstep(134, 139, z)); }
} // namespace
float World::caveCut(glm::vec3 p) const {
    const auto c = m_landmarks.caveCentre;
    if (p.z < 131 || p.z > 148 || std::abs(p.x - c.x) > 4.3f) return 1;
    const float r = caveRadius(p.z);
    const glm::vec2 q((p.x - c.x) / r, (p.y - c.y - caveRoofCentre(p.z)) / caveRoofRadius(p.z));
    return std::max({glm::dot(q, q) - 1.0f, 131 - p.z, p.z - 148, c.y - .02f - p.y});
}
void World::buildCave(render::Scene& scene, quench::PhysicsWorld& physics) {
    MeshData rock, floor;
    const auto c = m_landmarks.caveCentre;
    constexpr int rings = 136, sides = 72;
    for (int j = 0; j <= rings; ++j)
        for (int k = 0; k <= sides; ++k) {
            const float z = 131 + 17 * float(j) / rings, a = kTau * float(k) / sides;
            const float radius = caveRadius(z);
            const glm::vec3 p(c.x + std::cos(a) * radius, c.y + caveRoofCentre(z) + std::sin(a) * caveRoofRadius(z), z);
            rock.vertices.push_back({.position = p + alt, .color = glm::vec4(srgb(68, 107, 121), 1)});
        }
    for (int j = 0; j < rings; ++j)
        for (int k = 0; k < sides; ++k) {
            const auto a = std::uint32_t(j * (sides + 1) + k), b = a + sides + 1;
            for (const auto tri : std::array<std::array<std::uint32_t, 3>, 2>{{{{a, b, a + 1}}, {{a + 1, b, b + 1}}}}) {
                // The cave wall is inside the mountain. Do not expose a tube on the meadow.
                const auto p =
                    (rock.vertices[tri[0]].position + rock.vertices[tri[1]].position + rock.vertices[tri[2]].position) /
                        3.0f -
                    alt;
                if (p.y > terrainHeight({p.x, p.z}).value_or(-100) + .3f || p.y < c.y - .10f) continue;
                rock.indices.insert(rock.indices.end(), tri.begin(), tri.end());
            }
        }
    // A shallow stone threshold is buried into the intact meadow. It is level,
    // not a bridge or ramp, and uses stone shading rather than the walls' moss.
    for (int j = 0; j < 72; ++j) {
        const float z = 130.0f + float(j) * .25f;
        const auto a = std::uint32_t(floor.vertices.size());
        for (const auto q : std::array<glm::vec2, 4>{{{-1, 0}, {1, 0}, {1, .25f}, {-1, .25f}}}) {
            const float atZ = z + q.y;
            const float r = caveRadius(atZ) * .90f;
            floor.vertices.push_back(
                {.position =
                     glm::vec3(c.x + q.x * r, c.y + glm::mix(-.06f, .045f, smoothstep(130, 131.5f, atZ)), atZ) + alt,
                 .color = glm::vec4(srgb(136, 153, 156), 1)});
        }
        floor.indices.insert(floor.indices.end(), {a, a + 2, a + 1, a, a + 3, a + 2});
    }
    computeNormals(floor);
    computeTangents(floor);
    if (auto id = scene.addMesh(floor.desc("cave stone threshold and floor")))
        scene.addInstance({.mesh = *id, .material = m_stoneMaterial});
    addStatic(physics, floor);
    Random random{4107};
    for (int i = 0; i < 19; ++i) {
        const float z = random.uniform(136, 144.5f), x = random.uniform(-1.8f, 1.8f),
                    length = random.uniform(.45f, 1.55f);
        const float top = c.y + caveRoofCentre(z) +
                          caveRoofRadius(z) * std::sqrt(std::max(0.0f, 1 - x * x / (caveRadius(z) * caveRadius(z))));
        auto stalactite =
            lathe({{.001f, 0}, {.035f, length * .16f}, {.09f, length * .62f}, {.16f, length}, {.001f, length}}, 12);
        append(rock, stalactite, placed(glm::vec3(c.x + x, top - length, z) + alt), glm::vec4(srgb(100, 144, 151), 1));
    }
    for (int i = 0; i < 7; ++i) {
        const float z = 137 + float(i) * 1.05f, x = (i % 2 ? 1 : -1) * caveRadius(z) * .79f;
        auto stalagmite = lathe({{.001f, 0}, {.35f, 0}, {.23f, .45f}, {.11f, .95f}, {.001f, 1.2f}}, 12);
        append(rock, stalagmite, placed(glm::vec3(c.x + x, c.y, z) + alt), glm::vec4(srgb(90, 135, 145), 1));
    }
    computeNormals(rock);
    computeTangents(rock);
    if (auto id = scene.addMesh(rock.desc("fossil cave walls and stalactites")))
        scene.addInstance({.mesh = *id, .material = m_caveMaterial});
    addStatic(physics, rock);
}
void World::buildCottage(render::Scene& scene, quench::PhysicsWorld& physics) {
    MeshData garden;
    const auto c = m_landmarks.cottage;
    // Three low risers continue the wooden porch. Collision uses these same
    // steps, so there is no ramp hiding the entry or lifting the feet above it.
    for (int i = 0; i < 3; ++i) {
        const float top = .17f * float(i + 1);
        const auto block = chamferedBlock({1.12f - .06f * i, top * .5f, .39f}, .035f);
        MeshData step;
        append(step, block, placed(c + alt + glm::vec3(1.35f, top * .5f, -3.75f + .50f * i)),
               glm::vec4(srgb(150, 116, 78), 1));
        append(garden, step);
        addStatic(physics, step);
    }
    // Low stone flower beds frame the porch; the open middle remains a doorway.
    for (auto p : std::array<glm::vec2, 2>{{{-3.4f, -3.4f}, {4.2f, -2.0f}}}) {
        const auto at = c + glm::vec3(p.x, 0, p.y);
        const auto bed = lathe({{.68f, 0}, {.82f, 0}, {.82f, .35f}, {.68f, .35f}, {.68f, 0}}, 32);
        append(garden, bed, placed(at + alt), glm::vec4(srgb(165, 186, 177), 1));
        append(garden, render::diskMesh(.67f, 32), placed(at + alt + glm::vec3(0, .28f, 0)),
               glm::vec4(srgb(53, 88, 70), 1));
        MeshData col;
        append(col, bed, placed(at + alt));
        addStatic(physics, col);
    }
    for (const auto p : std::array<glm::vec3, 3>{{{-3.5f, 0, .2f}, {-3.5f, 0, -.7f}, {3.4f, 0, -4.0f}}}) {
        const auto pot = lathe({{.001f, 0},
                                {.18f, 0},
                                {.27f, .1f},
                                {.32f, .46f},
                                {.35f, .48f},
                                {.35f, .55f},
                                {.27f, .55f},
                                {.27f, .47f},
                                {.001f, .47f}},
                               32);
        append(garden, pot, placed(c + p + alt), glm::vec4(srgb(104, 155, 154), 1));
    }
    // The cottage candle stands on a narrow wooden table beside the bench.
    const auto candle = m_landmarks.candles[1];
    for (float dx : {-.20f, .20f})
        append(garden, chamferedBlock({.045f, .29f, .16f}, .012f),
               placed(glm::vec3(candle.x + dx, c.y + .82f, candle.z) + alt), glm::vec4(srgb(131, 94, 63), 1));
    append(garden, chamferedBlock({.31f, .045f, .24f}, .018f), placed(glm::vec3(candle.x, c.y + 1.16f, candle.z) + alt),
           glm::vec4(srgb(158, 122, 83), 1));
    computeNormals(garden);
    computeTangents(garden);
    if (auto id = scene.addMesh(garden.desc("cottage doorstep and planted pots")))
        scene.addInstance({.mesh = *id, .material = m_stoneMaterial});
}
} // namespace cloudsong
