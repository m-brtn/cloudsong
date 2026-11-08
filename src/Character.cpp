#include "Character.h"

#include <quench/animation/Ik.h>

namespace cloudsong {

namespace {

using quench::render::SkinVertex;

// The bind pose (model space, a figure 1.2 m tall with its hood, facing +z).
constexpr std::array<glm::vec3, kBodyJoints> kRest = {{
    {0.0f, 0.0f, 0.0f},       // root
    {0.0f, 0.50f, 0.0f},      // hips
    {0.0f, 0.62f, 0.0f},      // spine
    {0.0f, 0.74f, 0.0f},      // chest
    {0.0f, 0.86f, 0.0f},      // neck
    {0.0f, 0.91f, 0.0f},      // head
    {0.065f, 0.48f, 0.0f},    // left hip
    {0.065f, 0.27f, 0.012f},  // left knee
    {0.065f, 0.06f, 0.0f},    // left ankle
    {-0.065f, 0.48f, 0.0f},   // right hip
    {-0.065f, 0.27f, 0.012f}, // right knee
    {-0.065f, 0.06f, 0.0f},   // right ankle
    {0.115f, 0.81f, 0.0f},    // left shoulder
    {0.135f, 0.655f, -0.01f}, // left elbow
    {0.145f, 0.52f, 0.01f},   // left wrist
    {-0.115f, 0.81f, 0.0f},   // right shoulder
    {-0.135f, 0.655f, -0.01f},
    {-0.145f, 0.52f, 0.01f},
}};

constexpr std::array<int, kBodyJoints> kParent = {
    -1,    kRoot,   kHips, kSpine, kChest, kNeck,     kHips,  kLUpLeg, kLLeg,
    kHips, kRUpLeg, kRLeg, kChest, kLArm,  kLForearm, kChest, kRArm,   kRForearm,
};

int capeJoint(int row, int col) { return kBodyJoints + row * kCapeCols + col; }

// The cape at rest: v from the collar (0) to its hem (1), u across from the figure's right (0) to its left (1),
// wrapped around the shoulders and the back.
glm::vec3 capeRest(float u, float v, float length) {
    const float span = glm::mix(4.3f, 3.5f, std::sqrt(v));
    const float a = (u - 0.5f) * span;
    const float rx = glm::mix(0.128f, 0.25f, v);
    const float rz = glm::mix(0.1f, 0.19f, v);
    return {-std::sin(a) * rx, 0.865f - v * 0.52f * length, -std::cos(a) * rz - 0.012f - 0.04f * v};
}

SkinVertex rigid(int joint) {
    SkinVertex s;
    s.joints = {std::uint16_t(joint), 0, 0, 0};
    s.weights = {65535, 0, 0, 0};
    return s;
}

SkinVertex blend(int a, int b, float t) {
    SkinVertex s;
    t = std::clamp(t, 0.0f, 1.0f);
    s.joints = {std::uint16_t(a), std::uint16_t(b), 0, 0};
    const auto wb = std::uint16_t(std::lround(t * 65535.0f));
    s.weights = {std::uint16_t(65535 - wb), wb, 0, 0};
    return s;
}

void add(CharacterMesh& out, const MeshData& part, const std::function<SkinVertex(const Vertex&)>& weight) {
    const auto first = std::uint32_t(out.mesh.vertices.size());
    for (const Vertex& v : part.vertices) {
        out.mesh.vertices.push_back(v);
        out.skin.push_back(weight(v));
    }
    for (const std::uint32_t i : part.indices) out.mesh.indices.push_back(first + i);
}

// A tapered tube from a to b (closed with small caps), weighted to `joint`, blended with `parent` near a and with
// `child` near b.
void limb(CharacterMesh& out, glm::vec3 a, glm::vec3 b, float ra, float rb, glm::vec3 color, int joint, int parent,
          int child) {
    std::vector<glm::vec2> profile;
    const float len = glm::length(b - a);
    profile.push_back({1e-4f, -ra * 0.6f});
    for (int i = 0; i <= 6; ++i) {
        const float t = float(i) / 6.0f;
        profile.push_back({glm::mix(ra, rb, t), t * len});
    }
    profile.push_back({1e-4f, len + rb * 0.6f});
    MeshData tube = lathe(profile, 10, glm::vec4(color, 1.0f));
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), a) * alignY(b - a);
    MeshData placedTube;
    append(placedTube, tube, m);
    add(out, placedTube, [&](const Vertex& v) {
        const float t = glm::dot(v.position - a, b - a) / (len * len);
        if (t < 0.15f && parent >= 0) return blend(parent, joint, 0.5f + t / 0.3f);
        if (t > 0.85f && child >= 0) return blend(joint, child, (t - 0.85f) / 0.3f);
        return rigid(joint);
    });
}

