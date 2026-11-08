#pragma once

// The game's particles and trails, on the engine's (render::Scene::addParticles, addTrail): a ribbon of light from
// each corner of the cape while the figure flies or glides; the call's guide, a few heads of light with soft ribbons
// that rise beside the figure, fly over the isles to the journey's next goal, linger there and fade; sparse
// twinkling points round the camera over the flower fields, round the lanterns, along the isles' rims and in the
// cave (denser as the sun goes down); a few motes rising in the temple's beam and the open updraft and drifting in
// the cave. Their counts follow the picture setting.

#include "Settings.h"

#include <quench/particles/Path.h>
#include <quench/render/Scene.h>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace cloudsong {

class World;

class Particles {
public:
    // What moves the effects this frame (world coordinates).
    struct Frame {
        double time = 0.0; // the game's clock (s)
        float dt = 0.0f;   // since the last frame (s)
        glm::vec3 eye{0.0f};
        glm::vec3 capeLeft{0.0f}, capeRight{0.0f}, capeCentre{0.0f}; // the cape's lower corners and its middle
        glm::vec3 velocity{0.0f};
        bool flying = false;  // in the air long enough to trail, or gliding
        bool flapped = false; // a flap this frame
        bool jump = false;    // the figure jumped to another place (a teleport, back from a fall)
        bool updraft = false; // the updraft is open
        float dusk = 0.0f;    // 0: high sun .. 1: the sun at the horizon
    };

    quench::Result<void> create(quench::render::Scene& scene, const World& world, Quality quality);
    // Another picture setting: the systems' sizes and the trails' counts (restarts them).
    void setQuality(quench::render::Scene& scene, Quality quality);
    // The call at `feet` (world): the guide rises there and flies to `goal` (world, where it lingers), into the cave
    // through its mouth; with no goal it circles the figure once. A call while it is out sends it on from where its
    // first head is.
    void call(const World& world, glm::vec3 feet, std::optional<glm::vec3> goal);
    void update(quench::render::Scene& scene, const World& world, const Frame& frame);

    bool wingTrail() const { return m_emitting; }
    bool guiding() const { return m_guideAge >= 0.0f; }
    // From the guide's first head to where it lingers (m); 0 with no guide out or while it circles the figure.
    float guideDistance() const;

private:
    // A narrow light core and faint surrounding glow, with sparse sparkles.
    struct Wing {
        std::vector<quench::render::TrailId> trails;
    };

    void buildGuide(quench::render::Scene& scene);
    void removeGuide(quench::render::Scene& scene);
    void buildWings(quench::render::Scene& scene);
    // Appends the way from route.back() to `to` through the open air: an arc `rise` m high in its middle, kept clear
    // of the ground and of the cave's roof away from its ends.
    void arcTo(std::vector<glm::vec3>& route, const World& world, glm::vec3 to, float rise) const;
    glm::vec3 guideLead(float age) const; // where the first head is
    void updateGuide(quench::render::Scene& scene, const Frame& frame);
    float fieldDensity(const World& world, glm::vec2 xz) const;
    void emitField(quench::render::Scene& scene, const World& world, const Frame& frame);

    Quality m_quality = Quality::High;
    glm::vec3 m_updraft{0.0f}, m_beamBase{0.0f}, m_cave{0.0f}, m_caveEntrance{0.0f};
    float m_updraftRadius = 3.0f, m_updraftTop = 60.0f, m_caveRadius = 11.0f, m_caveHeight = 7.5f;
    std::vector<glm::vec3> m_lanterns;
    glm::vec3 m_eye{0.0f};

    // The guide: its heads' ribbons (a core each and, on the finer settings, a soft band) and their sparkles; its
    // way, and where it lingers at the end (radius 0: the way circles the figure and ends there).
    std::vector<quench::render::TrailId> m_guideHeads, m_guideHalos;
    quench::render::ParticleSystemId m_guideSparkles;
    quench::particles::Path m_route;
    glm::vec3 m_lingerCentre{0.0f};
    float m_lingerRadius = 0.0f, m_lingerAngle = 0.0f;
    float m_travel = 0.0f;    // s along the way
    float m_guideAge = -1.0f; // s since the call; negative: no guide out
    bool m_guideEmitting = false;
    bool m_guideRestart = false;      // the next update starts new ribbons at the figure
    std::vector<glm::vec3> m_guideAt; // the heads' places before their weave

    std::vector<Wing> m_wings;
    quench::particles::TrailDesc m_wingDesc, m_wingCoreDesc;
    float m_wingDaylight = -1.0f;
    quench::render::ParticleSystemId m_wingSparkles;
    bool m_emitting = false;
    float m_sinceFlight = 0.0f; // s since the figure last flew
    bool m_started = false;

    quench::render::ParticleSystemId m_field, m_beam, m_rising, m_caveMotes;
    std::vector<glm::vec3> m_spawns; // this frame's points of the field
    double m_fieldDue = 0.0;         // the field's candidate points owed (fractional)
    double m_lanternDue = 0.0;       // the points owed to each lantern (fractional)
    std::uint32_t m_random = 0x51f0u;
};

} // namespace cloudsong
