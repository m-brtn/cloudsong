#include "Particles.h"

#include "Util.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

namespace cloudsong {

namespace render = quench::render;
using quench::particles::Blend;
using quench::particles::ColorCurve;
using quench::particles::ParticleDesc;
using quench::particles::Shape;
using quench::particles::Sprite;
using quench::particles::TrailDesc;

namespace {

// What a picture setting allows.
struct Budget {
    int streams;                  // courses with a stream: the meadow's and the temple's first, then the updraft's
    int heads;                    // per stream
    float trailSeconds;           // how long a stream's ribbon lasts
    std::uint32_t subdivisions;   // strip vertices per recorded segment
    float streamSparkles;         // shed per metre by each head
    std::uint32_t streamSlots;    // per stream
    int wings;                    // 1: a ribbon from the cape's middle; 2: one from each lower corner
    float wingSparkles;           // shed per metre by each ribbon
    std::uint32_t wingSlots;
    std::uint32_t fieldSlots;
    float fieldRate;              // points per second at dusk
    float motes;                  // the beam's, the updraft's and the cave's rates and slots, times
    bool halos;                   // a wide faint ribbon round each stream's
};

Budget budgetOf(Quality q) {
    switch (q) {
    case Quality::High: return {3, 7, 2.6f, 4, 1.2f, 1024, 2, 5.0f, 384, 4096, 900.0f, 1.0f, true};
    case Quality::Medium: return {3, 5, 2.2f, 4, 0.8f, 512, 2, 3.0f, 256, 2048, 550.0f, 0.65f, true};
    case Quality::Low: return {2, 4, 1.8f, 3, 0.5f, 256, 1, 2.0f, 128, 1024, 300.0f, 0.4f, false};
    }
    return budgetOf(Quality::High);
}

std::uint32_t slots(float base, float scale) { return std::max(32u, std::uint32_t(base * scale)); }

// Fades in quickly, holds, fades out.
ColorCurve fadeInOut(float in, float out, float peak = 1.0f) {
    return {{0.0f, glm::vec4(peak, peak, peak, 0.0f)},
            {in, glm::vec4(peak, peak, peak, 1.0f)},
            {out, glm::vec4(1.0f)},
            {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}};
}

glm::mat4 at(glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); }

// A closed course round a centre (world): an ellipse of `radii` that rises and falls by `wave`, with `loops` turns
// of `loopRadius` m in the upright plane through the centre, lifted where it would pass closer than `clearance` m
// to the ground.
std::vector<glm::vec3> course(const World& world, glm::vec3 centre, glm::vec2 radii, float wave, int loops,
                              float loopRadius, float clearance) {
    constexpr int kPoints = 96;
    std::vector<glm::vec3> points;
    std::vector<float> floor;
    for (int i = 0; i < kPoints; ++i) {
        const float a = 2.0f * kPi * float(i) / float(kPoints);
        const glm::vec3 radial(std::cos(a), 0.0f, std::sin(a));
        const float l = float(loops) * a;
        const glm::vec3 p = centre + glm::vec3(radii.x * radial.x, wave * std::sin(2.0f * a), radii.y * radial.z) +
                            loopRadius * (radial * std::cos(l) + glm::vec3(0.0f, std::sin(l), 0.0f));
        const std::optional<float> ground = world.terrainHeight({p.x, p.z});
        floor.push_back(ground ? *ground + kAltitude + clearance : p.y);
        points.push_back(glm::vec3(p.x, std::max(p.y, floor.back()), p.z));
    }
    // Smooth the lifts so that the course climbs over a hill instead of stepping onto it.
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<glm::vec3> smooth = points;
        for (int i = 0; i < kPoints; ++i) {
            const float y = 0.25f * points[std::size_t((i + kPoints - 1) % kPoints)].y + 0.5f * points[std::size_t(i)].y +
                            0.25f * points[std::size_t((i + 1) % kPoints)].y;
            smooth[std::size_t(i)].y = std::max(y, floor[std::size_t(i)]);
        }
        points = std::move(smooth);
    }
    return points;
}