void solid(CharacterMesh& out, const MeshData& part, const glm::mat4& m, glm::vec3 color, int joint) {
    MeshData p;
    append(p, part, m, glm::vec4(color, 1.0f));
    add(out, p, [&](const Vertex&) { return rigid(joint); });
}

} // namespace

CharacterMesh Character::buildMesh(const CharacterLook& look) {
    CharacterMesh out;
    // The tunic: from a flared hem over the thighs to the neck, bending with the hips, the spine and the chest.
    {
        const std::vector<glm::vec2> profile = {{1e-4f, 0.33f},  {0.125f, 0.335f}, {0.115f, 0.40f}, {0.098f, 0.50f},
                                                {0.094f, 0.60f}, {0.104f, 0.72f},  {0.095f, 0.79f}, {0.06f, 0.85f},
                                                {0.042f, 0.89f}, {1e-4f, 0.91f}};
        MeshData torso = lathe(profile, 18, glm::vec4(look.body, 1.0f));
        for (Vertex& v : torso.vertices) v.position.z *= 0.82f;
        computeNormals(torso);
        add(out, torso, [](const Vertex& v) {
            const float y = v.position.y;
            if (y < 0.56f) return rigid(kHips);
            if (y < 0.68f) return blend(kHips, kSpine, (y - 0.56f) / 0.12f);
            if (y < 0.76f) return blend(kSpine, kChest, (y - 0.68f) / 0.08f);
            if (y < 0.86f) return rigid(kChest);
            return blend(kChest, kNeck, (y - 0.86f) / 0.04f);
        });
    }
    // The head: a round pale face with two dark eyes, under a hood whose tip falls back.
    const glm::vec3 headCentre(0.0f, 1.01f, 0.012f);
    solid(out, ellipsoid({0.112f, 0.118f, 0.108f}, 12, 18), glm::translate(glm::mat4(1.0f), headCentre), look.face,
          kHead);
    for (const float side : {-1.0f, 1.0f})
        solid(out, ellipsoid({0.014f, 0.022f, 0.01f}, 6, 8),
              glm::translate(glm::mat4(1.0f), headCentre + glm::vec3(side * 0.042f, 0.004f, 0.1f)), srgb(40, 34, 44),
              kHead);
    {
        MeshData hood = ellipsoid({0.142f, 0.15f, 0.148f}, 14, 22);
        MeshData shell;
        shell.vertices = hood.vertices;
        // The face opening: no triangle whose corners all look forward below the brow.
        for (std::size_t i = 0; i + 2 < hood.indices.size(); i += 3) {
            bool open = true;
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 p = glm::normalize(hood.vertices[hood.indices[i + std::size_t(k)]].position);
                open = open && p.z > 0.42f && p.y < 0.55f && p.y > -0.75f;
            }
            if (!open)
                shell.indices.insert(shell.indices.end(), {hood.indices[i], hood.indices[i + 1], hood.indices[i + 2]});
        }
        solid(out, shell, glm::translate(glm::mat4(1.0f), headCentre + glm::vec3(0.0f, 0.02f, -0.018f)), look.hood,
              kHead);
        // The tip: a tube along a curve up and back, tapering to a point.
        const int n = 10;
        const float tipLength = look.longHood ? 1.0f : 0.55f;
        std::vector<glm::vec3> spine;
        for (int i = 0; i <= n; ++i) {
            const float t = float(i) / float(n);
            spine.push_back(headCentre + glm::vec3(0.0f, 0.10f + 0.10f * t - 0.12f * t * t * tipLength,
                                                   -0.06f - 0.26f * t * tipLength));
        }
        MeshData tip;
        const int sides = 10;
        for (int i = 0; i <= n; ++i) {
            const glm::vec3 dir =
                glm::normalize(spine[std::size_t(std::min(i + 1, n))] - spine[std::size_t(std::max(i - 1, 0))]);
            const glm::mat4 frame = alignY(dir);
            const float t = float(i) / float(n);
            const float r = 0.085f * (1.0f - t) * (1.0f - 0.3f * t) + 0.004f;
            for (int s = 0; s <= sides; ++s) {
                const float a = kTau * float(s) / float(sides);
                const glm::vec3 offset =
                    glm::vec3(frame * glm::vec4(std::cos(a) * r, 0.0f, std::sin(a) * r * 0.8f, 0.0f));
                tip.vertices.push_back(
                    {.position = spine[std::size_t(i)] + offset, .color = glm::vec4(look.hood, 1.0f)});
            }
        }
        for (int i = 0; i < n; ++i)
            for (int s = 0; s < sides; ++s) {
                const auto a = std::uint32_t(i * (sides + 1) + s);
                const auto b = a + std::uint32_t(sides + 1);
                tip.indices.insert(tip.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
            }
        computeNormals(tip);
        add(out, tip, [](const Vertex&) { return rigid(kHead); });
    }
    // Legs, boots, arms (the tunic's sleeves) and hands.
    for (const int side : {0, 1}) {
        const int up = side == 0 ? kLUpLeg : kRUpLeg;
        limb(out, kRest[std::size_t(up)], kRest[std::size_t(up + 1)], 0.036f, 0.03f, look.legs, up, kHips, up + 1);
        limb(out, kRest[std::size_t(up + 1)], kRest[std::size_t(up + 2)], 0.03f, 0.026f, look.legs, up + 1, up, up + 2);
        solid(out, ellipsoid({0.038f, 0.034f, 0.062f}, 8, 12),
              glm::translate(glm::mat4(1.0f), kRest[std::size_t(up + 2)] + glm::vec3(0.0f, -0.025f, 0.022f)),
              look.boots, up + 2);
        const int arm = side == 0 ? kLArm : kRArm;
        limb(out, kRest[std::size_t(arm)], kRest[std::size_t(arm + 1)], 0.032f, 0.027f, look.body, arm, kChest,
             arm + 1);
        limb(out, kRest[std::size_t(arm + 1)], kRest[std::size_t(arm + 2)], 0.027f, 0.024f, look.body, arm + 1, arm,
             arm + 2);
        solid(out, ellipsoid({0.026f, 0.032f, 0.024f}, 6, 10),
              glm::translate(glm::mat4(1.0f), kRest[std::size_t(arm + 2)] + glm::vec3(0.0f, -0.02f, 0.0f)), look.face,
              arm + 2);
    }
    // The cape: a grid three times finer than its joints, each vertex weighted bilinearly to the four joints
    // around it. uv: (2 + across, down), which the character surface reads as the cape (its marks and inner side).
    {
        constexpr int kSub = 3;
        constexpr int cols = (kCapeCols - 1) * kSub + 1;
        constexpr int rows = (kCapeRows - 1) * kSub + 1;
        MeshData cape;
        std::vector<SkinVertex> weights;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const float u = float(c) / float(cols - 1);
                const float v = float(r) / float(rows - 1);
                cape.vertices.push_back({.position = capeRest(u, v, look.capeLength),
                                         .uv = {2.0f + u, v},
                                         .color = glm::vec4(look.cape, 1.0f)});
                const float gu = u * float(kCapeCols - 1), gv = v * float(kCapeRows - 1);
                const int c0 = std::min(int(gu), kCapeCols - 2), r0 = std::min(int(gv), kCapeRows - 2);
                const float fu = gu - float(c0), fv = gv - float(r0);
                const std::array<float, 4> w = {(1 - fu) * (1 - fv), fu * (1 - fv), (1 - fu) * fv, fu * fv};
                SkinVertex s;
                s.joints = {std::uint16_t(capeJoint(r0, c0)), std::uint16_t(capeJoint(r0, c0 + 1)),
                            std::uint16_t(capeJoint(r0 + 1, c0)), std::uint16_t(capeJoint(r0 + 1, c0 + 1))};
                int sum = 0;
                for (int k = 0; k < 4; ++k) {
                    s.weights[std::size_t(k)] = std::uint16_t(std::lround(w[std::size_t(k)] * 65535.0f));
                    sum += s.weights[std::size_t(k)];
                }
                s.weights[0] = std::uint16_t(std::clamp(int(s.weights[0]) + 65535 - sum, 0, 65535));
                weights.push_back(s);
            }
        for (int r = 0; r + 1 < rows; ++r)
            for (int c = 0; c + 1 < cols; ++c) {
                const auto a = std::uint32_t(r * cols + c);
                const auto b = a + std::uint32_t(cols);
                // Counter-clockwise seen from behind the figure (the cape's outside).
                cape.indices.insert(cape.indices.end(), {a, a + 1, b + 1, a, b + 1, b});
            }
        computeNormals(cape);
        // Outward: away from the body.
        glm::vec3 probe = cape.vertices[std::size_t(cols * (rows / 2) + cols / 2)].normal;
        if (probe.z > 0.0f) {
            for (std::size_t i = 0; i + 2 < cape.indices.size(); i += 3)
                std::swap(cape.indices[i + 1], cape.indices[i + 2]);
            computeNormals(cape);
        }
        const auto first = std::uint32_t(out.mesh.vertices.size());
        for (std::size_t i = 0; i < cape.vertices.size(); ++i) {
            out.mesh.vertices.push_back(cape.vertices[i]);
            out.skin.push_back(weights[i]);
        }
        for (const std::uint32_t i : cape.indices) out.mesh.indices.push_back(first + i);
    }
    return out;
}

