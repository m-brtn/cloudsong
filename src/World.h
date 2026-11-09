#pragma once

// The region: floating isles of grass above a sea of clouds, their meadows (grass and flowers that sway in the
// weather's wind), stone ruins, a pond, a cave with light falling in, small isles to glide between and the temple on
// the high isle. Seeded terrain and authored placements make the same world each run; imported sanctuary, willow
// and harp meshes complement the procedural geometry. Rendering and collision share the placed geometry.

#include "Util.h"

#include <quench/navigation/NavMesh.h>
#include <quench/physics/PhysicsWorld.h>
#include <quench/render/Scene.h>
#include <quench/render/SceneAssets.h>

#include <optional>
#include <vector>

namespace cloudsong {

// The isles float this high above y = 0, where the clouds' bases are (the engine's weather puts clouds above y = 0).
inline constexpr float kAltitude = 2200.0f;

struct Bump {
    glm::vec2 at;
    float radius;
    float height;        // negative: a hollow
    float flat = 0.0f;   // 0..1: share of the radius that is level at the top (a plateau, a pond's floor)
    float jagged = 0.0f; // 0..: how much the outline wanders from a circle
    glm::vec2 stretch{1.0f};
    float yaw = 0.0f;
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
    // Ground levelled down to a height (x, z the centre, z its radius, w the height), sloping back over 2 m.
    std::vector<glm::vec4> levels = {};
};

struct Isle {
    IsleDesc desc;
    std::optional<float> heightAt(glm::vec2 xz) const; // local height of the ground (m), none outside
    float outline(float angle) const;                  // radius of the outline at an angle about the centre
    float slope(glm::vec2 xz) const;                   // steepness of the ground (rise over run), 0 outside
    glm::vec3 grassColor(glm::vec2 xz) const;          // linear
};

// Places where the game puts things (local coordinates; world = local + (0, kAltitude, 0)).
struct Landmarks {
    glm::vec3 start; // the player's first place
    float startYaw = 0.0f;
    std::vector<glm::vec3> chimes;   // optional exploration collectibles; three awaken the wind song
    std::vector<glm::vec3> lights;   // the drifting lights to collect
    std::vector<glm::vec3> candles;  // the candles (the flame's place)
    std::vector<glm::vec3> lanterns; // stone lanterns that always burn (the flame's place)
    std::vector<glm::vec3> spirits;  // where the dormant spirits sit
    std::vector<float> spiritYaw;
    glm::vec3 updraft; // the wind that lifts to the temple once the candles burn
    float updraftRadius = 3.0f;
    float updraftTop = 60.0f; // local height the updraft lifts to
    glm::vec3 altar;          // the temple's centre (the ending)
    glm::vec3 beamBase;
    glm::vec3 caveCentre;
    float caveRadius = 11.0f;
    std::vector<glm::vec3> caveShafts; // where the light falls into the cave (the floor)
    std::vector<glm::vec3> caveHoles;  // where it comes in through the roof
    glm::vec3 caveEntrance;            // just inside the cave's mouth (the floor)
    float caveHeight = 7.5f;
    glm::vec3 pond;
    float pondRadius = 6.0f;
    glm::vec3 cottage;
    glm::vec3 meadowGlade;
    glm::vec3 musicCourt, musician; // level courtyard floor, and the resident's feet
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
    Result<void> loadArchitecture(quench::render::SceneAssets& assets, quench::PhysicsWorld& physics);
    // Grass and flower patches near the camera in full, far ones thinned.
    void updateDetail(quench::render::Scene& scene, glm::vec3 camera);
    // The call's ring of light over the flowers: its centre (world) and the seconds since the call (large: none).
    void setCallRing(quench::render::Scene& scene, glm::vec3 centre, float age);
    void setChime(quench::render::Scene& scene, std::size_t index, float age, float time);
    glm::vec3 shipPosition() const { return m_shipPosition; }
    void setCandleLit(quench::render::Scene& scene, int candle, bool lit);

