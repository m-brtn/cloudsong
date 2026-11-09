#include "World.h"

#include <quench/assets/Model.h>
#include <quench/core/FileSystem.h>
#include <quench/core/Log.h>
#include <tuple>

namespace cloudsong {
namespace render = quench::render;
namespace {
const glm::vec3 altitude(0, kAltitude, 0);
const glm::vec3 ivory = srgb(207, 218, 207), teal = srgb(62, 128, 135), gold = srgb(204, 171, 91);

glm::mat4 position(glm::vec3 p) { return glm::translate(glm::mat4(1), p + altitude); }
float segmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = b - a;
    return glm::distance(p, a + d * std::clamp(glm::dot(p - a, d) / std::max(glm::dot(d, d), .0001f), 0.0f, 1.0f));
}

} // namespace

void World::layoutPaths() {
    auto rawHeight = [&](glm::vec2 p) {
        float height = -1e6f;
        for (const auto& isle : m_isles)
            if (const auto h = isle.heightAt(p)) height = std::max(height, *h);
        return height > -1e5f ? height : 0.0f;
    };
    for (const auto p : std::array<glm::vec3, 19>{{{64, 63, 6.0f},
                                                   {4, -22, 5.0f},
                                                   {57, 56, 2.4f},
                                                   {71, 64, 2.4f},
                                                   {-65, 13, 6.0f},
                                                   {202, 132, 2.5f},
                                                   {0, 8, 3.1f},
                                                   {139, 79, 2.1f},
                                                   {139, 240, 3.0f},
                                                   {124, 239, 2.5f},
                                                   {-176, 100, 2.4f},
                                                   {-79, 2, 6.0f},
                                                   {-104, 28, 7.8f},
                                                   {-124, -3, 3.0f},
                                                   {-171, 97, 3.3f},
                                                   {143, 75, 3.0f},
                                                   {202, 155, 3.8f},
                                                   {-110, 161, 5.0f},
                                                   {133, 250, 6.0f}}})
        m_foundations.push_back({p.x, p.y, p.z, rawHeight({p.x, p.y})});
    // Control points follow the land, curving round the pond and the terrace walls.
    auto path = [&](std::initializer_list<glm::vec2> anchors, float width) {
        const std::vector<glm::vec2> a(anchors);
        Path result{{}, {}, width};
        for (std::size_t k = 0; k + 1 < a.size(); ++k) {
            const glm::vec2 p0 = a[k ? k - 1 : k], p1 = a[k], p2 = a[k + 1], p3 = a[std::min(k + 2, a.size() - 1)];
            const int count = std::max(4, int(glm::distance(p1, p2) * 4));
            for (int j = 0; j < count; ++j) {
                const float t = float(j) / float(count), t2 = t * t, t3 = t2 * t;
                result.points.push_back(.5f *
                                        (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                         (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3));
            }
        }
        result.points.push_back(a.back());
        for (const auto p : result.points) {
            result.low = glm::min(result.low, p - glm::vec2(width + 5.0f));
            result.high = glm::max(result.high, p + glm::vec2(width + 5.0f));
        }
        m_paths.push_back(std::move(result));
    };
    path({{2, -40}, {2, -29}, {2, -24}, {-3, -23}, {-13, -24}, {-21, -18}}, 1.1f);
    path({{-7, -23}, {-26, -23}, {-32, -14}, {-34, -4}, {-41, -1}}, 1.25f);
    path({{7, -20}, {15, -13}, {23, 0}, {25, 14}, {18, 22}, {10, 25}}, 1.05f);
    path({{-58, -1}, {-72, -8}, {-91, -5}, {-94, 8}, {-85, 22}, {-94, 32}, {-104, 28}}, 1.65f);
    path({{-80, -7}, {-79, -2}, {-79, 2}}, 1.2f);
    path({{-91, -5}, {-112, -12}, {-124, -3}, {-128, 15}, {-123, 28}}, 1.0f);
    path({{-178, 60}, {-161, 62}, {-146, 75}, {-150, 89}, {-161, 101}, {-171, 97}}, 1.5f);
    path({{-120, 135}, {-104, 138}, {-92, 151}, {-99, 163}, {-110, 161}}, 1.25f);
    path({{52, 42}, {59, 48}, {65.35f, 54}, {65.35f, 59.7f}}, 1.0f);
    path({{76, 48}, {81, 52}, {86, 56}}, 1.5f);
    path({{107, 56}, {120, 52}, {134, 49}, {153, 55}, {160, 70}, {151, 83}, {143, 75}}, 1.5f);
    path({{171, 120}, {182, 120}, {213, 126}, {224, 146}, {217, 169}, {202, 155}}, 1.5f);
    path({{116, 220}, {129, 225}, {148, 233}, {151, 249}, {133, 250}}, 1.35f);
    for (auto& route : m_paths) {
        for (const auto p : route.points) route.heights.push_back(foundationHeight(p, rawHeight(p)));
        // Keep both landings at their authored elevations. Slope limiting may cut/fill the
        // approach but must not lower a road's destination below the building it serves.
        const float firstHeight = route.heights.front(), lastHeight = route.heights.back();
        std::vector<float> distance(route.points.size(), 0.0f);
        for (std::size_t i = 1; i < route.points.size(); ++i)
            distance[i] = distance[i - 1] + glm::distance(route.points[i], route.points[i - 1]);
        const float landing = std::min(3.0f, distance.back() * .1f);
        std::vector<bool> anchored(route.points.size(), false);
        std::vector<float> anchorHeight = route.heights;
        for (std::size_t i = 0; i < route.points.size(); ++i) {
            if (distance[i] < landing) {
                anchored[i] = true;
                anchorHeight[i] = firstHeight;
            }
            if (distance.back() - distance[i] < landing) {
                anchored[i] = true;
                anchorHeight[i] = lastHeight;
            }
            // Every shared junction has the same level on all incident routes.
            // Blending unrelated profile heights near the fork made a steep ripple.
            for (const auto& other : m_paths) {
                for (auto end : {other.points.front(), other.points.back()}) {
                    if (glm::distance(route.points[i], end) < 1.8f) {
                        anchored[i] = true;
                        anchorHeight[i] = foundationHeight(end, rawHeight(end));
                    }
                }
            }
            for (const auto pad : m_foundations)
                if (glm::distance(route.points[i], glm::vec2(pad.x, pad.y)) < pad.z + 1.0f) {
                    anchored[i] = true;
                    anchorHeight[i] = pad.w;
                }
        }
        // Road cuts climb along their length. A broad shoulder blends their level cross-section
        // into the hill, instead of painting a ribbon on an unwalkable sideways slope.
        for (int pass = 0; pass < 64; ++pass) {
            const auto previous = route.heights;
            for (std::size_t i = 1; i + 1 < route.points.size(); ++i)
                route.heights[i] = (previous[i - 1] + 2.0f * previous[i] + previous[i + 1]) * .25f;
            route.heights.front() = firstHeight;
            for (std::size_t i = 1; i < route.points.size(); ++i) {
                const float gradeLimit = .30f;
                const float rise = glm::distance(route.points[i], route.points[i - 1]) * gradeLimit;
                if (anchored[i])
                    route.heights[i] = anchorHeight[i];
                else
                    route.heights[i] =
                        std::clamp(route.heights[i], route.heights[i - 1] - rise, route.heights[i - 1] + rise);
            }
            route.heights.back() = lastHeight;
            for (std::size_t i = route.points.size() - 1; i > 0; --i) {
                const float gradeLimit = .30f;
                const float rise = glm::distance(route.points[i], route.points[i - 1]) * gradeLimit;
                if (anchored[i - 1])
                    route.heights[i - 1] = anchorHeight[i - 1];
                else
                    route.heights[i - 1] =
                        std::clamp(route.heights[i - 1], route.heights[i] - rise, route.heights[i] + rise);
            }
        }
        float steepest = 0;
        for (std::size_t i = 1; i < route.points.size(); ++i)
            steepest = std::max(steepest, std::abs(route.heights[i] - route.heights[i - 1]) /
                                              std::max(.001f, distance[i] - distance[i - 1]));
        quench::log::info("game",
                          "route ({:.1f},{:.1f}) to ({:.1f},{:.1f}): {:.1f} m, grade {:.3f}, landing error {:.4f}",
                          route.points.front().x, route.points.front().y, route.points.back().x, route.points.back().y,
                          distance.back(), steepest, std::abs(route.heights.back() - lastHeight));
    }
    // Store endpoint heights before any bridge grading. The same profile carves its aprons
    // into the banks and builds the deck, so a descending deck cannot disappear under turf.
    for (const auto bridge :
         std::array<Bridge, 2>{{{{-41, -1}, {-58, -1}, 1.8f, 0, 0}, {{86, 56}, {107, 56}, 1.65f, 0, 0}}}) {
        auto b = bridge;
        b.ya = terrainHeight(b.a).value_or(0);
        b.yb = terrainHeight(b.b).value_or(0);
        m_bridges.push_back(b);
    }
    // Audit the finished terrain, including profile blending, foundation shoulders and bridge
    // cuts. A bounded centreline alone cannot detect an unintended cross-slope at a junction.
    glm::vec2 steepestAt(0), crossAt(0);
    for (const auto& route : m_paths)
        for (std::size_t i = 1; i < route.points.size(); ++i) {
            const auto a = route.points[i - 1], b = route.points[i], d = b - a;
            const float length = glm::length(d);
            if (length < .001f) continue;
            const auto ha = terrainHeight(a), hb = terrainHeight(b);
            if (ha && hb) {
                const float grade = std::abs(*hb - *ha) / length;
                if (grade > m_pathMaxGrade) {
                    m_pathMaxGrade = grade;
                    steepestAt = b;
                }
            }
            const auto across = glm::vec2(-d.y, d.x) / length * route.width * .8f;
            const auto left = terrainHeight(b - across), right = terrainHeight(b + across);
            if (left && right) {
                const float grade = std::abs(*right - *left) / (route.width * 1.6f);
                if (grade > m_pathMaxCrossSlope) {
                    m_pathMaxCrossSlope = grade;
                    crossAt = b;
                }
            }
        }
    quench::log::info(
        "game", "route terrain audit: longitudinal {:.3f} at ({:.2f},{:.2f}), cross-slope {:.3f} at ({:.2f},{:.2f})",
        m_pathMaxGrade, steepestAt.x, steepestAt.y, m_pathMaxCrossSlope, crossAt.x, crossAt.y);
}

float World::foundationHeight(glm::vec2 p, float natural) const {
    for (const auto pad : m_foundations) {
        const float distance = glm::distance(p, glm::vec2(pad.x, pad.y));
        natural = glm::mix(natural, pad.w, 1.0f - smoothstep(pad.z, pad.z + 4.0f, distance));
    }
    return natural;
}

float World::gradedHeight(glm::vec2 p, float natural) const {
    natural = foundationHeight(p, natural);
    float total = 0, height = 0, influence = 0;
    for (const auto& route : m_paths) {
        if (route.heights.empty() || p.x < route.low.x || p.x > route.high.x || p.y < route.low.y || p.y > route.high.y)
            continue;
        float nearest = route.width + 5.0f, target = natural;
        for (std::size_t i = 1; i < route.points.size(); ++i) {
            const auto a = route.points[i - 1], b = route.points[i], d = b - a;
            if (p.x < std::min(a.x, b.x) - nearest || p.x > std::max(a.x, b.x) + nearest ||
                p.y < std::min(a.y, b.y) - nearest || p.y > std::max(a.y, b.y) + nearest)
                continue;
            const float t = std::clamp(glm::dot(p - a, d) / std::max(glm::dot(d, d), .0001f), 0.0f, 1.0f);
            const float distance = glm::distance(p, a + d * t);
            if (distance < nearest) {
                nearest = distance;
                target = glm::mix(route.heights[i - 1], route.heights[i], t);
            }
        }
        const float weight = 1.0f - smoothstep(route.width + .45f, route.width + 5.0f, nearest);
        const float blendWeight = weight * weight * weight;
        height += blendWeight * target;
        total += blendWeight;
        influence = std::max(influence, weight);
    }
    float result = total > .0001f ? glm::mix(natural, height / total, influence) : natural;
    for (const auto& bridge : m_bridges) {
        const auto delta = bridge.b - bridge.a;
        const float t = glm::dot(p - bridge.a, delta) / glm::dot(delta, delta);
        if (t < 0 || t > 1) continue;
        const float distance = glm::distance(p, bridge.a + delta * t);
        const float deck = glm::mix(bridge.ya, bridge.yb, t) + std::pow(std::sin(kPi * t), 2.0f);
        result = glm::mix(result, std::min(result, deck - .035f),
                          1.0f - smoothstep(bridge.width + .3f, bridge.width + 2.0f, distance));
    }
    // Building interiors win over a nearby road's shoulder. Roads crossing a pad
    // are anchored to this same elevation, so their approaches remain continuous.
    for (const auto pad : m_foundations) {
        const float r = glm::distance(p, glm::vec2(pad.x, pad.y));
        result = glm::mix(result, pad.w, 1.0f - smoothstep(pad.z - .3f, pad.z + .5f, r));
    }
    return result;
}

float World::pathCoverage(glm::vec2 p) const {
    // Signed distance-like field. Interpolating this before the fragment threshold gives
    // smooth curves even when a path crosses a terrain triangle diagonally.
    float coverage = -1;
    for (std::size_t i = 1; i < m_meadowSteps.size(); ++i)
        coverage = std::max(coverage, 1.0f - segmentDistance(p, m_meadowSteps[i - 1], m_meadowSteps[i]) / 1.15f);
    for (const auto& route : m_paths) {
        if (p.x < route.low.x || p.x > route.high.x || p.y < route.low.y || p.y > route.high.y) continue;
        float nearest = route.width * 2.0f;
        for (std::size_t i = 1; i < route.points.size(); ++i) {
            const auto a = route.points[i - 1], b = route.points[i];
            if (p.x < std::min(a.x, b.x) - nearest || p.x > std::max(a.x, b.x) + nearest ||
                p.y < std::min(a.y, b.y) - nearest || p.y > std::max(a.y, b.y) + nearest)
                continue;
            nearest = std::min(nearest, segmentDistance(p, a, b));
        }
        coverage = std::max(coverage, 1.0f - nearest / route.width);
    }
    // The cave threshold blends into exposed stone in the terrain itself.
    coverage = std::max(coverage, 1.0f - glm::distance(p, glm::vec2(202, 132)) / 4.2f);
    return coverage;
}

bool World::onPath(glm::vec2 p) const {
    for (std::size_t i = 1; i < m_meadowSteps.size(); ++i)
        if (segmentDistance(p, m_meadowSteps[i - 1], m_meadowSteps[i]) < 1.35f) return true;
    for (const auto& bridge : m_bridges)
        if (segmentDistance(p, bridge.a, bridge.b) < bridge.width + .2f) return true;
    for (const auto& path : m_paths) {
        if (p.x < path.low.x || p.x > path.high.x || p.y < path.low.y || p.y > path.high.y) continue;
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const glm::vec2 a = path.points[i - 1], b = path.points[i];
            if (p.x < std::min(a.x, b.x) - path.width - .25f || p.x > std::max(a.x, b.x) + path.width + .25f ||
                p.y < std::min(a.y, b.y) - path.width - .25f || p.y > std::max(a.y, b.y) + path.width + .25f)
                continue;
            if (segmentDistance(p, a, b) < path.width + .2f) return true;
        }
    }
    return false;
}