// A ribbon of light: a thin bright core, or the wide faint halo round it.
TrailDesc ribbon(const Budget& b, glm::vec3 color, bool halo) {
    return {.lifetime = b.trailSeconds,
            .spacing = 0.3f,
            .maxPoints = 128,
            .subdivisions = b.subdivisions,
            .width = halo ? 1.1f : 0.13f,
            .widthOverAge = {{0.0f, 0.7f}, {0.06f, 1.0f}, {0.5f, 0.65f}, {1.0f, 0.0f}},
            .color = color * (halo ? 0.1f : 1.0f),
            .colorOverAge = {{0.0f, glm::vec4(1.8f, 1.8f, 1.8f, 1.0f)},
                             {0.12f, glm::vec4(1.2f, 1.2f, 1.2f, 1.0f)},
                             {0.5f, glm::vec4(0.9f, 0.9f, 0.95f, 0.6f)},
                             {1.0f, glm::vec4(0.7f, 0.7f, 0.8f, 0.0f)}},
            .headSize = halo ? 0.0f : 0.4f,
            .headColor = color * 1.2f + glm::vec3(1.2f),
            .softness = halo ? 1.0f : 0.35f,
            .minPixels = halo ? 1.0f : 1.5f};
}

ParticleDesc streamSparkles(glm::vec3 color, std::uint32_t seed, std::uint32_t maxParticles) {
    return {.maxParticles = maxParticles,
            .seed = seed,
            .rate = 0.0f,
            .lifetime = {1.0f, 2.4f},
            .speed = {0.05f, 0.4f},
            .spread = kPi,
            .inherit = 0.03f,
            .size = {0.05f, 0.1f},
            .gravity = glm::vec3(0.0f, -0.2f, 0.0f),
            .drag = 1.2f,
            .noise = 0.2f,
            .noiseScale = 3.0f,
            .wind = 0.2f,
            .color = color * 1.2f + glm::vec3(0.6f),
            .colorOverLife = fadeInOut(0.05f, 0.5f, 1.5f),
            .brightnessVariation = 0.6f,
            .twinkle = 0.75f,
            .twinkleRate = 3.0f,
            .twinkleSharpness = 4.0f,
            .sprite = Sprite::Star,
            .softness = 0.2f,
            .nearFade = 0.5f,
            .minPixels = 1.5f};
}

} // namespace