    const std::vector<Isle>& isles() const { return m_isles; }
    const Landmarks& landmarks() const { return m_landmarks; }
    // The local ground height below a local point (the isles' terrain, not the ruins).
    std::optional<float> terrainHeight(glm::vec2 xz) const;
    Ground groundAt(glm::vec3 local) const;
    float pathMaxGrade() const { return m_pathMaxGrade; }
    float pathMaxCrossSlope() const { return m_pathMaxCrossSlope; }
    // What the spirits walk on (local coordinates): the static geometry near the way, collected by build().
    quench::navigation::Geometry takeNavGeometry() { return std::move(m_nav); }

private:
    struct Patch {
        glm::vec3 centre; // world
        quench::render::InstanceId near, far;
        int detail = 2; // 2 near, 1 far, 0 hidden
    };

    struct Path {
        std::vector<glm::vec2> points;
        std::vector<float> heights; // graded centreline, shared by ground and bridge approaches
        float width;
        glm::vec2 low{1e9f}, high{-1e9f};
    };
    struct Bridge {
        glm::vec2 a, b;
        float width, ya, yb;
    };
    std::vector<Path> m_paths;
    std::vector<glm::vec2> m_meadowSteps;
    std::vector<Bridge> m_bridges;
    float m_pathMaxGrade = 0, m_pathMaxCrossSlope = 0;
    std::vector<glm::vec4> m_foundations; // centre x/z, level radius, height
    float foundationHeight(glm::vec2 p, float natural) const;
    void layoutPaths();
    struct GardenObject {
        glm::vec2 at;
        float height, yaw;
        bool tree;
        float floor = 0;
    };
    std::vector<GardenObject> m_garden;
    void layoutGarden();
    void layoutChimes();
    struct CacheCover {
        glm::vec2 at;
        float yaw;
        int kind;
    };
    std::vector<CacheCover> m_cacheCovers;
    glm::vec3 m_shipPosition{0};
    void buildRegion(quench::render::Scene& scene, quench::PhysicsWorld& physics);
    bool onPath(glm::vec2 p) const;
    float pathCoverage(glm::vec2 p) const;
    float gradedHeight(glm::vec2 p, float natural) const;
    float terrainSlope(glm::vec2 p) const;
    void buildIsle(quench::render::Scene& scene, quench::PhysicsWorld& physics, const Isle& isle);
    // Grass over an isle's rim and roots and vines below it (`rimY`: the rim's height round the outline).
    void buildHanging(quench::render::Scene& scene, const Isle& isle, const std::vector<float>& rimY);
    void buildFoliage(quench::render::Scene& scene, const Isle& isle, float density);
    void buildRuins(quench::render::Scene& scene, quench::PhysicsWorld& physics);
    float caveCut(glm::vec3 local) const;
    void buildCottage(quench::render::Scene& scene, quench::PhysicsWorld& physics);
    void buildCave(quench::render::Scene& scene, quench::PhysicsWorld& physics);
    void addLights(quench::render::Scene& scene);
    // How much of the open sky's light reaches the ground here (1 in the open, low inside the cave).
    float shade(glm::vec2 xz) const;
    void addStatic(quench::PhysicsWorld& physics, const MeshData& mesh);
    void addBox(quench::PhysicsWorld& physics, glm::vec3 centre, glm::vec3 half, float yaw);
    bool excluded(glm::vec2 xz) const;
    void addNavTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c);

    std::vector<Isle> m_isles;
    Landmarks m_landmarks;
    std::vector<Patch> m_patches;
    std::vector<std::pair<glm::vec2, float>> m_clear; // circles without grass (ruins, the pond)
    std::vector<std::pair<glm::vec2, float>> m_stone; // circles of stone floor (footsteps)
    quench::render::SurfaceId m_worldSurface, m_foliageSurface;
    quench::render::MaterialId m_terrain, m_rock, m_stoneMaterial, m_grass, m_water, m_flame, m_flameOff,
        m_caveMaterial, m_glowCap;
    std::vector<quench::render::InstanceId> m_flames;
    std::vector<std::array<quench::render::InstanceId, 2>> m_chimes;
    quench::render::MaterialDesc m_grassDesc;
    float m_detailDistance = 34.0f;
    quench::navigation::Geometry m_nav;
};

} // namespace cloudsong