void World::buildRegion(render::Scene& scene, quench::PhysicsWorld& physics) {
    MeshData masonry, roofs, trim, garden;
    auto y = [&](glm::vec2 p) { return terrainHeight(p).value_or(0); };
    auto add = [&](MeshData& into, const MeshData& mesh, glm::vec3 at, glm::vec3 color) {
        append(into, mesh, position(at), glm::vec4(color, 1));
    };
    auto pedestal = [&](glm::vec3 at, float radius, float height) {
        MeshData m = lathe({{.001f, 0},
                            {radius - .06f, 0},
                            {radius, .06f},
                            {radius, height - .05f},
                            {radius - .06f, height},
                            {.001f, height}},
                           48);
        add(masonry, m, at, ivory);
        MeshData collision;
        append(collision, m, position(at));
        addStatic(physics, collision);
    };
    // Paths belong to the terrain itself. Junctions have one height, one normal and one
    // material surface: there are no overlapping road ribbons or floating stair boxes.
    // Walkable bowed bridges. The underside is a true arch and the parapets have open gaps at the landings.
    auto bridge = [&](const Bridge& profile) {
        const auto a = profile.a, b = profile.b;
        const float width = profile.width;
        MeshData bridgeMesh;
        const float ya = profile.ya + .025f, yb = profile.yb + .025f;
        const auto along = glm::normalize(b - a);
        const glm::vec2 across(-along.y, along.x);
        constexpr int n = 48;
        for (int i = 0; i <= n; ++i) {
            const float t = float(i) / n;
            const auto p = glm::mix(a, b, t);
            const float top = glm::mix(ya, yb, t) + std::pow(std::sin(kPi * t), 2.0f);
            const float bottom = glm::mix(ya, yb, t) - 2.6f + 2.85f * std::sin(kPi * t);
            for (const auto q :
                 std::array<glm::vec2, 4>{{{-width, bottom}, {-width, top}, {width, top}, {width, bottom}}}) {
                const auto xz = p + across * q.x;
                bridgeMesh.vertices.push_back(
                    {.position = glm::vec3(xz.x, q.y, xz.y) + altitude, .color = glm::vec4(ivory, 1)});
            }
            if (i)
                for (std::uint32_t j = 0; j < 4; ++j) {
                    const auto c = std::uint32_t((i - 1) * 4) + j, d = std::uint32_t((i - 1) * 4) + (j + 1) % 4;
                    bridgeMesh.indices.insert(bridgeMesh.indices.end(), {c, d, c + 4, d, d + 4, c + 4});
                }
        }
        // Close the abutments: a bridge is masonry with end faces, not an open ribbon.
        bridgeMesh.indices.insert(bridgeMesh.indices.end(), {0, 2, 1, 0, 3, 2});
        const std::uint32_t end = n * 4;
        bridgeMesh.indices.insert(bridgeMesh.indices.end(), {end, end + 1, end + 2, end, end + 2, end + 3});
        // Low chamfered stone parapets follow the deck, with individually readable coping blocks.
        // They turn down before each landing, leaving a generous clear entrance.
        const float length = glm::distance(a, b), yaw = std::atan2(along.x, along.y);
        const int blocks = int(length / .85f);
        for (int i = 1; i + 1 < blocks; ++i) {
            const float t = (float(i) + .5f) / float(blocks);
            const auto p = glm::mix(a, b, t);
            const float deck = glm::mix(ya, yb, t) + std::pow(std::sin(kPi * t), 2.0f);
            const float edgeFade = smoothstep(.04f, .18f, t) * (1.0f - smoothstep(.82f, .96f, t));
            const float height = .38f + .30f * edgeFade;
            for (float side : {-1.0f, 1.0f}) {
                const auto q = p + across * (width - .13f) * side;
                const auto m = position({q.x, deck + height * .5f - .06f, q.y}) *
                               glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
                append(masonry, chamferedBlock({.15f, height * .5f, length / float(blocks) * .495f}, .035f), m,
                       glm::vec4(ivory * (.84f + .025f * float(i % 3)), 1));
                // A separate lip protects the wall and gives a clean lit edge at human scale.
                const auto cap =
                    position({q.x, deck + height - .015f, q.y}) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
                append(masonry, chamferedBlock({.21f, .07f, length / float(blocks) * .485f}, .035f), cap,
                       glm::vec4(ivory, 1));
                addBox(physics, glm::vec3(q.x, deck + height * .5f, q.y) + altitude,
                       {.18f, height * .5f + .07f, length / float(blocks) * .49f}, yaw);
            }
        }
        computeNormals(bridgeMesh);
        append(masonry, bridgeMesh);
        addStatic(physics, bridgeMesh);
        // Narrow mortar joints belong only to this bridge's masonry. Local geometry keeps
        // their scale crisp without projecting a brick texture onto towers or the landscape.
        auto joint = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, bool reverse) {
            const auto first = std::uint32_t(masonry.vertices.size());
            for (auto p : {p0, p1, p2, p3})
                masonry.vertices.push_back({.position = p + altitude, .color = glm::vec4(ivory * .76f, 1)});
            if (reverse)
                masonry.indices.insert(masonry.indices.end(),
                                       {first, first + 2, first + 1, first, first + 3, first + 2});
            else
                masonry.indices.insert(masonry.indices.end(),
                                       {first, first + 1, first + 2, first, first + 2, first + 3});
        };
        auto edgePoint = [&](float t, float side, float fraction) {
            const auto p = glm::mix(a, b, t) + across * (width + .008f) * side;
            const float top = glm::mix(ya, yb, t) + std::pow(std::sin(kPi * t), 2.0f);
            const float bottom = glm::mix(ya, yb, t) - 2.6f + 2.85f * std::sin(kPi * t);
            return glm::vec3(p.x, glm::mix(bottom, top, fraction), p.y);
        };
        for (float side : {-1.0f, 1.0f}) {
            for (int i = 3; i < n; i += 3) {
                const float t = float(i) / n, skew = (t - .5f) * .045f;
                const auto p0 = edgePoint(t, side, .025f), p1 = edgePoint(t + skew, side, .96f);
                const glm::vec3 margin(along.x * .012f, 0, along.y * .012f);
                joint(p0 - margin, p0 + margin, p1 + margin, p1 - margin, side < 0);
            }
            for (int i = 0; i < n; ++i) {
                const auto p0 = edgePoint(float(i) / n, side, .53f), p1 = edgePoint(float(i + 1) / n, side, .53f);
                const glm::vec3 margin(0, .008f, 0);
                joint(p0 - margin, p1 - margin, p1 + margin, p0 + margin, side < 0);
            }
        }
    };
    for (const auto& profile : m_bridges) bridge(profile);
    // Larger plinth under the imported sanctuary, built from three shallow, walkable steps.
    const glm::vec2 sanctuary(-104, 28);
    for (int k = 0; k < 3; ++k)
        pedestal({sanctuary.x, y(sanctuary) + float(k) * .12f, sanctuary.y}, 7.0f - float(k) * .25f, .12f);

    // Slender bell towers share the pavilion's stone, roof colours, cornices and sun discs. Their open belfries
    // have separated piers: the silhouette and real openings do more work than noisy surface decoration.
    auto tower = [&](glm::vec2 at, float scale) {
        const glm::vec3 base(at.x, y(at) - .1f, at.y);
        auto part = [&](MeshData& target, MeshData mesh, glm::vec3 offset, glm::vec3 color) {
            append(target, mesh, position(base + offset * scale) * glm::scale(glm::mat4(1), glm::vec3(scale)),
                   glm::vec4(color, 1));
        };
        for (int k = 0; k < 3; ++k)
            pedestal(base + glm::vec3(0, float(k) * .18f * scale, 0), (2.5f - float(k) * .2f) * scale, .18f * scale);
        const auto shaft = lathe({{.001f, .5f},
                                  {1.4f, .5f},
                                  {1.55f, .65f},
                                  {1.38f, .85f},
                                  {1.05f, 5.8f},
                                  {1.4f, 5.9f},
                                  {1.45f, 6.2f},
                                  {.001f, 6.2f}},
                                 24);
        part(masonry, shaft, {}, ivory);
        MeshData coll;
        append(coll, shaft, position(base) * glm::scale(glm::mat4(1), glm::vec3(scale)));
        addStatic(physics, coll);
        for (int k = 0; k < 6; ++k) {
            const float a = kTau * float(k) / 6;
            const glm::vec3 p(std::cos(a) * 1.13f, 6.2f, std::sin(a) * 1.13f);
            part(masonry, lathe({{.001f, 0}, {.22f, 0}, {.18f, .2f}, {.16f, 1.65f}, {.25f, 1.8f}, {.001f, 1.8f}}, 12),
                 p, ivory);
        }
        part(roofs,
             lathe({{.001f, 8},
                    {1.9f, 8},
                    {1.95f, 8.15f},
                    {1.65f, 8.28f},
                    {1.22f, 8.55f},
                    {.8f, 9.15f},
                    {.52f, 9.85f},
                    {.16f, 10.2f},
                    {.001f, 10.3f}},
                   48),
             {}, teal);
        for (float h : {.8f, 5.9f, 8.15f})
            part(trim, render::torusMesh(h > 7 ? 1.9f : 1.4f, .045f, 48, 8), {0, h, 0}, gold);
        part(trim, ellipsoid({.14f, .4f, .14f}, 10, 16), {0, 10.55f, 0}, gold);
        // A bell suspended in the open belfry.
        part(trim,
             lathe({{.001f, 0}, {.45f, 0}, {.5f, .1f}, {.35f, .25f}, {.25f, .65f}, {.08f, .8f}, {.001f, .8f}}, 24),
             {0, 6.7f, 0}, gold);
        for (int k = 0; k < 4; ++k) {
            const float a = kTau * float(k) / 4;
            const glm::vec3 p(std::sin(a) * 1.4f, 3.8f, std::cos(a) * 1.4f);
            const auto m = position(base + p * scale) * glm::rotate(glm::mat4(1), a, glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1), kPi * .5f, glm::vec3(1, 0, 0)) *
                           glm::scale(glm::mat4(1), glm::vec3(scale));
            append(trim, render::torusMesh(.37f, .045f, 32, 8), m, glm::vec4(gold, 1));
        }
    };
    tower({202, 155}, 1.3f);

    // Roofless arcades make the northern destinations distinct from the bell towers.
    auto arcade = [&](glm::vec2 at, float radius, int bays) {
        const float ground = y(at);
        pedestal({at.x, ground - .1f, at.y}, radius + 1.2f, .2f);
        pedestal({at.x, ground + .1f, at.y}, radius + .9f, .18f);
        MeshData arches;
        for (int k = 0; k < 6; ++k) {
            const float a = kTau * float(k) / 6;
            const glm::vec3 p(at.x + std::cos(a) * radius, ground + .28f, at.y + std::sin(a) * radius);
            const float height = k < bays + 1 ? 3.4f : 1.3f;
            const auto pier = lathe({{.001f, 0},
                                     {.46f, 0},
                                     {.46f, .18f},
                                     {.33f, .3f},
                                     {.3f, height - .2f},
                                     {.43f, height},
                                     {.001f, height}},
                                    16);
            add(arches, pier, p, ivory);
            if (k >= bays) continue;
            const float a1 = kTau * float(k + 1) / 6;
            const glm::vec3 q(at.x + std::cos(a1) * radius, ground + .28f, at.y + std::sin(a1) * radius);
            const auto across = glm::normalize(q - p);
            const auto depth = glm::cross(across, glm::vec3(0, 1, 0));
            const auto centre = (p + q) * .5f + glm::vec3(0, 3.4f, 0);
            const float r = glm::distance(p, q) * .5f;
            const auto first = std::uint32_t(arches.vertices.size());
            for (int j = 0; j <= 24; ++j)
                for (int v = 0; v < 8; ++v) {
                    const float angle = kPi * float(j) / 24, section = kTau * float(v) / 8;
                    const float rr = r + .2f * std::cos(section);
                    const auto pos = centre + across * (std::cos(angle) * rr) + glm::vec3(0, std::sin(angle) * rr, 0) +
                                     depth * (std::sin(section) * .3f);
                    arches.vertices.push_back({.position = pos + altitude, .color = glm::vec4(ivory, 1)});
                }
            for (std::uint32_t j = 0; j < 24; ++j)
                for (std::uint32_t v = 0; v < 8; ++v) {
                    const auto c = first + j * 8 + v, d = first + j * 8 + (v + 1) % 8;
                    arches.indices.insert(arches.indices.end(), {c, c + 8, d, d, c + 8, d + 8});
                }
        }
        computeNormals(arches);
        append(masonry, arches);
        addStatic(physics, arches);
        pedestal({at.x, ground + .28f, at.y}, 1.1f, .65f);
        add(trim, render::torusMesh(.8f, .05f, 40, 8), {at.x, ground + .96f, at.y}, gold);
    };
    arcade({-110, 161}, 4.0f, 3);

    // Music court: one composition, with a clear southern entrance and seating
    // looking across the paving toward the harp, not a prop dropped on a hill.
    const glm::vec3 court = m_landmarks.musicCourt;
    pedestal(court - glm::vec3(0, .17f, 0), 5.45f, .14f);
    auto sector = [&](float inner, float outer, float a, float b, float bottom, float top) {
        MeshData mesh;
        for (float h : {bottom, top})
            for (auto q : std::array<glm::vec2, 4>{{{inner, a}, {outer, a}, {outer, b}, {inner, b}}})
                mesh.vertices.push_back({.position = {std::sin(q.y) * q.x, h, std::cos(q.y) * q.x}});
        mesh.indices = {4, 7, 5, 5, 7, 6, 0, 1, 3, 1, 2, 3, 0, 4, 1, 1, 4, 5,
                        1, 5, 2, 2, 5, 6, 2, 6, 3, 3, 6, 7, 3, 7, 0, 0, 7, 4};
        computeNormals(mesh);
        return mesh;
    };
    MeshData courtCollision;
    for (int ring = 0; ring < 5; ++ring) {
        const int count = 8 + ring * 8;
        const float inner = float(ring) * 1.07f + .012f, outer = float(ring + 1) * 1.07f - .012f;
        for (int k = 0; k < count; ++k) {
            const float a = kTau * (float(k) + .5f * float(ring % 2)) / float(count);
            const auto slab = sector(inner, outer, a + .003f, a + kTau / float(count) - .003f, -.035f, 0);
            const glm::vec3 tint = glm::mix(srgb(161, 182, 185), srgb(214, 221, 206), hash01(k, ring, 92u));
            append(masonry, slab, position(court), glm::vec4(tint, 1));
            append(courtCollision, slab, position(court));
        }
    }
    addStatic(physics, courtCollision);
    // Inlaid arcs describe wind round a small central star. These are flush marks,
    // never a raised obstacle or a luminous carpet.
    for (int k = 0; k < 3; ++k)
        append(trim, sector(.9f, .94f, kTau * float(k) / 3, kTau * float(k) / 3 + 1.6f, .002f, .004f),
               position(court + glm::vec3(0, 0, -.8f)), glm::vec4(teal, 1));
    for (int k = 0; k < 18; ++k) {
        const float a = -.95f + float(k) * .11f;
        const auto block = sector(5.10f, 5.48f, a, a + .106f, 0, .34f);
        append(masonry, block, position(court), glm::vec4(ivory * .85f, 1));
        MeshData col;
        append(col, block, position(court));
        addStatic(physics, col);
        append(masonry, sector(5.04f, 5.55f, a, a + .106f, .34f, .43f), position(court), glm::vec4(ivory, 1));
    }
    auto bench = [&](glm::vec3 at, float yaw, float length) {
        auto part = [&](glm::vec3 half, glm::vec3 offset, glm::vec3 tint) {
            const auto m = position(at) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0)) *
                           glm::translate(glm::mat4(1), offset);
            const auto block = chamferedBlock(half, .045f);
            append(masonry, block, m, glm::vec4(tint, 1));
            MeshData col;
            append(col, block, m);
            addStatic(physics, col);
        };
        for (float side : {-1.0f, 1.0f}) {
            part({.15f, .23f, .32f}, {side * length * .72f, .23f, 0}, ivory * .8f);
            part({.09f, .38f, .09f}, {side * length * .8f, .68f, -.29f}, ivory);
        }
        part({length, .09f, .37f}, {0, .50f, 0}, ivory);
        part({length, .14f, .065f}, {0, .86f, -.30f}, teal * .8f);
    };
    bench(court + glm::vec3(-2.5f, 0, -1.7f), .72f, 1.12f);
    // A small quiet court leaves a broad open centre for its resident and bubbles.
    const auto glade = m_landmarks.meadowGlade;
    MeshData gladeFloor;
    for (int row = -4; row <= 4; ++row)
        for (int col = -4; col <= 4; ++col) {
            const glm::vec2 offset(col * .94f + (row % 2) * .47f, row * .86f);
            if (glm::length(offset) > 4.1f) continue;
            const auto tile = chamferedBlock({.45f, .07f, .41f}, .025f);
            const auto tint = glm::mix(srgb(170, 190, 192), srgb(213, 220, 205), hash01(col, row, 319));
            append(gladeFloor, tile, position(glade + glm::vec3(offset.x, -.07f, offset.y)), glm::vec4(tint, 1));
        }
    append(masonry, gladeFloor);
    addStatic(physics, gladeFloor);
    bench(glade + glm::vec3(-3.9f, 0, 2.5f), 1.0f, 1.15f);
    bench(glade + glm::vec3(3.9f, 0, 2.5f), -1.0f, 1.15f);
    // Fine turquoise carvings in both backrests, deliberately quiet in daylight.
    for (float side : {-1.f, 1.f}) {
        const float yaw = side < 0 ? 1.0f : -1.0f;
        const auto frame =
            position(glade + glm::vec3(side * 3.9f, 0, 2.5f)) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
        for (int j = -3; j <= 3; ++j)
            append(trim, render::torusMesh(.055f, .012f, 20, 6),
                   frame * glm::translate(glm::mat4(1), glm::vec3(j * .26f, .86f, -.225f)) *
                       glm::rotate(glm::mat4(1), kPi * .5f, glm::vec3(1, 0, 0)),
                   glm::vec4(ivory, 1));
    }
    // A planted border nestles under the one willow, leaving the entrance open.
    pedestal(court + glm::vec3(-3.7f, -.1f, 2), 1.25f, .24f);
    add(garden, lathe({{.001f, 0}, {1.15f, 0}, {1.1f, .03f}, {.001f, .03f}}, 40), court + glm::vec3(-3.7f, .14f, 2),
        srgb(66, 118, 103));
    // A low water garden replaces the eastern tower: basin, shallow reflecting
    // water, a dry stepping-stone crossing and a single spout, a different silhouette.
    const glm::vec3 basin(143, y({143, 75}) + .1f, 75);
    pedestal(basin - glm::vec3(0, .12f, 0), 2.75f, .12f);
    MeshData basinWall;
    append(basinWall, lathe({{2.35f, 0}, {2.7f, 0}, {2.7f, .40f}, {2.35f, .40f}, {2.35f, 0}}, 64), position(basin),
           glm::vec4(ivory, 1));
    append(masonry, basinWall);
    addStatic(physics, basinWall);
    auto waterMesh = lathe({{.001f, .29f}, {2.34f, .29f}}, 64);
    if (auto id = scene.addMesh(waterMesh.desc("reflecting garden water")))
        scene.addInstance({.mesh = *id, .material = m_water, .transform = position(basin), .castsShadows = false});
    for (int k = -2; k <= 2; ++k)
        pedestal(basin + glm::vec3(float(k) * .85f, .15f, .22f * std::sin(float(k) * 1.9f)), .36f, .23f);
    add(masonry, chamferedBlock({.28f, .7f, .3f}, .08f), basin + glm::vec3(0, .7f, 2.2f), ivory);
    addBox(physics, basin + altitude + glm::vec3(0, .7f, 2.2f), {.28f, .7f, .3f}, 0);
    add(trim, ellipsoid({.13f, .10f, .25f}, 10, 16), basin + glm::vec3(0, 1.1f, 1.9f), teal);
    // Small dark blue water thread, restrained enough to read as water in daylight.
    add(trim, ellipsoid({.025f, .40f, .025f}, 8, 12), basin + glm::vec3(0, .70f, 1.72f), srgb(83, 168, 184));
    // Observatory's broad floor, then the telescope; the northern destination is
    // a sheltered overlook, rather than another copy of the bell tower.
    const glm::vec3 observatory(-171, y({-171, 97}), 97);
    pedestal(observatory, 3.15f, .14f);
    bench(observatory + glm::vec3(1.8f, .14f, 1.0f), -1.2f, .8f);
    bench({-124, y({-124, -3}), -3}, -.4f, 1.0f);
    // The far meadow ends in an open wind compass. No second arcade or tower.
    const glm::vec3 compass(133, y({133, 250}), 250);
    pedestal(compass, 3.5f, .12f);
    for (int k = 0; k < 8; ++k) {
        const float a = float(k) * kTau / 8;
        append(trim, sector(2.1f, 2.5f, a - .025f, a + .025f, .121f, .125f), position(compass), glm::vec4(teal, 1));
    }

    // Large rounded outcrops and smaller stones break the smooth hill bases into readable natural groups.
    // Moss follows upward faces, while their lower faces stay blue-grey. Every rock has matching collision.
    Random rocksRandom{811u};
    for (const auto& isle : m_isles) {
        if (isle.desc.seed < 101u) continue;
        for (int group = 0; group < 3; ++group) {
            const float angle = float(group) * 2.2f + float(isle.desc.seed) * .4f;
            const auto centre =
                isle.desc.centre + glm::vec2(std::cos(angle), std::sin(angle)) * isle.desc.radius * .57f;
            for (int j = 0; j < 5; ++j) {
                const auto p = centre + glm::vec2(rocksRandom.uniform(-5, 5), rocksRandom.uniform(-4, 4));
                if (onPath(p) || !isle.heightAt(p)) continue;
                const float scale = rocksRandom.uniform(.65f, 2.3f);
                MeshData rock = ellipsoid({scale, scale * rocksRandom.uniform(.6f, 1.4f), scale * .8f}, 10, 16);
                // Keep a whole boulder's footprint outside roots and shrine steps,
                // not just its centre. Consume its random dimensions before skipping
                // so adding a shrine does not reshuffle every subsequent rock group.
                const bool overlapsGarden = std::any_of(m_garden.begin(), m_garden.end(), [&](const auto& object) {
                    const float footprint = object.tree ? object.height * .23f : 1.9f;
                    return glm::distance(p, object.at) < footprint + scale + .4f;
                });
                if (overlapsGarden) continue;
                bool clear = true;
                for (auto pad : m_foundations)
                    if (glm::distance(p, glm::vec2(pad.x, pad.y)) < pad.z + scale + .4f) clear = false;
                for (auto chime : m_landmarks.chimes)
                    if (glm::distance(p, glm::vec2(chime.x, chime.z)) < scale + 1.4f) clear = false;
                if (!clear) continue;
                for (auto& v : rock.vertices) {
                    v.position *= 1.0f + .08f * std::sin(v.position.x * 2 + v.position.z * 3);
                    v.color = glm::vec4(
                        glm::mix(srgb(97, 131, 147), isle.grassColor(p), smoothstep(.25f, .7f, v.normal.y)), 1);
                }
                MeshData placedRock;
                append(placedRock, rock, position({p.x, y(p) - scale * .25f, p.y}));
                append(garden, placedRock);
                addStatic(physics, placedRock);
            }
        }
    }
    // Hiding places have room to walk around. The cover is between the route and
    // the chime, never over the collectible or its landing footprint.
    for (const auto& cover : m_cacheCovers) {
        MeshData nook;
        const float floor = y(cover.at);
        const auto transform =
            position({cover.at.x, floor, cover.at.y}) * glm::rotate(glm::mat4(1), cover.yaw, glm::vec3(0, 1, 0));
        if (cover.kind == 1) {
            for (int k = 0; k < 3; ++k)
                append(nook, chamferedBlock({.55f, .70f - .1f * float(k), .25f}, .08f),
                       transform * glm::translate(glm::mat4(1), glm::vec3(float(k - 1) * 1.08f, .60f, 0)),
                       glm::vec4(ivory * .78f, 1));
        } else {
            for (int k = 0; k < 2; ++k) {
                auto stone = ellipsoid({1.1f, 1.15f - .3f * float(k), .75f}, 12, 20);
                for (auto& v : stone.vertices)
                    v.color = glm::vec4(
                        glm::mix(srgb(99, 132, 145), srgb(78, 132, 104), smoothstep(.3f, .75f, v.normal.y)), 1);
                append(nook, stone,
                       transform *
                           glm::translate(glm::mat4(1), glm::vec3(float(k) * 1.1f - .55f, .75f, .15f * float(k))));
            }
        }
        append(garden, nook);
        addStatic(physics, nook);
    }
    // Two small seating alcoves face the sanctuary approach. These are purposeful groups,
    // not a repeated ring of disconnected marker posts around every building.
    for (const glm::vec2 centre : std::array<glm::vec2, 2>{{{-112, 28}, {-104, 36}}}) {
        const float floor = y(centre);
        for (float side : {-1.0f, 1.0f}) {
            const glm::vec3 at(centre.x + side * .65f, floor + .22f, centre.y);
            add(masonry, render::boxMesh({.15f, .24f, .3f}), at, ivory * .82f);
        }
        add(masonry, chamferedBlock({1.0f, .10f, .4f}, .05f), {centre.x, floor + .48f, centre.y}, ivory);
        addBox(physics, glm::vec3(centre.x, floor + .48f, centre.y) + altitude, {1.0f, .10f, .4f}, 0);
    }
    auto finish = [&](MeshData& mesh, render::MaterialId material, const char* name, bool shadows = true) {
        if (mesh.indices.empty()) return;
        computeNormals(mesh);
        computeTangents(mesh);
        if (auto id = scene.addMesh(mesh.desc(name)))
            scene.addInstance({.mesh = *id, .material = material, .castsShadows = shadows});
    };
    finish(masonry, m_stoneMaterial, "region masonry");
    const auto roofMat = scene.addMaterial({.roughness = .72f});
    const auto trimMat = scene.addMaterial({.roughness = .48f, .metallic = .35f});
    finish(roofs, roofMat, "patinated bell roofs");
    finish(trim, trimMat, "sun and bell details");
    finish(garden, m_terrain, "garden moss", false);
}