quench::Result<void> Particles::create(render::Scene& scene, const World& world, Quality quality) {
    const Landmarks& l = world.landmarks();
    const glm::vec3 alt(0.0f, kAltitude, 0.0f);
    m_altar = l.altar + alt;
    m_beamBase = l.beamBase + alt;
    m_updraft = l.updraft + alt;
    m_updraftRadius = l.updraftRadius;
    m_updraftTop = l.updraftTop + kAltitude;
    m_cave = l.caveCentre + alt;
    m_quality = quality;
    const Budget b = budgetOf(quality);
    // Over the meadow past its arch (low, so that it crosses the view from the way in), round the temple's crown and
    // round the updraft's isle.
    m_courses = {course(world, glm::vec3(2.0f, kAltitude + 5.0f, -12.0f), {18.0f, 12.0f}, 2.0f, 3, 3.0f, 3.0f),
                 course(world, m_altar + glm::vec3(0.0f, 16.0f, 0.0f), {26.0f, 26.0f}, 4.0f, 5, 5.0f, 4.0f),
                 course(world, m_updraft + glm::vec3(0.0f, 24.0f, 0.0f), {30.0f, 22.0f}, 6.0f, 4, 6.0f, 4.0f)};

    // Twinkling points over the meadow round the camera (placed by emitField on the ground).
    quench::Result<render::ParticleSystemId> field = scene.addParticles({.maxParticles = b.fieldSlots,
                                                                         .seed = 7,
                                                                         .rate = 0.0f,
                                                                         .lifetime = {2.5f, 5.0f},
                                                                         .speed = {0.0f, 0.08f},
                                                                         .spread = kPi,
                                                                         .size = {0.05f, 0.11f},
                                                                         .noise = 0.08f,
                                                                         .noiseScale = 3.0f,
                                                                         .wind = 0.1f,
                                                                         .color = glm::vec3(2.8f, 2.4f, 1.6f),
                                                                         .colorOverLife = fadeInOut(0.15f, 0.8f),
                                                                         .brightnessVariation = 0.7f,
                                                                         .twinkle = 0.65f,
                                                                         .twinkleRate = 0.8f,
                                                                         .twinkleSharpness = 2.5f,
                                                                         .softness = 0.15f,
                                                                         .nearFade = 1.5f,
                                                                         .minPixels = 1.2f});
    if (!field) return quench::Error{field.error()};
    m_field = *field;

    // Motes rising slowly in the temple's beam, quickly in the open updraft, and drifting in the cave's light.
    quench::Result<render::ParticleSystemId> beam = scene.addParticles(
        {.maxParticles = slots(512.0f, b.motes),
         .seed = 13,
         .rate = 24.0f * b.motes,
         .shape = Shape::Disc,
         .extent = glm::vec3(1.8f),
         .lifetime = {14.0f, 22.0f},
         .speed = {1.2f, 2.6f},
         .spread = 0.15f,
         .size = {0.05f, 0.09f},
         .noise = 0.25f,
         .noiseScale = 6.0f,
         .noiseRate = 0.1f,
         .color = glm::vec3(5.0f, 4.3f, 3.0f) * 0.8f,
         .colorOverLife = fadeInOut(0.1f, 0.8f),
         .brightnessVariation = 0.5f,
         .twinkle = 0.45f,
         .twinkleRate = 0.4f,
         .twinkleSharpness = 1.0f,
         .softness = 0.2f,
         .minPixels = 1.2f},
        at(m_beamBase));
    if (!beam) return quench::Error{beam.error()};
    m_beam = *beam;
    const float rise = std::max(m_updraftTop - m_updraft.y, 10.0f);
    quench::Result<render::ParticleSystemId> rising = scene.addParticles(
        {.maxParticles = slots(512.0f, b.motes),
         .seed = 17,
         .rate = 30.0f * b.motes,
         .shape = Shape::Disc,
         .extent = glm::vec3(m_updraftRadius),
         .lifetime = {rise / 9.0f, rise / 7.0f},
         .speed = {6.0f, 10.0f},
         .spread = 0.08f,
         .size = {0.04f, 0.07f},
         .noise = 0.7f,
         .noiseScale = 3.0f,
         .noiseRate = 0.3f,
         .color = glm::vec3(2.4f, 2.4f, 2.0f),
         .colorOverLife = fadeInOut(0.08f, 0.75f),
         .brightnessVariation = 0.5f,
         .twinkle = 0.5f,
         .twinkleRate = 2.0f,
         .softness = 0.2f,
         .minPixels = 1.2f},
        at(m_updraft));
    if (!rising) return quench::Error{rising.error()};
    m_rising = *rising;
    scene.setParticlesEmitting(m_rising, false);
    quench::Result<render::ParticleSystemId> cave = scene.addParticles(
        {.maxParticles = slots(160.0f, b.motes),
         .seed = 19,
         .rate = 14.0f * b.motes,
         .shape = Shape::Box,
         .extent = glm::vec3(8.0f, 2.5f, 8.0f),
         .lifetime = {6.0f, 10.0f},
         .speed = {0.0f, 0.08f},
         .spread = kPi,
         .size = {0.025f, 0.045f},
         .noise = 0.12f,
         .noiseScale = 2.0f,
         .color = glm::vec3(4.0f, 3.6f, 1.6f) * 0.6f,
         .colorOverLife = fadeInOut(0.15f, 0.8f),
         .brightnessVariation = 0.5f,
         .twinkle = 0.6f,
         .twinkleRate = 1.2f,
         .softness = 0.1f,
         .minPixels = 1.2f},
        at(m_cave + glm::vec3(0.0f, 2.5f, 0.0f)));
    if (!cave) return quench::Error{cave.error()};
    m_caveMotes = *cave;

    buildStreams(scene);
    buildWings(scene);
    return {};
}

void Particles::setQuality(render::Scene& scene, Quality quality) {
    if (quality == m_quality) return;
    m_quality = quality;
    const Budget b = budgetOf(quality);
    removeStreams(scene);
    for (const render::TrailId wing : m_wings) scene.removeTrail(wing);
    m_wings.clear();
    scene.removeParticles(m_wingSparkles);
    buildStreams(scene);
    buildWings(scene);
    m_emitting = false;
    m_started = false;
    if (const ParticleDesc* d = scene.particleDesc(m_field)) {
        ParticleDesc field = *d;
        field.maxParticles = b.fieldSlots;
        (void)scene.setParticles(m_field, field);
    }
    for (const auto& [id, base, rate] : {std::tuple{m_beam, 512.0f, 24.0f}, std::tuple{m_rising, 512.0f, 30.0f},
                                         std::tuple{m_caveMotes, 160.0f, 14.0f}})
        if (const ParticleDesc* d = scene.particleDesc(id)) {
            ParticleDesc motes = *d;
            motes.maxParticles = slots(base, b.motes);
            motes.rate = rate * b.motes;
            (void)scene.setParticles(id, motes);
        }
}