Character::Character(const CharacterLook& look) : m_look(look) {
    for (int j = 0; j < kBodyJoints; ++j) {
        m_bind[std::size_t(j)] = kRest[std::size_t(j)];
        m_parent[std::size_t(j)] = kParent[std::size_t(j)];
    }
    for (int r = 0; r < kCapeRows; ++r)
        for (int c = 0; c < kCapeCols; ++c) {
            const glm::vec3 p =
                capeRest(float(c) / float(kCapeCols - 1), float(r) / float(kCapeRows - 1), look.capeLength);
            m_bind[std::size_t(capeJoint(r, c))] = p;
            m_clothRest[std::size_t(r * kCapeCols + c)] = p;
        }
    m_rowLength = glm::distance(m_clothRest[0], m_clothRest[kCapeCols]);
    for (int r = 0; r < kCapeRows; ++r)
        m_rowWidth[std::size_t(r)] =
            glm::distance(m_clothRest[std::size_t(r * kCapeCols)], m_clothRest[std::size_t(r * kCapeCols + 1)]);
    m_palette.assign(kJointCount, glm::mat4(1.0f));
    for (glm::mat4& m : m_model) m = glm::mat4(1.0f);
}

void Character::setPlacement(glm::vec3 feet, float yaw, float pitch) {
    m_feet = feet;
    m_yaw = yaw;
    m_pitch = pitch;
    const float pivot = 0.62f * m_look.scale;
    m_world = glm::translate(glm::mat4(1.0f), feet) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
              glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, pivot, 0.0f)) *
              glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
              glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -pivot, 0.0f)) *
              glm::scale(glm::mat4(1.0f), glm::vec3(m_look.scale));
}