void World::layoutGarden() {
    // Two unique trees, each belongs to a place: a willow shelters the music
    // courtyard; a different wind-carved cypress marks the northern lookout.
    m_landmarks.meadowGlade = {-65, terrainHeight({-65, 13}).value_or(0) + .035f, 13};
    m_clear.emplace_back(glm::vec2(-65, 13), 4.9f);
    const glm::vec2 court(-79, 2);
    const float floor = terrainHeight(court).value_or(0) + .12f;
    m_landmarks.musicCourt = {court.x, floor, court.y};
    m_landmarks.musician = {court.x + .1f, floor, court.y + 1.4f};
    m_garden.push_back({court + glm::vec2(-3.7f, 2.0f), 6.5f, .7f, true, floor});
    m_garden.push_back({court + glm::vec2(1.7f, 1.8f), 3.2f, -.3f, false, floor});
    m_clear.emplace_back(court, 5.8f);
    m_clear.emplace_back(glm::vec2(-176, 100), 1.8f);
    quench::log::info("game", "music courtyard: {:.2f} {:.2f} {:.2f}", court.x, floor, court.y);
}

void World::layoutChimes() {
    // Searches stay in authored districts, but each find is off the main route.
    // Prefer terrain occlusion; where the meadow is flat, make a small stone nook.
    const std::array<glm::vec2, 12> districts = {{{-12, 12},
                                                  {-8, -23},
                                                  {-31, -14},
                                                  {-75, -5},
                                                  {-94, 30},
                                                  {-145, 75},
                                                  {-165, 96},
                                                  {-106, 153},
                                                  {122, 52},
                                                  {148, 83},
                                                  {192, 129},
                                                  {134, 239}}};
    for (std::size_t index = 0; index < districts.size(); ++index) {
        if (index == 10) {
            m_landmarks.chimes.push_back(m_landmarks.caveCentre + glm::vec3(1.9f, .95f, 3));
            continue;
        }
        float best = 1e9f;
        glm::vec2 chosen = districts[index], toward(0, 1);
        bool hidden = false;
        for (int dz = -16; dz <= 16; dz += 2)
            for (int dx = -16; dx <= 16; dx += 2) {
                const auto p = districts[index] + glm::vec2(dx, dz);
                const auto h = terrainHeight(p);
                if (!h || terrainSlope(p) > .3f) continue;
                float distance = 1e9f;
                glm::vec2 nearest(0);
                for (const auto& path : m_paths)
                    for (std::size_t j = 1; j < path.points.size(); ++j) {
                        const auto a = path.points[j - 1], d = path.points[j] - a;
                        const auto q =
                            a + d * std::clamp(glm::dot(p - a, d) / std::max(.001f, glm::dot(d, d)), 0.0f, 1.0f);
                        const float r = glm::distance(p, q);
                        if (r < distance) {
                            distance = r;
                            nearest = q;
                        }
                    }
                if (distance < 6 || distance > 18) continue;
                bool clear =
                    glm::distance(p, glm::vec2(m_landmarks.pond.x, m_landmarks.pond.z)) > m_landmarks.pondRadius + 2;
                for (auto pad : m_foundations)
                    if (glm::distance(p, glm::vec2(pad.x, pad.y)) < pad.z + 3) clear = false;
                for (auto other : m_landmarks.chimes)
                    if (glm::distance(p, glm::vec2(other.x, other.z)) < 7) clear = false;
                // A find and its surrounding space must be on the same solid terrace.
                for (auto d : std::array<glm::vec2, 4>{{{2, 0}, {-2, 0}, {0, 2}, {0, -2}}}) {
                    const auto edge = terrainHeight(p + d);
                    if (!edge || std::abs(*edge - *h) > .65f) clear = false;
                }
                if (!clear) continue;
                const float eye = terrainHeight(nearest).value_or(*h) + 1.2f;
                bool occluded = false;
                for (int j = 2; j < 18; ++j) {
                    const float t = float(j) / 20;
                    if (terrainHeight(glm::mix(nearest, p, t)).value_or(-100) > glm::mix(eye, *h + 1.15f, t) + .12f)
                        occluded = true;
                }
                const float score = float(dx * dx + dz * dz) + distance * 2 - (occluded ? 300.0f : 0.0f);
                if (score < best) {
                    best = score;
                    chosen = p;
                    toward = glm::normalize(nearest - p);
                    hidden = occluded;
                }
            }
        const float h = terrainHeight(chosen).value_or(0);
        m_landmarks.chimes.push_back({chosen.x, h + .95f, chosen.y});
        m_clear.emplace_back(chosen, .65f);
        if (!hidden) m_cacheCovers.push_back({chosen + toward * 2.5f, std::atan2(toward.x, toward.y), int(index % 3)});
        quench::log::info("game", "hidden chime {}: {:.1f} {:.2f} {:.1f}, terrain cover {}", index, chosen.x, h,
                          chosen.y, hidden);
    }
}