void Particles::buildStreams(render::Scene& scene) {
    const Budget b = budgetOf(m_quality);
    // Gold over the meadow, blue round the temple, rose round the updraft's isle: saturated, so that the tone curve
    // keeps their hue against the bright sky.
    const std::array<StreamLook, 3> looks{{{glm::vec3(2.6f, 1.45f, 0.4f), 7.0f, 1.4f, 0.8f, 101},
                                           {glm::vec3(0.8f, 1.7f, 3.4f), 9.0f, 1.8f, 1.1f, 202},
                                           {glm::vec3(3.0f, 0.95f, 1.6f), 8.0f, 1.6f, 1.0f, 303}}};
    for (int i = 0; i < std::min(b.streams, int(looks.size())); ++i) {
        const StreamLook& look = looks[std::size_t(i)];
        quench::Result<render::ParticleSystemId> sparkles =
            scene.addParticles(streamSparkles(look.color, look.seed, b.streamSlots));
        if (!sparkles) continue;
        Stream stream{.look = look,
                      .path = quench::particles::Path(m_courses[std::size_t(i)], true),
                      .sparkles = *sparkles};
        for (int k = 0; k < b.heads; ++k) {
            const render::TrailId head = scene.addTrail(ribbon(b, look.color, false));
            scene.setTrailSparkles(head, stream.sparkles, b.streamSparkles);
            stream.heads.push_back(head);
            if (b.halos) stream.halos.push_back(scene.addTrail(ribbon(b, look.color, true)));
        }
        m_streams.push_back(std::move(stream));
    }
}

void Particles::removeStreams(render::Scene& scene) {
    for (const Stream& stream : m_streams) {
        for (const render::TrailId head : stream.heads) scene.removeTrail(head);
        for (const render::TrailId halo : stream.halos) scene.removeTrail(halo);
        scene.removeParticles(stream.sparkles);
    }
    m_streams.clear();
}

void Particles::buildWings(render::Scene& scene) {
    const Budget b = budgetOf(m_quality);
    quench::Result<render::ParticleSystemId> sparkles = scene.addParticles({.maxParticles = b.wingSlots,
                                                                            .seed = 23,
                                                                            .rate = 0.0f,
                                                                            .lifetime = {0.6f, 1.5f},
                                                                            .speed = {0.02f, 0.25f},
                                                                            .spread = kPi,
                                                                            .inherit = 0.15f,
                                                                            .size = {0.008f, 0.018f},
                                                                            .gravity = glm::vec3(0.0f, -0.5f, 0.0f),
                                                                            .drag = 1.6f,
                                                                            .noise = 0.1f,
                                                                            .noiseScale = 1.5f,
                                                                            .color = glm::vec3(3.0f, 2.2f, 1.1f),
                                                                            .colorOverLife = fadeInOut(0.05f, 0.5f),
                                                                            .brightnessVariation = 0.5f,
                                                                            .twinkle = 0.7f,
                                                                            .twinkleRate = 4.0f,
                                                                            .sprite = Sprite::Star,
                                                                            .softness = 0.05f,
                                                                            .nearFade = 0.2f,
                                                                            .minPixels = 1.2f});
    if (sparkles) m_wingSparkles = *sparkles;
    const TrailDesc wing{.lifetime = 1.3f,
                         .spacing = 0.04f,
                         .maxPoints = 192,
                         .subdivisions = b.subdivisions,
                         .width = 0.045f,
                         .widthOverAge = {{0.0f, 0.6f}, {0.08f, 1.0f}, {1.0f, 0.0f}},
                         .color = glm::vec3(2.4f, 1.25f, 0.4f),
                         .colorOverAge = {{0.0f, glm::vec4(1.8f, 1.8f, 1.8f, 1.0f)},
                                          {0.15f, glm::vec4(1.2f, 1.2f, 1.2f, 0.9f)},
                                          {1.0f, glm::vec4(0.9f, 0.75f, 0.9f, 0.0f)}},
                         .headSize = 0.05f,
                         .headColor = glm::vec3(4.0f, 2.8f, 1.4f),
                         .softness = 0.05f,
                         .minPixels = 1.2f};
    for (int i = 0; i < b.wings; ++i) {
        const render::TrailId t = scene.addTrail(wing);
        scene.setTrailEmitting(t, false);
        if (sparkles) scene.setTrailSparkles(t, m_wingSparkles, b.wingSparkles);
        m_wings.push_back(t);
    }
}

