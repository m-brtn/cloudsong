#pragma once

// The player's body: a capsule moved by Jolt's virtual character over the isles and the ruins. Walking and running
// follow the stick relative to the camera; a jump; in the air, holding jump glides (a slow fall, faster forward,
// steered by the stick), pressing it again flaps upward with one mark of the cape's energy. Updrafts lift a glider.

#include "Util.h"

#include <quench/physics/PhysicsWorld.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace cloudsong {

struct PlayerInput {
    glm::vec2 move{0.0f}; // stick: x right, y forward, length 0..1
    bool walk = false;    // slower
    bool jumpPressed = false;
    bool jumpHeld = false;
    float cameraYaw = 0.0f; // the camera's heading (radians; 0 looks along +z)
    float lift = 0.0f;      // an updraft's pull (m/s upward it carries a glider to)
    glm::vec2 pull{0.0f};   // and its pull toward its middle (m/s² in x and z)
};

class Player {
public:
    void create(quench::PhysicsWorld& physics, glm::vec3 position, float yaw);
    void destroy();
    void step(float dt, const PlayerInput& input, quench::PhysicsWorld& physics);
    void teleport(glm::vec3 position, float yaw);

    glm::vec3 position() const { return m_position; } // the feet (world)
    glm::vec3 velocity() const { return m_velocity; }
    float yaw() const { return m_yaw; }
    bool grounded() const { return m_grounded; }
    float glide() const { return m_glide; } // 0..1
    float flap() const { return m_flapTime < kFlapDuration ? m_flapTime / kFlapDuration : 0.0f; }
    bool flapped() const { return m_flappedThisStep; }
    bool jumped() const { return m_jumpedThisStep; }
    bool landed() const { return m_landedThisStep; }
    float airTime() const { return m_airTime; }

    float energy = 2.0f; // marks of the cape lit (fractional while recharging)
    int maxEnergy = 2;

    static constexpr float kFlapDuration = 0.5f;

private:
    JPH::Ref<JPH::CharacterVirtual> m_character;
    glm::vec3 m_position{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_yaw = 0.0f;
    bool m_grounded = true;
    float m_glide = 0.0f;
    float m_airTime = 0.0f;
    float m_flapTime = 10.0f;
    float m_coyote = 0.0f;
    bool m_flappedThisStep = false, m_jumpedThisStep = false, m_landedThisStep = false;
};

} // namespace cloudsong
