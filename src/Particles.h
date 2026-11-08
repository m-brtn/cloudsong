#pragma once

// The game's particles and trails, on the engine's (render::Scene::addParticles, addTrail): a ribbon of light from
// each corner of the cape while the figure flies or glides, streams of light that loop through the sky over the
// meadow, round the updraft's isle and round the temple, a field of twinkling points over the meadow round the camera
// (denser as the sun goes down), and motes rising in the temple's beam and the updraft and drifting in the cave.
// Their counts follow the picture setting.

#include "Settings.h"

#include <quench/particles/Path.h>
#include <quench/render/Scene.h>

#include <glm/glm.hpp>

#include <array>
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
    void update(quench::render::Scene& scene, const World& world, const Frame& frame);

    bool wingTrail() const { return m_emitting; }

private:
    struct StreamLook {
        glm::vec3 color; // the ribbons' linear HDR color
        float speed;     // m/s along the course
        float gap;       // m between two heads
        float braid;     // m: how far the heads weave round the course
        std::uint32_t seed;
    };
    struct Stream {
        StreamLook look;
        quench::particles::Path path;
        std::vector<quench::render::TrailId> heads;
        std::vector<quench::render::TrailId> halos; // a wide faint ribbon round each head's, or none
        quench::render::ParticleSystemId sparkles;
    };
    // A ribbon from the cape, trails that follow one point: a warm band, alpha-blended so that its colour holds
    // against a bright sky; the light along its middle with the glowing head at the cape (additive, it sheds the
    // sparkles); on the finer settings a wide faint glow round them.
    struct Wing {
        std::vector<quench::render::TrailId> trails;
    };

    void buildStreams(quench::render::Scene& scene);
    void removeStreams(quench::render::Scene& scene);
    void buildWings(quench::render::Scene& scene);
    glm::vec3 headAt(const Stream& stream, int k, double t) const;
    void emitField(quench::render::Scene& scene, const World& world, const Frame& frame);

    Quality m_quality = Quality::High;
    glm::vec3 m_altar{0.0f}, m_updraft{0.0f}, m_beamBase{0.0f}, m_cave{0.0f};
    float m_updraftRadius = 3.0f, m_updraftTop = 60.0f;
    std::array<std::vector<glm::vec3>, 3> m_courses; // the streams' courses (world), clear of the ground

    std::vector<Stream> m_streams;
    std::vector<Wing> m_wings;
    quench::render::ParticleSystemId m_wingSparkles;
    bool m_emitting = false;
    float m_sinceFlight = 0.0f; // s since the figure last flew
    bool m_started = false;

    quench::render::ParticleSystemId m_field, m_beam, m_rising, m_caveMotes;
    std::vector<glm::vec3> m_spawns; // this frame's points of the field
    double m_fieldDue = 0.0;         // the field's points owed (fractional)
    std::uint32_t m_random = 0x51f0u;
};

} // namespace cloudsong