// Where head k of a stream is at time t: along its course, weaving round it.
glm::vec3 Particles::headAt(const Stream& stream, int k, double t) const {
    const quench::particles::Path& path = stream.path;
    const auto along = float(std::fmod(t * double(stream.look.speed), double(path.length())) -
                             double(float(k) * stream.look.gap));
    const glm::vec3 tangent = path.tangent(along);
    glm::vec3 side = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
    side = glm::length(side) > 1e-3f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 normal = glm::cross(side, tangent);
    const float phase = 2.39996f * float(k); // the golden angle: the heads spread evenly round the course
    const float w = 1.3f * float(t) + phase;
    return path.at(along) + stream.look.braid * (side * std::sin(w) + normal * std::cos(0.8f * w + phase));
}

void Particles::update(render::Scene& scene, const World& world, const Frame& f) {
    const bool jump = f.jump || !m_started;
    for (const Stream& stream : m_streams)
        for (std::size_t k = 0; k < stream.heads.size(); ++k) {
            const glm::vec3 at = headAt(stream, int(k), f.time);
            scene.moveTrail(stream.heads[k], at, f.time, !m_started);
            if (k < stream.halos.size()) scene.moveTrail(stream.halos[k], at, f.time, !m_started);
        }

    // The cape's ribbons: on while flying (kept through the moment between a flap and a glide), fading out over
    // their life after landing.
    m_sinceFlight = f.flying ? 0.0f : m_sinceFlight + f.dt;
    const bool emit = f.flying || (m_emitting && m_sinceFlight < 0.3f && !f.jump);
    if (emit != m_emitting) {
        for (const render::TrailId wing : m_wings) scene.setTrailEmitting(wing, emit);
        m_emitting = emit;
    }
    const std::array<glm::vec3, 2> tips =
        m_wings.size() == 1 ? std::array{f.capeCentre, f.capeCentre} : std::array{f.capeLeft, f.capeRight};
    for (std::size_t i = 0; i < m_wings.size(); ++i) scene.moveTrail(m_wings[i], tips[i], f.time, jump);
    if (f.flapped) {
        // A flap scatters a puff of sparkles from the cape.
        std::array<glm::vec3, 24> puff;
        for (std::size_t i = 0; i < puff.size(); ++i) {
            const float t = float(i) / float(puff.size());
            puff[i] = glm::mix(f.capeLeft, f.capeRight, t) + (f.capeCentre - (f.capeLeft + f.capeRight) * 0.5f) *
                                                                 (0.5f + std::sin(t * 19.0f));
        }
        scene.emitParticles(m_wingSparkles, puff, f.velocity * 0.2f);
    }
    m_started = true;

    scene.setParticlesEmitting(m_rising, f.updraft);
    emitField(scene, world, f);
}

void Particles::emitField(render::Scene& scene, const World& world, const Frame& f) {
    // Points owed by this frame: the rate thickens as the sun goes down.
    const Budget b = budgetOf(m_quality);
    m_fieldDue += double(b.fieldRate * (0.35f + 0.65f * std::clamp(f.dusk, 0.0f, 1.0f)) * f.dt);
    const int count = std::min(int(m_fieldDue), 256);
    m_fieldDue -= double(int(m_fieldDue));
    if (count <= 0) return;
    const auto random = [this] {
        m_random = m_random * 1664525u + 1013904223u;
        return float(m_random >> 8) * (1.0f / 16777216.0f);
    };
    // In a ring round the eye: points nearer than its inside would be large soft discs on the screen.
    constexpr float kInside = 5.0f, kRadius = 30.0f;
    m_spawns.clear();
    for (int i = 0; i < count; ++i) {
        const float r = std::sqrt(kInside * kInside + (kRadius * kRadius - kInside * kInside) * random());
        const float a = 2.0f * kPi * random(), lift = random();
        const glm::vec2 xz(f.eye.x + r * std::cos(a), f.eye.z + r * std::sin(a));
        const std::optional<float> ground = world.terrainHeight(xz);
        // Only over the meadows near the camera, not far below it.
        if (!ground || f.eye.y - (*ground + kAltitude) > 14.0f) continue;
        m_spawns.emplace_back(xz.x, *ground + kAltitude + 0.15f + 1.3f * lift * lift, xz.y);
    }
    if (!m_spawns.empty()) scene.emitParticles(m_field, m_spawns);
}

} // namespace cloudsong