glm::vec3 Character::jointWorld(Joint joint) const { return glm::vec3(m_world * m_model[std::size_t(joint)][3]); }

void Character::resetCape() {
    for (std::size_t i = 0; i < m_cloth.size(); ++i) {
        m_cloth[i] = glm::vec3(m_world * glm::vec4(m_clothRest[i], 1.0f));
        m_clothPrev[i] = m_cloth[i];
    }
    m_clothValid = true;
    m_lastFeet = m_feet;
}

void Character::forward(const std::array<glm::quat, kBodyJoints>& local, glm::vec3 hipsOffset) {
    for (int j = 0; j < kBodyJoints; ++j) {
        const int p = m_parent[std::size_t(j)];
        glm::vec3 offset = p < 0 ? kRest[std::size_t(j)] : kRest[std::size_t(j)] - kRest[std::size_t(p)];
        if (j == kHips) offset += hipsOffset;
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), offset) * glm::mat4_cast(local[std::size_t(j)]);
        m_model[std::size_t(j)] = p < 0 ? m : m_model[std::size_t(p)] * m;
    }
}

void Character::update(float dt, const PoseInput& in, const GroundQuery& ground) {
    m_time += dt;
    const float scale = m_look.scale;
    m_speed = approach(m_speed, in.speed / scale, 0.12f, dt);
    m_air = approach(m_air, in.grounded ? 0.0f : 1.0f, in.grounded ? 0.05f : 0.12f, dt);
    const float s = m_speed;
    const float run = smoothstep(1.6f, 3.6f, s);
    const float move = smoothstep(0.05f, 0.7f, s) * (1.0f - m_air);
    const float stride = glm::mix(0.8f, 1.45f, run);
    if (in.grounded) m_phase = std::fmod(m_phase + s / stride * kTau * dt, kTau * 64.0f);
    const float ph = m_phase;
    const int halfStep = int(std::floor((ph + 0.5f * kPi) / kPi));
    m_stepped = halfStep != m_halfStep && in.grounded && move > 0.3f && in.sit < 0.5f;
    m_halfStep = halfStep;
    const float walk = (1.0f - m_air) * (1.0f - in.sit);
    const float air = m_air * (1.0f - in.glide);
    const float glide = in.glide;

    // Euler angles per joint (x pitch, y yaw, z roll), summed from the gaits by their weights.
    std::array<glm::vec3, kBodyJoints> e{};
    glm::vec3 hips(0.0f);
    const float breath = std::sin(m_time * 1.7f);
    // Idle and the ground gaits.
    {
        const float swing = move * glm::mix(0.42f, 0.78f, run);
        const float armSwing = move * glm::mix(0.32f, 0.85f, run);
        const float lean = move * (0.04f + 0.16f * run);
        e[kHips] += glm::vec3(lean, 0.1f * move * std::sin(ph), 0.035f * move * std::cos(ph)) * walk;
        e[kSpine] += glm::vec3(0.05f * run * move, -0.08f * move * std::sin(ph), 0.0f) * walk;
        e[kChest] +=
            glm::vec3(0.025f * breath * (1.0f - move) + 0.04f * run * move, -0.06f * move * std::sin(ph), 0.0f) * walk;
        e[kHead] += glm::vec3(-0.8f * lean - 0.03f * breath * (1.0f - move), 0.0f, 0.0f) * walk;
        hips.y += (-0.012f * move + (0.01f + 0.018f * run) * move * std::cos(2.0f * ph)) * walk;
        for (int side = 0; side < 2; ++side) {
            const float sgn = side == 0 ? 1.0f : -1.0f;
            const float p = ph + (side == 0 ? 0.0f : kPi);
            const int up = side == 0 ? kLUpLeg : kRUpLeg;
            const float hip = -swing * std::sin(p) - 0.12f * run * move;
            const float knee = move * ((0.18f + 1.15f * run) * std::max(0.0f, std::cos(p)) + 0.1f) + 0.05f;
            e[std::size_t(up)] += glm::vec3(hip, 0.0f, sgn * 0.02f) * walk;
            e[std::size_t(up + 1)] += glm::vec3(knee, 0.0f, 0.0f) * walk;
            e[std::size_t(up + 2)] += glm::vec3(-0.7f * (hip + knee) + 0.05f, 0.0f, 0.0f) * walk;
            const int arm = side == 0 ? kLArm : kRArm;
            e[std::size_t(arm)] +=
                glm::vec3(armSwing * std::sin(p), 0.0f, sgn * (0.1f + 0.12f * run + 0.02f * breath)) * walk;
            e[std::size_t(arm + 1)] += glm::vec3(-(0.18f + 1.0f * run * move), 0.0f, 0.0f) * walk;
        }
    }
    // In the air: knees up, arms out, more so while rising.
    if (air > 0.0f) {
        const float rise = std::clamp(in.verticalSpeed / 4.0f, -1.0f, 1.0f);
        e[kLUpLeg] += glm::vec3(-0.75f, 0.0f, 0.03f) * air;
        e[kLLeg] += glm::vec3(1.1f, 0.0f, 0.0f) * air;
        e[kRUpLeg] += glm::vec3(-0.15f, 0.0f, -0.03f) * air;
        e[kRLeg] += glm::vec3(0.55f, 0.0f, 0.0f) * air;
        e[kLArm] += glm::vec3(-0.3f, 0.0f, 0.55f + 0.35f * rise) * air;
        e[kRArm] += glm::vec3(-0.3f, 0.0f, -0.55f - 0.35f * rise) * air;
        e[kLForearm] += glm::vec3(-0.4f, 0.0f, 0.0f) * air;
        e[kRForearm] += glm::vec3(-0.4f, 0.0f, 0.0f) * air;
        e[kHead] += glm::vec3(0.1f * rise, 0.0f, 0.0f) * air;
    }
    // Gliding: arms spread like wings, legs trailing; the body's pitch is the placement's.
    if (glide > 0.0f) {
        const float flutter = 0.06f * std::sin(m_time * 9.0f);
        e[kLArm] += glm::vec3(0.35f, 0.0f, 1.35f + flutter) * glide;
        e[kRArm] += glm::vec3(0.35f, 0.0f, -1.35f - flutter) * glide;
        e[kLForearm] += glm::vec3(-0.1f, 0.0f, 0.15f) * glide;
        e[kRForearm] += glm::vec3(-0.1f, 0.0f, -0.15f) * glide;
        e[kLUpLeg] += glm::vec3(0.18f, 0.0f, 0.04f) * glide;
        e[kRUpLeg] += glm::vec3(0.25f, 0.0f, -0.04f) * glide;
        e[kLLeg] += glm::vec3(0.25f + 0.1f * std::sin(m_time * 3.0f), 0.0f, 0.0f) * glide;
        e[kRLeg] += glm::vec3(0.35f + 0.1f * std::sin(m_time * 3.0f + 1.0f), 0.0f, 0.0f) * glide;
        e[kHead] += glm::vec3(-0.85f * m_pitch, 0.0f, 0.0f) * glide;
        e[kChest] += glm::vec3(-0.15f, 0.0f, 0.0f) * glide;
    }
    // A flap: the arms sweep down from above the head.
    if (in.flap > 0.0f && in.flap < 1.0f) {
        const float t = in.flap;
        const float up = (1.0f - smooth01(std::min(t * 1.6f, 1.0f)));
        const float w = std::sin(kPi * t);
        e[kLArm] += glm::vec3(0.0f, 0.0f, (2.0f * up - 0.4f) * w);
        e[kRArm] += glm::vec3(0.0f, 0.0f, -(2.0f * up - 0.4f) * w);
        e[kLUpLeg] += glm::vec3(0.4f * w, 0.0f, 0.0f);
        e[kRUpLeg] += glm::vec3(0.2f * w, 0.0f, 0.0f);
        e[kLLeg] += glm::vec3(0.6f * w, 0.0f, 0.0f);
        e[kRLeg] += glm::vec3(0.5f * w, 0.0f, 0.0f);
    }
    // Sitting with folded legs (a dormant spirit; the ending).
    if (in.sit > 0.0f) {
        const float w = in.sit;
        hips.y += -0.33f * w;
        e[kHips] += glm::vec3(-0.1f, 0.0f, 0.0f) * w;
        e[kLUpLeg] += glm::vec3(-1.45f, 0.35f, 0.35f) * w;
        e[kRUpLeg] += glm::vec3(-1.45f, -0.35f, -0.35f) * w;
        e[kLLeg] += glm::vec3(2.2f, 0.0f, 0.0f) * w;
        e[kRLeg] += glm::vec3(2.2f, 0.0f, 0.0f) * w;
        e[kLArm] += glm::vec3(-0.55f, 0.0f, 0.12f) * w;
        e[kRArm] += glm::vec3(-0.55f, 0.0f, -0.12f) * w;
        e[kLForearm] += glm::vec3(-0.5f, 0.0f, -0.3f) * w;
        e[kRForearm] += glm::vec3(-0.5f, 0.0f, 0.3f) * w;
        e[kHead] += glm::vec3(0.25f + 0.02f * breath, 0.0f, 0.0f) * w;
    }
    // Calling: arms raised, face up.
    if (in.pray > 0.0f) {
        const float w = in.pray;
        e[kLArm] += glm::vec3(-1.9f, 0.0f, 0.55f) * w;
        e[kRArm] += glm::vec3(-1.9f, 0.0f, -0.55f) * w;
        e[kLForearm] += glm::vec3(-0.3f, 0.0f, 0.0f) * w;
        e[kRForearm] += glm::vec3(-0.3f, 0.0f, 0.0f) * w;
        e[kHead] += glm::vec3(-0.35f, 0.0f, 0.0f) * w;
        e[kChest] += glm::vec3(-0.1f, 0.0f, 0.0f) * w;
    }

    std::array<glm::quat, kBodyJoints> local;
    for (int j = 0; j < kBodyJoints; ++j) {
        const glm::vec3 a = e[std::size_t(j)];
        local[std::size_t(j)] = glm::angleAxis(a.z, glm::vec3(0, 0, 1)) * glm::angleAxis(a.y, glm::vec3(0, 1, 0)) *
                                glm::angleAxis(a.x, glm::vec3(1, 0, 0));
    }

    // Feet on the ground: the hips go down to the lower foot's ground, each leg reaches its own (the engine's
    // two-bone IK, knees bending forward).
    forward(local, hips);
    const float ikWeight = (1.0f - m_air) * (1.0f - in.sit) * (1.0f - glide);
    std::array<float, 2> delta{0.0f, 0.0f};
    if (ikWeight > 0.01f && ground) {
        for (int side = 0; side < 2; ++side) {
            const int foot = side == 0 ? kLFoot : kRFoot;
            const glm::vec3 w = glm::vec3(m_world * m_model[std::size_t(foot)][3]);
            const std::optional<float> g = ground(glm::vec3(w.x, m_feet.y + 0.45f * scale, w.z));
            const float d = g ? (*g - m_feet.y) / scale : 0.0f;
            delta[std::size_t(side)] = std::clamp(d, -0.28f, 0.28f) * ikWeight;
        }
    }
    m_hipsDrop = approach(m_hipsDrop, std::min({delta[0], delta[1], 0.0f}), 0.06f, dt);
    hips.y += m_hipsDrop;
    forward(local, hips);
    if (ikWeight > 0.01f) {
        for (int side = 0; side < 2; ++side) {
            const int up = side == 0 ? kLUpLeg : kRUpLeg;
            const float lift = delta[std::size_t(side)] - m_hipsDrop;
            if (std::abs(lift) < 1e-4f) continue;
            quench::animation::TwoBoneIk ik;
            ik.target = glm::vec3(m_model[std::size_t(up + 2)][3]) + glm::vec3(0.0f, lift, 0.0f);
            ik.pole = glm::vec3(0.0f, 0.0f, 1.0f);
            ik.midAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            ik.weight = ikWeight;
            const quench::animation::TwoBoneResult r = quench::animation::solveTwoBone(
                m_model[std::size_t(up)], m_model[std::size_t(up + 1)], m_model[std::size_t(up + 2)], ik);
            local[std::size_t(up)] = local[std::size_t(up)] * r.start;
            local[std::size_t(up + 1)] = local[std::size_t(up + 1)] * r.mid;
        }
        forward(local, hips);
    }
    // The head turns toward what the figure looks at (the engine's aim IK), eased.
    {
        glm::quat want(1.0f, 0.0f, 0.0f, 0.0f);
        if (in.lookAt) {
            const glm::vec3 target = glm::vec3(glm::inverse(m_world) * glm::vec4(*in.lookAt, 1.0f));
            quench::animation::AimIk aim;
            aim.target = target;
            aim.forward = glm::vec3(0.0f, 0.0f, 1.0f);
            aim.up = glm::vec3(0.0f, 1.0f, 0.0f);
            aim.offset = glm::vec3(0.0f, 0.1f, 0.08f);
            aim.pole = glm::vec3(0.0f, 1.0f, 0.0f);
            aim.weight = 0.7f;
            const glm::vec3 eyes = glm::vec3(m_model[kHead] * glm::vec4(aim.offset, 1.0f));
            const glm::vec3 to = target - eyes;
            const float l = glm::length(to);
            // Only what is in front: the head does not turn around.
            if (l > 0.3f && to.z / l > -0.2f) want = quench::animation::solveAim(m_model[kHead], aim).correction;
        }
        m_head = glm::slerp(m_head, want, 1.0f - std::exp(-dt / 0.18f));
        local[kHead] = local[kHead] * m_head;
        forward(local, hips);
    }

    // The cape follows the shoulders.
    if (!m_clothValid || glm::distance(m_feet, m_lastFeet) > 3.0f) resetCape();
    simulateCape(dt, in.wind, std::max(glide, in.flap > 0.0f && in.flap < 1.0f ? std::sin(kPi * in.flap) : 0.0f));
    m_lastFeet = m_feet;

    const glm::mat4 toModel = glm::inverse(m_world);
    for (int j = 0; j < kBodyJoints; ++j)
        m_palette[std::size_t(j)] = m_model[std::size_t(j)] * glm::translate(glm::mat4(1.0f), -kRest[std::size_t(j)]);
    for (int r = 0; r < kCapeRows; ++r)
        for (int c = 0; c < kCapeCols; ++c) {
            auto at = [&](int rr, int cc) {
                return glm::vec3(toModel * glm::vec4(m_cloth[std::size_t(rr * kCapeCols + cc)], 1.0f));
            };
            auto rest = [&](int rr, int cc) { return m_clothRest[std::size_t(rr * kCapeCols + cc)]; };
            const int r0 = std::max(r - 1, 0), r1 = std::min(r + 1, kCapeRows - 1);
            const int c0 = std::max(c - 1, 0), c1 = std::min(c + 1, kCapeCols - 1);
            auto basis = [](glm::vec3 down, glm::vec3 across) {
                const glm::vec3 y = glm::normalize(-down);
                const glm::vec3 z = glm::normalize(glm::cross(across, y));
                const glm::vec3 x = glm::cross(y, z);
                return glm::mat3(x, y, z);
            };
            const glm::mat3 now = basis(at(r1, c) - at(r0, c), at(r, c1) - at(r, c0));
            const glm::mat3 bind = basis(rest(r1, c) - rest(r0, c), rest(r, c1) - rest(r, c0));
            glm::mat4 m(now * glm::transpose(bind));
            m[3] = glm::vec4(at(r, c), 1.0f);
            const int j = capeJoint(r, c);
            m_model[std::size_t(j)] = m;
            m_palette[std::size_t(j)] = m * glm::translate(glm::mat4(1.0f), -m_bind[std::size_t(j)]);
        }
}

