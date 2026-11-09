#pragma once

// Small helpers shared by the game's files: a deterministic random generator, value noise, mesh building in the
// engine's vertex format and transforms.

#include <quench/core/Result.h>
#include <quench/render/MeshBuilder.h>
#include <quench/render/Scene.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cloudsong {

using quench::Error;
using quench::Result;
using quench::render::MeshData;
using quench::render::Vertex;

inline constexpr float kPi = 3.14159265f;
inline constexpr float kTau = 6.28318531f;

// xorshift32: the same sequence on every platform.
struct Random {
    std::uint32_t state = 0x9e3779b9u;
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    float uniform(float a, float b) { return a + (b - a) * uniform(); }
    int range(int count) { return std::min(int(uniform() * float(count)), count - 1); }
};

inline std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hash01(int x, int y, std::uint32_t seed) {
    const std::uint32_t h = hash32(std::uint32_t(x) * 0x8da6b343u ^ hash32(std::uint32_t(y) * 0xd8163841u ^ seed));
    return float(h >> 8) * (1.0f / 16777216.0f);
}

inline float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

inline float smoothstep(float a, float b, float x) { return smooth01(std::clamp((x - a) / (b - a), 0.0f, 1.0f)); }

// Value noise in 0..1.
inline float valueNoise(glm::vec2 p, std::uint32_t seed) {
    const glm::vec2 i = glm::floor(p);
    const glm::vec2 f = p - i;
    const int x = int(i.x), y = int(i.y);
    const float a = hash01(x, y, seed), b = hash01(x + 1, y, seed);
    const float c = hash01(x, y + 1, seed), d = hash01(x + 1, y + 1, seed);
    const float u = smooth01(f.x), v = smooth01(f.y);
    return glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
}

// Fractal value noise in about -1..1.
inline float fbm(glm::vec2 p, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amplitude * (valueNoise(p, seed + std::uint32_t(o) * 1013u) * 2.0f - 1.0f);
        norm += amplitude;
        amplitude *= 0.5f;
        p = p * 2.03f + glm::vec2(17.1f, 9.7f);
    }
    return sum / norm;
}

inline glm::mat4 placed(glm::vec3 position, float yaw = 0.0f, glm::vec3 scale = glm::vec3(1.0f)) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(m, scale);
}

// Appends `part` transformed by `m` (normals by its inverse transpose), with its vertex colors multiplied by `color`.
inline void append(MeshData& out, const MeshData& part, const glm::mat4& m = glm::mat4(1.0f),
                   glm::vec4 color = glm::vec4(1.0f)) {
    const glm::mat3 n = glm::transpose(glm::inverse(glm::mat3(m)));
    const auto first = std::uint32_t(out.vertices.size());
    for (Vertex v : part.vertices) {
        v.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(n * v.normal);
        const glm::vec3 t = glm::mat3(m) * glm::vec3(v.tangent);
        if (glm::dot(t, t) > 1e-12f) v.tangent = glm::vec4(glm::normalize(t), v.tangent.w);
        v.color *= color;
        out.vertices.push_back(v);
    }
    for (const std::uint32_t i : part.indices) out.indices.push_back(first + i);
}

