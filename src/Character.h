#pragma once

// A small hooded, caped figure: its skinned mesh is made in code (bind pose, model space, facing +z, y up, the feet at
// the origin), its pose comes from procedural gaits (idle, walk, run, jump, glide, flap), the engine's two-bone IK
// puts its feet on the ground and its aim IK turns its head toward what it looks at, and its cape is a cloth of
// verlet particles that drive the cape's joints. The player and the spirits are figures with other looks.

#include "Util.h"

#include <quench/render/Scene.h>

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cloudsong {

struct CharacterLook {
    glm::vec3 cape = srgb(226, 96, 70); // linear
    glm::vec3 capeInner = srgb(150, 50, 44);
    glm::vec3 hood = srgb(200, 76, 62);
    glm::vec3 body = srgb(236, 228, 212); // the tunic and the sleeves
    glm::vec3 legs = srgb(64, 56, 70);
    glm::vec3 face = srgb(252, 249, 244); // a pale mask
    glm::vec3 boots = srgb(92, 62, 48);
    float scale = 1.0f;      // 1: a child about 1.2 m tall with the hood
    float capeLength = 1.0f; // relative
    bool longHood = true;    // the hood's tip falls back
};

enum Joint : int {
    kRoot,
    kHips,
    kSpine,
    kChest,
    kNeck,
    kHead,
    kLUpLeg,
    kLLeg,
    kLFoot,
    kRUpLeg,
    kRLeg,
    kRFoot,
    kLArm,
    kLForearm,
    kLHand,
    kRArm,
    kRForearm,
    kRHand,
    kBodyJoints
};

inline constexpr int kCapeCols = 5;
inline constexpr int kCapeRows = 5;
inline constexpr int kJointCount = kBodyJoints + kCapeCols * kCapeRows;

struct CharacterMesh {
    MeshData mesh;
    std::vector<quench::render::SkinVertex> skin;
};

// What drives a pose this frame.
struct PoseInput {
    float speed = 0.0f; // horizontal speed (m/s)
    bool grounded = true;
    float verticalSpeed = 0.0f;
    float glide = 0.0f;              // 0..1: the glide pose
    float flap = 0.0f;               // 0..1: time into a flap's stroke (0 or 1: none)
    float sit = 0.0f;                // 0..1: sitting (a dormant spirit, the ending)
    float pray = 0.0f;               // 0..1: arms raised (calling)
    std::optional<glm::vec3> lookAt; // world point
    glm::vec3 wind{0.0f};            // the air's velocity at the figure (m/s)
};

// The ground below a world point: its height (m), or nothing when there is none within reach.
using GroundQuery = std::function<std::optional<float>(glm::vec3 from)>;

class Character {
public:
    static CharacterMesh buildMesh(const CharacterLook& look);

    explicit Character(const CharacterLook& look = {});

    // The figure's place: its feet (world), its heading (radians about +y, 0 = facing +z) and how far it leans
    // forward (radians, about its middle: the glide).
    void setPlacement(glm::vec3 feet, float yaw, float pitch = 0.0f);
    // Puts the cape at rest around the figure (a respawn, the first frame).
    void resetCape();
    // Advances the gait and the cape by dt and computes the joints' matrices.
    void update(float dt, const PoseInput& input, const GroundQuery& ground);

    glm::mat4 transform() const { return m_world; }
    std::span<const glm::mat4> palette() const { return m_palette; }
    glm::vec3 jointWorld(Joint joint) const;
    float scale() const { return m_look.scale; }
    // The middle of the collar and of the hem (world).
    glm::vec3 capePoint(int row) const { return m_cloth[std::size_t(row * kCapeCols + kCapeCols / 2)]; }
    // A foot came down in the last update (footsteps).
    bool stepped() const { return m_stepped; }

private:
    void forward(const std::array<glm::quat, kBodyJoints>& local, glm::vec3 hipsOffset);
    void simulateCape(float dt, glm::vec3 wind, float wings);

    CharacterLook m_look;
    std::array<glm::vec3, kJointCount> m_bind{}; // bind pose joint positions (model space)
    std::array<int, kBodyJoints> m_parent{};
    std::array<glm::mat4, kJointCount> m_model{}; // posed joints (model space)
    std::vector<glm::mat4> m_palette;
    glm::mat4 m_world{1.0f};
    glm::vec3 m_feet{0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_phase = 0.0f;
    float m_time = 0.0f;
    float m_air = 0.0f;   // 0..1, eased
    float m_speed = 0.0f; // eased
    float m_hipsDrop = 0.0f;
    glm::quat m_head{1.0f, 0.0f, 0.0f, 0.0f};

    // The cape's cloth (world space).
    std::array<glm::vec3, kCapeCols * kCapeRows> m_cloth{};
    std::array<glm::vec3, kCapeCols * kCapeRows> m_clothPrev{};
    std::array<glm::vec3, kCapeCols * kCapeRows> m_clothRest{}; // bind pose (model space)
    float m_rowLength = 0.1f;
    std::array<float, kCapeRows> m_rowWidth{};
    bool m_clothValid = false;
    int m_halfStep = 0;
    bool m_stepped = false;
    glm::vec3 m_lastFeet{0.0f};
};

} // namespace cloudsong
