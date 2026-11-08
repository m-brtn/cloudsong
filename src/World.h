#pragma once

// The region: floating isles of grass above a sea of clouds, their meadows (grass and flowers that sway in the
// weather's wind), stone ruins, a pond, a cave with light falling in, small isles to glide between and the temple on
// the high isle. Everything is made in code from a fixed seed, so every run builds the same world. The world's
// geometry goes into the renderer's scene and, where it can be stood on, into the physics world.

#include "Util.h"

#include <quench/physics/PhysicsWorld.h>
#include <quench/render/Scene.h>

#include <optional>
#include <vector>

namespace cloudsong {

// The isles float this high above y = 0, where the clouds' bases are (the engine's weather puts clouds above y = 0).
inline constexpr float kAltitude = 2200.0f;

struct Bump {
    glm::vec2 at;
    float radius;
    float height; // negative: a hollow
    float flat = 0.0f; // 0..1: share of the radius that is level at the top (a plateau, a pond's floor)
};

struct IsleDesc {
    glm::vec2 centre;
    float base;   // ground height at the rim (local, above kAltitude)
    float radius; // mean radius of the outline (m)
    float hills;  // rolling hills' height (m)
    float depth;  // how far the rock hangs below (m)
    std::uint32_t seed;
    std::vector<Bump> bumps;
    float flowers = 1.0f; // 0..: density of the flower fields
};

struct Isle {
    IsleDesc desc;
    std::optional<float> heightAt(glm::vec2 xz) const; // local height of the ground (m), none outside
    float outline(float angle) const;                 // radius of the outline at an angle about the centre
    glm::vec3 grassColor(glm::vec2 xz) const;         // linear
};

// Places where the game puts things (local coordinates; world = local + (0, kAltitude, 0)).
struct Landmarks {
    glm::vec3 start;            // the player's first place
    float startYaw = 0.0f;
    std::vector<glm::vec3> lights;   // the drifting lights to collect
    std::vector<glm::vec3> candles;  // the candles (the flame's place)
    std::vector<glm::vec3> spirits;  // where the dormant spirits sit
    std::vector<float> spiritYaw;
    glm::vec3 updraft;          // the wind that lifts to the temple once the candles burn
    float updraftRadius = 3.0f;
    float updraftTop = 60.0f;   // local height the updraft lifts to
    glm::vec3 altar;            // the temple's centre (the ending)
    glm::vec3 beamBase;
    glm::vec3 caveCentre;
    float caveRadius = 11.0f;
    std::vector<glm::vec3> caveShafts; // where the light falls into the cave (the floor)
    glm::vec3 pond;
    float pondRadius = 6.0f;
};

// Ground types for footsteps.
enum class Ground { None, Grass, Stone };

class World {
public:
    World();

    // Builds the meshes, materials and instances into the scene (grass at `grassDensity`, 0.25..1) and the static
    // bodies into the physics world.
    Result<void> build(quench::render::Scene& scene, quench::PhysicsWorld& physics, quench::render::SurfaceId world,
                       quench::render::SurfaceId foliage, float grassDensity);
    // Grass and flower patches near the camera in full, far ones thinned.
    void updateDetail(quench::render::Scene& scene, glm::vec3 camera);
    // The call's ring of light over the flowers: its centre (world) and the seconds since the call (large: none).
    void setCallRing(quench::render::Scene& scene, glm::vec3 centre, float age);
    void setCandleLit(quench::render::Scene& scene, int candle, bool lit);

    const std::vector<Isle>& isles() const { return m_isles; }
    const Landmarks& landmarks() const { return m_landmarks; }
    // The local ground height below a local point (the isles' terrain, not the ruins).
    std::optional<float> terrainHeight(glm::vec2 xz) const;
    Ground groundAt(glm::vec3 local) const;

private:
    struct Patch {
        glm::vec3 centre; // world
        quench::render::InstanceId near, far;
        bool nearShown = true;
    };

    void buildIsle(quench::render::Scene& scene, quench::PhysicsWorld& physics, const Isle& isle);
    void buildFoliage(quench::render::Scene& scene, const Isle& isle, float density);
    void buildRuins(quench::render::Scene& scene, quench::PhysicsWorld& physics);
    void addStatic(quench::PhysicsWorld& physics, const MeshData& mesh);
    void addBox(quench::PhysicsWorld& physics, glm::vec3 centre, glm::vec3 half, float yaw);
    bool excluded(glm::vec2 xz) const;

    std::vector<Isle> m_isles;
    Landmarks m_landmarks;
    std::vector<Patch> m_patches;
    std::vector<std::pair<glm::vec2, float>> m_clear; // circles without grass (ruins, the pond)
    std::vector<std::pair<glm::vec2, float>> m_stone; // circles of stone floor (footsteps)
    quench::render::SurfaceId m_worldSurface, m_foliageSurface;
    quench::render::MaterialId m_terrain, m_stoneMaterial, m_grass, m_water, m_flame, m_flameOff;
    std::vector<quench::render::InstanceId> m_flames;
    quench::render::MaterialDesc m_grassDesc;
    float m_detailDistance = 34.0f;
};

} // namespace cloudsong