Result<void> World::loadArchitecture(render::SceneAssets& assets, quench::PhysicsWorld& physics) {
    // The renderer and physics use the same cooked vertices and transform, including the hollow doors.
    auto bytes = quench::fs::readAsset("sanctuary.qmodel");
    if (!bytes) return quench::Error{bytes.error()};
    auto model = quench::assets::readModel(*bytes);
    if (!model) return quench::Error{model.error()};
    constexpr float scale = 7.0f;
    const glm::vec3 base(-104, terrainHeight({-104, 28}).value_or(0) + .36f - model->boundsMin.y * scale, 28);
    const auto transform = position(base) * glm::scale(glm::mat4(1), glm::vec3(scale));
    assets.addModel(assets.loadModel("sanctuary.qmodel"), transform);
    for (const auto& part : model->meshes) {
        MeshData collision;
        for (const auto& v : part.vertices)
            collision.vertices.push_back({.position = glm::vec3(transform * glm::vec4(v.position, 1))});
        const auto& lod = part.lods.front();
        collision.indices.assign(part.indices.begin() + lod.firstIndex,
                                 part.indices.begin() + lod.firstIndex + lod.indexCount);
        addStatic(physics, collision);
    }
    // Load each source once; repeated instances share the cooked model and textures.
    for (bool tree : {true, false}) {
        const char* path = tree ? "willow.qmodel" : "harp.qmodel";
        const auto bytes = quench::fs::readAsset(path);
        if (!bytes) return quench::Error{bytes.error()};
        const auto data = quench::assets::readModel(*bytes);
        if (!data) return quench::Error{data.error()};
        const auto ref = assets.loadModel(path);
        for (const auto& object : m_garden) {
            if (object.tree != tree) continue;
            const float scale = object.height / std::max(.01f, data->boundsMax.y - data->boundsMin.y);
            const glm::vec3 centre((data->boundsMin.x + data->boundsMax.x) * .5f, data->boundsMin.y,
                                   (data->boundsMin.z + data->boundsMax.z) * .5f);
            const auto m = position({object.at.x, object.floor - (tree ? .55f : 0), object.at.y}) *
                           glm::rotate(glm::mat4(1), object.yaw, glm::vec3(0, 1, 0)) *
                           glm::scale(glm::mat4(1), glm::vec3(scale)) * glm::translate(glm::mat4(1), -centre);
            assets.addModel(ref, m);
            for (const auto& part : data->meshes) {
                MeshData collision;
                for (const auto& v : part.vertices)
                    collision.vertices.push_back({.position = glm::vec3(m * glm::vec4(v.position, 1))});
                const auto& lod = part.lods.front();
                collision.indices.assign(part.indices.begin() + lod.firstIndex,
                                         part.indices.begin() + lod.firstIndex + lod.indexCount);
                addStatic(physics, collision);
            }
        }
    }
    auto placedAsset = [&](const char* path, glm::vec3 at, float height, float yaw,
                           bool collision) -> Result<render::ModelInstanceId> {
        const auto bytes = quench::fs::readAsset(path);
        if (!bytes) return Error{bytes.error()};
        const auto data = quench::assets::readModel(*bytes);
        if (!data) return Error{data.error()};
        const float size = height / std::max(.01f, data->boundsMax.y - data->boundsMin.y);
        const glm::vec3 centre((data->boundsMin.x + data->boundsMax.x) * .5f, data->boundsMin.y,
                               (data->boundsMin.z + data->boundsMax.z) * .5f);
        const auto m = position(at) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0)) *
                       glm::scale(glm::mat4(1), glm::vec3(size)) * glm::translate(glm::mat4(1), -centre);
        const auto instance = assets.addModel(assets.loadModel(path), m);
        if (collision)
            for (const auto& part : data->meshes) {
                MeshData mesh;
                for (const auto& v : part.vertices)
                    mesh.vertices.push_back({.position = glm::vec3(m * glm::vec4(v.position, 1))});
                const auto& lod = part.lods.front();
                mesh.indices.assign(part.indices.begin() + lod.firstIndex,
                                    part.indices.begin() + lod.firstIndex + lod.indexCount);
                addStatic(physics, mesh);
            }
        return instance;
    };
    for (const auto& item : std::array<std::tuple<const char*, glm::vec3, float, float>, 7>{
             {{"cave_entrance.qmodel", {202, m_landmarks.caveCentre.y - .16f, 133.8f}, 7.2f, 0},
              {"starting_ruins.qmodel", {4, terrainHeight({4, -22}).value_or(0) - .12f, -22}, 4.0f, 0},
              {"cypress.qmodel", {-176, terrainHeight({-176, 100}).value_or(0) - .5f, 100}, 6.5f, .6f},
              {"telescope.qmodel", {-170.7f, terrainHeight({-171, 97}).value_or(0) + .14f, 96.5f}, 2.3f, -1.3f},
              {"cottage.qmodel", m_landmarks.cottage, 6.0f, 0},
              {"sanctum.qmodel", m_landmarks.altar - glm::vec3(0, 1.23f, 0), 15.0f, kPi},
              {"lantern.qmodel", m_landmarks.cottage + glm::vec3(-2.0f, 0, -4.9f), 2.1f, 0}}}) {
        auto result = placedAsset(std::get<0>(item), std::get<1>(item), std::get<2>(item), std::get<3>(item), true);
        if (!result) return Error{result.error()};
    }
    for (auto at : m_landmarks.lanterns) {
        auto result = placedAsset("lantern.qmodel", at - glm::vec3(0, 1.05f, 0), 1.5f, .2f, true);
        if (!result) return Error{result.error()};
    }
    // Every canopy is a distinct source, tied to a specific garden. Root flares
    // and exported earth skirts are buried below the levelled planting ground.
    for (const auto& item : std::array<std::tuple<const char*, glm::vec2, float, float, float>, 5>{
             {{"pear_tree.qmodel", {71, 64}, 4.2f, 1.4f, .4f},
              {"silver_birch.qmodel", {0, 8}, 5.6f, .6f, .48f},
              {"ancient_oak.qmodel", {139, 240}, 7.2f, .3f, .85f},
              {"fan_palm.qmodel", {139, 79}, 5.6f, 1.8f, .50f},
              {"lantern_tree.qmodel", {124, 239}, 5.4f, -.9f, .48f}}}) {
        const auto p = std::get<1>(item);
        auto result = placedAsset(std::get<0>(item), {p.x, terrainHeight(p).value_or(0) - std::get<4>(item), p.y},
                                  std::get<2>(item), std::get<3>(item), true);
        if (!result) return Error{result.error()};
    }
    struct Plant {
        const char* file;
        glm::vec3 at;
        float height, yaw, bury;
    };
    const auto home = m_landmarks.cottage;
    for (const auto& p : std::array<Plant, 19>{
             {{"hydrangea.qmodel", home + glm::vec3(-3.4f, .28f, -3.4f), 1.1f, .3f, .18f},
              {"hydrangea.qmodel", home + glm::vec3(4.2f, .28f, -2.0f), .86f, 2.2f, .14f},
              {"fern.qmodel", home + glm::vec3(-3.5f, .47f, .2f), .65f, -.5f, .10f},
              {"blue_heather.qmodel", home + glm::vec3(-3.5f, .47f, -.7f), .65f, 1, .15f},
              {"berry_bush.qmodel", home + glm::vec3(3.4f, .47f, -4.0f), .8f, .7f, .12f},
              {"fern.qmodel", {-82, terrainHeight({-82, 4}).value_or(0), 4}, 1.0f, .2f, .18f},
              {"blue_heather.qmodel", {-83, terrainHeight({-83, 1}).value_or(0), 1}, .9f, 1.4f, .22f},
              {"berry_bush.qmodel", {72, terrainHeight({72, 65}).value_or(0), 65}, 1.3f, .5f, .2f},
              {"fern.qmodel", {138, terrainHeight({138, 79}).value_or(0), 79}, 1.1f, -1, .20f},
              {"cushion_moss.qmodel", {140, terrainHeight({140, 241}).value_or(0), 241}, .65f, 1.1f, .25f},
              {"blue_heather.qmodel", {125, terrainHeight({125, 238}).value_or(0), 238}, 1.1f, -1.5f, .25f},
              {"hydrangea.qmodel", {56, terrainHeight({56, 56}).value_or(0), 56}, 1.0f, -.8f, .18f},
              {"cushion_moss.qmodel", m_landmarks.caveCentre + glm::vec3(-2, .015f, -2), .65f, .6f, .24f},
              {"fern.qmodel", m_landmarks.meadowGlade + glm::vec3(-4.9f, 0, 1.2f), .95f, .8f, .17f},
              {"blue_heather.qmodel", m_landmarks.meadowGlade + glm::vec3(-4.5f, 0, 3.8f), .75f, 1.6f, .20f},
              {"hydrangea.qmodel", m_landmarks.meadowGlade + glm::vec3(4.7f, 0, 3.4f), .9f, -1.1f, .16f},
              {"cushion_moss.qmodel", m_landmarks.meadowGlade + glm::vec3(4.9f, 0, 1.0f), .45f, .5f, .20f},
              {"fern.qmodel", m_landmarks.caveEntrance + glm::vec3(-1.8f, 0, 1), .8f, 1.7f, .16f},
              {"blue_heather.qmodel", {-175, terrainHeight({-175, 100}).value_or(0), 100}, .85f, .4f, .2f}}}) {
        auto result = placedAsset(p.file, p.at - glm::vec3(0, p.bury, 0), p.height, p.yaw, false);
        if (!result) return Error{result.error()};
    }
    if (auto fossil =
            placedAsset("fossil.qmodel", m_landmarks.caveCentre + glm::vec3(-.6f, .03f, 1.4f), 1.1f, .35f, true);
        !fossil)
        return Error{fossil.error()};
    // A stationary sailboat in the starting pond. Set the keel below the water,
    // with the deck dry, and leave the centre clear for the water-hover route.
    m_shipPosition = m_landmarks.pond + glm::vec3(-2.2f, -.5f, .3f);
    if (auto ship = placedAsset("skyship.qmodel", m_shipPosition, 4.8f, .2f, true); !ship) return Error{ship.error()};
    quench::log::info("game", "region: sanctuary loaded with matching collision; six new exploration isles");
    return {};
}
} // namespace cloudsong