void Character::simulateCape(float dt, glm::vec3 wind, float wings) {
    const float scale = m_look.scale;
    dt = std::min(dt, 1.0f / 30.0f);
    // The collar follows the chest: the top row is pinned to its bind place in the chest's frame.
    const glm::mat4 chest = m_world * m_model[kChest] * glm::translate(glm::mat4(1.0f), -kRest[kChest]);
    const glm::vec3 collarBefore = m_cloth[std::size_t(kCapeCols / 2)];
    for (int c = 0; c < kCapeCols; ++c) {
        m_cloth[std::size_t(c)] = glm::vec3(chest * glm::vec4(m_clothRest[std::size_t(c)], 1.0f));
        m_clothPrev[std::size_t(c)] = m_cloth[std::size_t(c)];
    }
    // The body the cloth falls against: spheres at the back, the hips and the thighs.
    struct Sphere {
        glm::vec3 c;
        float r;
    };
    const auto world = [&](Joint j, glm::vec3 offset) {
        return glm::vec3(m_world * m_model[std::size_t(j)] * glm::vec4(offset, 1.0f));
    };
    const std::array<Sphere, 6> body = {{
        {world(kChest, {0.0f, -0.02f, 0.0f}), 0.125f * scale},
        {world(kSpine, {0.0f, -0.04f, 0.0f}), 0.12f * scale},
        {world(kHips, {0.0f, -0.08f, 0.0f}), 0.13f * scale},
        {world(kLUpLeg, {0.0f, -0.12f, 0.0f}), 0.07f * scale},
        {world(kRUpLeg, {0.0f, -0.12f, 0.0f}), 0.07f * scale},
        {world(kHead, {0.0f, 0.1f, -0.01f}), 0.15f * scale},
    }};
    const glm::vec3 gravity(0.0f, -9.81f, 0.0f);
    // Swinging against the body dies out quickly (the cloth's own stiffness and the air), the same at any frame rate.
    const glm::vec3 collarVelocity = glm::length(m_cloth[std::size_t(kCapeCols / 2)] - collarBefore) < 1.0f
                                         ? (m_cloth[std::size_t(kCapeCols / 2)] - collarBefore) / std::max(dt, 1e-4f)
                                         : glm::vec3(0.0f);
    const float settle = std::exp(-2.5f * dt);
    for (int i = kCapeCols; i < kCapeCols * kCapeRows; ++i) {
        glm::vec3& p = m_cloth[std::size_t(i)];
        glm::vec3& q = m_clothPrev[std::size_t(i)];
        const glm::vec3 velocity = collarVelocity + ((p - q) / std::max(dt, 1e-4f) - collarVelocity) * settle;
        // Drag toward the air's motion: the cape streams behind a running or gliding figure and flutters in the wind.
        const glm::vec3 drag = (wind - velocity) * (1.4f + 1.2f * wings);
        const glm::vec3 next = p + velocity * dt + (gravity + drag) * (dt * dt);
        q = p;
        p = next;
    }
    const float vertical = m_rowLength * scale;
    for (int iteration = 0; iteration < 8; ++iteration) {
        for (int r = 1; r < kCapeRows; ++r)
            for (int c = 0; c < kCapeCols; ++c) {
                const int i = r * kCapeCols + c;
                const int up = i - kCapeCols;
                glm::vec3& a = m_cloth[std::size_t(up)];
                glm::vec3& b = m_cloth[std::size_t(i)];
                const glm::vec3 d = b - a;
                const float l = glm::length(d);
                if (l < 1e-6f) continue;
                const glm::vec3 corr = d * ((l - vertical) / l);
                if (r == 1) {
                    b -= corr;
                } else {
                    a += corr * 0.5f;
                    b -= corr * 0.5f;
                }
            }
        for (int r = 1; r < kCapeRows; ++r) {
            // Spread as wings, the gathered cape opens wide between the arms.
            const float spread = 1.0f + wings * (r == 1 ? 0.3f : r == 2 ? 0.6f : 0.8f);
            const float width = m_rowWidth[std::size_t(r)] * scale * spread;
            for (int c = 0; c + 1 < kCapeCols; ++c) {
                glm::vec3& a = m_cloth[std::size_t(r * kCapeCols + c)];
                glm::vec3& b = m_cloth[std::size_t(r * kCapeCols + c + 1)];
                const glm::vec3 d = b - a;
                const float l = glm::length(d);
                if (l < 1e-6f) continue;
                // Cloth resists stretching more than it resists folding (less so spread as wings).
                const float target = l < width ? glm::mix(l, width, 0.3f + 0.6f * wings) : width;
                const glm::vec3 corr = d * ((l - target) / l) * 0.5f;
                a += corr;
                b -= corr;
            }
        }
        for (int i = kCapeCols; i < kCapeCols * kCapeRows; ++i)
            for (std::size_t b = 0; b < body.size(); ++b) {
                // The hood only holds off the cape's top; tipped back in a glide it would lift all of it.
                if (b + 1 == body.size() && i >= 2 * kCapeCols) break;
                const Sphere& s = body[b];
                glm::vec3& p = m_cloth[std::size_t(i)];
                const glm::vec3 d = p - s.c;
                const float l = glm::length(d);
                const float r = s.r + 0.012f * scale;
                if (l < r && l > 1e-6f) p = s.c + d * (r / l);
            }
    }
}

} // namespace cloudsong