// Smooth normals from the triangles (area weighted), for meshes built vertex by vertex.
inline void computeNormals(MeshData& mesh) {
    for (Vertex& v : mesh.vertices) v.normal = glm::vec3(0.0f);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : mesh.vertices) {
        const float l = glm::length(v.normal);
        v.normal = l > 1e-12f ? v.normal / l : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

inline MeshData chamferedBlock(glm::vec3 half, float bevel) {
    MeshData mesh;
    bevel = std::min(bevel, std::min({half.x, half.y, half.z}) * .65f);
    for (int ring = 0; ring < 4; ++ring) {
        const float inset = ring == 0 || ring == 3 ? bevel : 0;
        const float x = half.x - inset, z = half.z - inset;
        const float y = ring == 0 ? -half.y : ring == 1 ? -half.y + bevel : ring == 2 ? half.y - bevel : half.y;
        const float corner = std::max(.001f, bevel - inset * .5f);
        for (const auto p : std::array<glm::vec2, 8>{{{-x + corner, -z},
                                                      {x - corner, -z},
                                                      {x, -z + corner},
                                                      {x, z - corner},
                                                      {x - corner, z},
                                                      {-x + corner, z},
                                                      {-x, z - corner},
                                                      {-x, -z + corner}}})
            mesh.vertices.push_back({.position = {p.x, y, p.y}});
    }
    for (std::uint32_t ring = 0; ring < 3; ++ring)
        for (std::uint32_t j = 0; j < 8; ++j) {
            const auto a = ring * 8 + j, b = ring * 8 + (j + 1) % 8;
            mesh.indices.insert(mesh.indices.end(), {a, a + 8, b, b, a + 8, b + 8});
        }
    for (std::uint32_t j = 1; j + 1 < 8; ++j) {
        mesh.indices.insert(mesh.indices.end(), {0, j, j + 1});
        mesh.indices.insert(mesh.indices.end(), {24, 24 + j + 1, 24 + j});
    }
    computeNormals(mesh);
    return mesh;
}

// A closed surface of revolution around +y: `profile` gives (radius, y) from the bottom pole to the top pole.
inline MeshData lathe(const std::vector<glm::vec2>& profile, int segments, glm::vec4 color = glm::vec4(1.0f)) {
    MeshData m;
    const int rows = int(profile.size());
    for (int r = 0; r < rows; ++r)
        for (int s = 0; s <= segments; ++s) {
            const float a = kTau * float(s) / float(segments);
            const glm::vec3 p(profile[r].x * std::sin(a), profile[r].y, profile[r].x * std::cos(a));
            m.vertices.push_back({.position = p,
                                  .uv = {float(s) / float(segments), float(r) / float(rows - 1)},
                                  .color = color,
                                  .tangent = {std::cos(a), 0.0f, -std::sin(a), 1.0f}});
        }
    for (int r = 0; r + 1 < rows; ++r)
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = std::uint32_t(r * (segments + 1) + s);
            const std::uint32_t b = a + std::uint32_t(segments + 1);
            m.indices.insert(m.indices.end(), {a, a + 1, b + 1, a, b + 1, b});
        }
    computeNormals(m);
    return m;
}

// An ellipsoid of radii r around the origin.
inline MeshData ellipsoid(glm::vec3 r, int rings = 10, int segments = 16, glm::vec4 color = glm::vec4(1.0f)) {
    std::vector<glm::vec2> profile;
    for (int i = 0; i <= rings; ++i) {
        const float t = kPi * float(i) / float(rings);
        profile.push_back({std::max(std::sin(t), 1e-4f), -std::cos(t)});
    }
    MeshData m = lathe(profile, segments, color);
    for (Vertex& v : m.vertices) v.position *= r;
    computeNormals(m);
    return m;
}

// A rotation that takes +y to `dir`.
inline glm::mat4 alignY(glm::vec3 dir) {
    const glm::vec3 y = glm::normalize(dir);
    const glm::vec3 helper = std::abs(y.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(helper, y));
    const glm::vec3 z = glm::cross(x, y);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

inline glm::vec3 lerpColor(glm::vec3 a, glm::vec3 b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }

// sRGB (0..255 per channel) to linear.
inline glm::vec3 srgb(int r, int g, int b) {
    auto f = [](int c) {
        const float x = float(c) / 255.0f;
        return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    };
    return {f(r), f(g), f(b)};
}

// Exponential approach of `value` to `target` with a time constant (s).
inline float approach(float value, float target, float timeConstant, float dt) {
    return target + (value - target) * std::exp(-dt / std::max(timeConstant, 1e-4f));
}
inline glm::vec3 approach(glm::vec3 value, glm::vec3 target, float timeConstant, float dt) {
    return target + (value - target) * std::exp(-dt / std::max(timeConstant, 1e-4f));
}

inline float wrapAngle(float a) {
    while (a > kPi) a -= kTau;
    while (a < -kPi) a += kTau;
    return a;
}

} // namespace cloudsong
