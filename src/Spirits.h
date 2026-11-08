#pragma once

// The spirits: three small caped figures asleep in grey beside their candles. A lit candle wakes its spirit: its
// colours come back, it stands, and it follows the player. On the ground it walks the isles on a navmesh built from
// the world's static geometry (the engine's navigation: a crowd with avoidance); when the player leaves the ground
// for long or goes where the mesh does not lead, it glides through the air after the player and lands near it again.
// At the end they gather round the altar and sit.

#include "Character.h"
#include "World.h"

#include <quench/core/Jobs.h>
#include <quench/navigation/Crowd.h>
#include <quench/navigation/NavMesh.h>
#include <quench/navigation/NavQuery.h>
#include <quench/physics/PhysicsWorld.h>
#include <quench/render/Scene.h>

#include <optional>
#include <vector>

namespace cloudsong {

// What the spirits follow this frame (world coordinates).
struct SpiritTarget {
    glm::vec3 feet{0.0f};
    float yaw = 0.0f;
    bool grounded = true;
    float airTime = 0.0f;
};

class Spirits {
public:
    enum class State { Dormant, Waking, Walking, Flying, Gathered };

    Result<void> create(quench::render::Scene& scene, quench::render::SurfaceId surface, const Landmarks& landmarks,
                        quench::navigation::Geometry geometry, quench::JobSystem& jobs);
    void destroy();

    void wake(int spirit);
    // The ending: they come to the altar and sit round it.
    void gather();
    void update(float dt, const SpiritTarget& target, quench::render::Scene& scene,
                const quench::PhysicsWorld& physics);

    std::size_t count() const { return m_spirits.size(); }
    State state(int spirit) const { return m_spirits[std::size_t(spirit)].state; }
    int awake() const;
    // The feet (world) and the cape's colour (linear) of a spirit.
    glm::vec3 position(int spirit) const { return m_spirits[std::size_t(spirit)].feet; }
    glm::vec3 color(int spirit) const { return m_spirits[std::size_t(spirit)].look.cape; }
    float glow(int spirit) const; // 0 asleep .. 1 awake
    bool navReady() const { return m_mesh && m_mesh->ready(); }
    int walking() const;
    int seated() const; // gathered and sitting at the altar
    float distanceTo(int spirit, glm::vec3 point) const {
        return glm::distance(m_spirits[std::size_t(spirit)].feet, point);
    }

private:
    struct Spirit {
        CharacterLook look;
        Character figure;
        quench::render::InstanceId instance;
        State state = State::Dormant;
        glm::vec3 feet{0.0f}; // world
        glm::vec3 previous{0.0f};
        glm::vec3 velocity{0.0f};
        float yaw = 0.0f;
        float wake = 0.0f; // 0..1
        float sit = 1.0f;
        float glide = 0.0f;
        float retarget = 0.0f;
        float settled = 0.0f; // seconds the flight has been near its landing
        std::optional<quench::navigation::AgentId> agent;
        int slot = 0; // its place behind the player
    };

    glm::vec3 followPoint(const Spirit& s, const SpiritTarget& target) const;
    bool land(Spirit& s);
    void takeOff(Spirit& s);

    std::vector<Spirit> m_spirits;
    const Landmarks* m_landmarks = nullptr;
    std::optional<quench::navigation::NavMesh> m_mesh;
    std::optional<quench::navigation::Crowd> m_crowd;
    std::optional<quench::navigation::NavQuery> m_query;
    bool m_gathering = false;
    float m_time = 0.0f;
};

} // namespace cloudsong
