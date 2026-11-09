#include "Particles.h"

#include "Util.h"
#include "World.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

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
    int guideHeads;             // the call's guide
    float guideSparkles;        // shed per metre by each of its heads
    std::uint32_t subdivisions; // strip vertices per recorded segment
    int wings;                  // 1: a ribbon from the cape's middle; 2: one from each lower corner
    float wingSparkles;         // shed per metre by each ribbon
    std::uint32_t wingSlots;
    std::uint32_t fieldSlots;
    float fieldRate; // candidate points per second at dusk; the field's density keeps a share of them
    float motes;     // the beam's, the updraft's and the cave's rates and slots, times
    bool halos;      // a soft band round each of the guide's and the cape's ribbons
};

Budget budgetOf(Quality q) {
    switch (q) {
    case Quality::High: return {3, 0.35f, 4, 2, 1.0f, 384, 512, 480.0f, 1.0f, true};
    case Quality::Medium: return {3, 0.25f, 4, 2, 0.7f, 256, 320, 300.0f, 0.65f, true};
    case Quality::Low: return {2, 0.15f, 3, 1, 0.5f, 128, 192, 170.0f, 0.4f, false};
    }
    return budgetOf(Quality::High);
}

// The motes' slots and rates (per second) on the finest setting; the others scale both by Budget::motes.
struct Motes {
    float slots, rate;
};
constexpr Motes kBeamMotes{128.0f, 5.0f}, kRisingMotes{192.0f, 12.0f}, kCaveMotes{160.0f, 12.0f};

// The field's points round each lantern per second on the finest setting, at dusk (the others scale them like the
// field's rate).
constexpr float kLanternPoints = 5.0f;

// The guide: how long it lingers round its goal (s), how long a point of its ribbons lasts (s) and how long they
// get at most (m), how closely each head follows the one before (s), and the circle it lingers on (m).
constexpr float kLinger = 3.2f, kGuideTrail = 1.3f, kGuideLength = 4.0f, kFollow = 0.22f, kLingerRadius = 1.1f;

std::uint32_t slots(float base, float scale) { return std::max(32u, std::uint32_t(base * scale)); }

// Fades in quickly, holds, fades out.
ColorCurve fadeInOut(float in, float out, float peak = 1.0f) {
    return {{0.0f, glm::vec4(peak, peak, peak, 0.0f)},
            {in, glm::vec4(peak, peak, peak, 1.0f)},
            {out, glm::vec4(1.0f)},
            {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}};
}

glm::mat4 at(glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); }
glm::vec2 xz(glm::vec3 p) { return {p.x, p.z}; }

// A ribbon of the guide: a thin pale gold core with a small glowing head (additive), or the soft band of its colour
// round it (alpha-blended, so that the hue holds against a bright sky where added light would turn white).
TrailDesc guideRibbon(const Budget& b, bool halo) {
    const glm::vec3 color(2.0f, 1.45f, 0.62f);
    return {.lifetime = kGuideTrail,
            .spacing = 0.25f,
            .maxPoints = 64,
            .maxLength = kGuideLength,
            .subdivisions = b.subdivisions,
            .width = halo ? 0.45f : 0.075f,
            .widthOverAge = {{0.0f, 0.6f}, {0.08f, 1.0f}, {0.5f, 0.6f}, {1.0f, 0.0f}},
            .color = color * (halo ? 0.8f : 1.0f),
            .opacity = halo ? 0.22f : 0.9f,
            .colorOverAge = {{0.0f, glm::vec4(1.3f, 1.3f, 1.3f, 1.0f)},
                             {0.15f, glm::vec4(1.0f, 1.0f, 1.0f, 0.9f)},
                             {0.5f, glm::vec4(0.85f, 0.85f, 0.9f, 0.5f)},
                             {1.0f, glm::vec4(0.7f, 0.7f, 0.8f, 0.0f)}},
            .headSize = halo ? 0.0f : 0.22f,
            .headColor = color + glm::vec3(0.5f),
            .blend = halo ? Blend::Alpha : Blend::Additive,
            .softness = halo ? 1.0f : 0.35f,
            .minPixels = halo ? 1.0f : 1.3f};
}

} // namespace

quench::Result<void> Particles::create(render::Scene& scene, const World& world, Quality quality) {
    const Landmarks& l = world.landmarks();
    const glm::vec3 alt(0.0f, kAltitude, 0.0f);
    m_beamBase = l.beamBase + alt;
    m_updraft = l.updraft + alt;
    m_updraftRadius = l.updraftRadius;
    m_updraftTop = l.updraftTop + kAltitude;
    m_cave = l.caveCentre + alt;
    m_caveEntrance = l.caveEntrance + alt;
    m_caveRadius = l.caveRadius;
    m_caveHeight = l.caveHeight;
    m_lanterns.clear();
    for (const glm::vec3& lantern : l.lanterns) m_lanterns.push_back(lantern + alt);
    m_quality = quality;
    const Budget b = budgetOf(quality);

    // Sparse twinkling points round the camera where the field is dense (placed by emitField on the ground).
    quench::Result<render::ParticleSystemId> field = scene.addParticles({.maxParticles = b.fieldSlots,
                                                                         .seed = 7,
                                                                         .rate = 0.0f,
                                                                         .lifetime = {3.5f, 6.0f},
                                                                         .speed = {0.025f, 0.055f},
                                                                         .spread = kPi,
                                                                         .size = {0.018f, 0.035f},
                                                                         .drag = 0.8f,
                                                                         .noise = 0.045f,
                                                                         .noiseScale = 3.0f,
                                                                         .noiseRate = 0.07f,
                                                                         .wind = 0.01f,
                                                                         .color = glm::vec3(1.25f, 1.05f, 0.65f),
                                                                         .colorOverLife = fadeInOut(0.2f, 0.75f),
                                                                         .brightnessVariation = 0.6f,
                                                                         .twinkle = 0.55f,
                                                                         .twinkleRate = 0.45f,
                                                                         .twinkleSharpness = 2.0f,
                                                                         .softness = 0.15f,
                                                                         .nearFade = 1.5f,
                                                                         .minPixels = 0.75f});
    if (!field) return quench::Error{field.error()};
    m_field = *field;

    // A few motes rising slowly in the temple's beam, quickly in the open updraft, and drifting in the cave's light.
    quench::Result<render::ParticleSystemId> beam =
        scene.addParticles({.maxParticles = slots(kBeamMotes.slots, b.motes),
                            .seed = 13,
                            .rate = kBeamMotes.rate * b.motes,
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
    quench::Result<render::ParticleSystemId> rising =
        scene.addParticles({.maxParticles = slots(kRisingMotes.slots, b.motes),
                            .seed = 17,
                            .rate = kRisingMotes.rate * b.motes,
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
    quench::Result<render::ParticleSystemId> cave =
        scene.addParticles({.maxParticles = slots(kCaveMotes.slots, b.motes),
                            .seed = 19,
                            .rate = kCaveMotes.rate * b.motes,
                            .shape = Shape::Box,
                            .extent = glm::vec3(2.5f, 1.0f, 3.5f),
                            .lifetime = {6.0f, 10.0f},
                            .speed = {0.025f, 0.07f},
                            .spread = kPi,
                            .size = {0.025f, 0.045f},
                            .noise = 0.12f,
                            .noiseScale = 2.0f,
                            .color = glm::vec3(.35f, 2.2f, 1.8f),
                            .colorOverLife = fadeInOut(0.15f, 0.8f),
                            .brightnessVariation = 0.5f,
                            .twinkle = 0.6f,
                            .twinkleRate = 1.2f,
                            .softness = 0.1f,
                            .minPixels = 1.2f},
                           at(m_cave + glm::vec3(0.0f, 2.5f, 0.0f)));
    if (!cave) return quench::Error{cave.error()};
    m_caveMotes = *cave;

    buildGuide(scene);
    buildWings(scene);
    return {};
}

void Particles::setQuality(render::Scene& scene, Quality quality) {
    if (quality == m_quality) return;
    m_quality = quality;
    const Budget b = budgetOf(quality);
    removeGuide(scene);
    for (const Wing& wing : m_wings)
        for (const render::TrailId t : wing.trails) scene.removeTrail(t);
    m_wings.clear();
    scene.removeParticles(m_wingSparkles);
    buildGuide(scene);
    buildWings(scene);
    m_emitting = false;
    m_started = false;
    if (const ParticleDesc* d = scene.particleDesc(m_field)) {
        ParticleDesc field = *d;
        field.maxParticles = b.fieldSlots;
        (void)scene.setParticles(m_field, field);
    }
    for (const auto& [id, motes] :
         {std::pair{m_beam, kBeamMotes}, std::pair{m_rising, kRisingMotes}, std::pair{m_caveMotes, kCaveMotes}})
        if (const ParticleDesc* d = scene.particleDesc(id)) {
            ParticleDesc desc = *d;
            desc.maxParticles = slots(motes.slots, b.motes);
            desc.rate = motes.rate * b.motes;
            (void)scene.setParticles(id, desc);
        }
}

void Particles::buildGuide(render::Scene& scene) {
    const Budget b = budgetOf(m_quality);
    // A few pale gold sparkles that drift off the heads, sink and go out.
    quench::Result<render::ParticleSystemId> sparkles = scene.addParticles({.maxParticles = 128,
                                                                            .seed = 101,
                                                                            .rate = 0.0f,
                                                                            .lifetime = {0.8f, 1.8f},
                                                                            .speed = {0.05f, 0.3f},
                                                                            .spread = kPi,
                                                                            .inherit = 0.03f,
                                                                            .size = {0.03f, 0.06f},
                                                                            .gravity = glm::vec3(0.0f, -0.15f, 0.0f),
                                                                            .drag = 1.2f,
                                                                            .noise = 0.2f,
                                                                            .noiseScale = 3.0f,
                                                                            .wind = 0.2f,
                                                                            .color = glm::vec3(2.4f, 1.9f, 1.0f),
                                                                            .colorOverLife = fadeInOut(0.05f, 0.5f),
                                                                            .brightnessVariation = 0.6f,
                                                                            .twinkle = 0.7f,
                                                                            .twinkleRate = 2.5f,
                                                                            .twinkleSharpness = 3.0f,
                                                                            .sprite = Sprite::Star,
                                                                            .softness = 0.2f,
                                                                            .nearFade = 0.5f,
                                                                            .minPixels = 1.2f});
    if (sparkles) m_guideSparkles = *sparkles;
    for (int k = 0; k < b.guideHeads; ++k) {
        const render::TrailId head = scene.addTrail(guideRibbon(b, false));
        if (sparkles) scene.setTrailSparkles(head, m_guideSparkles, b.guideSparkles);
        scene.setTrailEmitting(head, false);
        m_guideHeads.push_back(head);
        if (b.halos) {
            const render::TrailId halo = scene.addTrail(guideRibbon(b, true));
            scene.setTrailEmitting(halo, false);
            m_guideHalos.push_back(halo);
        }
    }
    m_guideAt.assign(m_guideHeads.size(), glm::vec3(0.0f));
    m_guideAge = -1.0f;
    m_guideEmitting = false;
}

void Particles::removeGuide(render::Scene& scene) {
    for (const render::TrailId t : m_guideHeads) scene.removeTrail(t);
    for (const render::TrailId t : m_guideHalos) scene.removeTrail(t);
    scene.removeParticles(m_guideSparkles);
    m_guideHeads.clear();
    m_guideHalos.clear();
}

void Particles::buildWings(render::Scene& scene) {
    const Budget b = budgetOf(m_quality);
    // Gold sparkles that drift off the ribbons, sink and go out.
    quench::Result<render::ParticleSystemId> sparkles = scene.addParticles({.maxParticles = b.wingSlots,
                                                                            .seed = 23,
                                                                            .rate = 0.0f,
                                                                            .lifetime = {0.8f, 1.8f},
                                                                            .speed = {0.1f, 0.5f},
                                                                            .spread = kPi,
                                                                            .inherit = 0.08f,
                                                                            .size = {0.012f, 0.025f},
                                                                            .gravity = glm::vec3(0.0f, -0.35f, 0.0f),
                                                                            .drag = 1.2f,
                                                                            .noise = 0.3f,
                                                                            .noiseScale = 1.5f,
                                                                            .wind = 0.3f,
                                                                            .color = glm::vec3(0.8f, 0.5f, 0.2f),
                                                                            .colorOverLife = fadeInOut(0.05f, 0.45f),
                                                                            .brightnessVariation = 0.5f,
                                                                            .twinkle = 0.7f,
                                                                            .twinkleRate = 4.0f,
                                                                            .sprite = Sprite::Star,
                                                                            .softness = 0.05f,
                                                                            .nearFade = 0.3f,
                                                                            .minPixels = 0.75f});
    if (sparkles) m_wingSparkles = *sparkles;
    // Long translucent ribbons keep their gesture without washing out the night sky.
    // The engine's cross-strip profile is 1 at the axis and 0 at either edge.
    // Both layers add light; a narrow core defines the line, with a faint surrounding glow.
    TrailDesc body{.lifetime = 1.4f,
                   .spacing = 0.12f,
                   .maxPoints = 128,
                   .maxLength = 10.0f,
                   .subdivisions = b.subdivisions,
                   .width = 0.42f,
                   .widthOverAge = {{0.0f, 0.4f}, {0.06f, 1.0f}, {0.4f, 0.65f}, {1.0f, 0.0f}},
                   .color = glm::vec3(0.45f, 0.28f, 0.1f),
                   .opacity = 0.055f,
                   .colorOverAge = {{0.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)},
                                    {0.08f, glm::vec4(1.0f)},
                                    {0.2f, glm::vec4(1.0f, 1.0f, 1.0f, 0.8f)},
                                    {0.5f, glm::vec4(1.0f, 1.0f, 1.0f, 0.45f)},
                                    {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}},
                   .blend = Blend::Additive,
                   .softness = 0.1f,
                   .minPixels = 0.75f};
    TrailDesc core = body;
    core.width = 0.014f;
    core.color = glm::vec3(0.9f, 0.65f, 0.3f);
    core.opacity = 0.2f;
    core.minPixels = 0.6f;
    m_wingCoreDesc = core;
    m_wingDesc = body;
    m_wingDaylight = -1.0f;
    for (int i = 0; i < b.wings; ++i) {
        Wing wing{.trails = {scene.addTrail(body), scene.addTrail(core)}};
        if (sparkles) scene.setTrailSparkles(wing.trails[1], m_wingSparkles, b.wingSparkles);
        for (const render::TrailId t : wing.trails) scene.setTrailEmitting(t, false);
        m_wings.push_back(std::move(wing));
    }
}

void Particles::arcTo(std::vector<glm::vec3>& route, const World& world, glm::vec3 to, float rise) const {
    const glm::vec3 from = route.back();
    const float length = glm::distance(from, to);
    const int n = std::max(2, int(std::ceil(length / 4.0f)));
    std::vector<glm::vec3> points{from};
    std::vector<float> floor{from.y};
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n);
        glm::vec3 p = glm::mix(from, to, t);
        p.y += rise * std::sin(kPi * t);
        // Close to its ends it may come down to them; between them it keeps 3 m over the ground and the cave's roof.
        const float clearance = 0.6f + 2.4f * smoothstep(0.0f, 6.0f, std::min(t, 1.0f - t) * length);
        float low = p.y;
        if (i < n) {
            if (const std::optional<float> ground = world.terrainHeight(xz(p)))
                low = std::max(low, *ground + kAltitude + clearance);
            if (glm::distance(xz(p), xz(m_cave)) < m_caveRadius + 2.5f)
                low = std::max(low, m_cave.y + m_caveHeight + clearance);
        }
        floor.push_back(low);
        points.push_back(glm::vec3(p.x, low, p.z));
    }
    // Smooth the lifts so that the way climbs over a hill instead of stepping onto it; its ends stay.
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<glm::vec3> smooth = points;
        for (std::size_t i = 1; i + 1 < points.size(); ++i)
            smooth[i].y = std::max(0.25f * points[i - 1].y + 0.5f * points[i].y + 0.25f * points[i + 1].y, floor[i]);
        points = std::move(smooth);
    }
    route.insert(route.end(), points.begin() + 1, points.end());
}

void Particles::call(const World& world, glm::vec3 feet, std::optional<glm::vec3> goal) {
    // Out and flying: on from where its first head is; else up from beside the figure.
    const bool out = m_guideAge >= 0.0f && m_guideEmitting && !m_guideAt.empty();
    std::vector<glm::vec3> route{out ? m_guideAt.front() : feet + glm::vec3(0.0f, 0.8f, 0.0f)};
    const auto horizontal = [](glm::vec3 from, glm::vec3 to, glm::vec2 fallback) {
        const glm::vec2 d = xz(to) - xz(from);
        return glm::length(d) > 0.1f ? glm::normalize(d) : fallback;
    };
    const glm::vec2 side = horizontal(m_eye, feet, {0.0f, 1.0f});
    if (goal) {
        // The cottage candle is reached through its doorway, never through
        // its roof or the former cave portal.
        const auto house = world.landmarks().cottage + glm::vec3(0, kAltitude, 0);
        const auto inCave = [&](glm::vec3 p) {
            const auto q = p - house;
            return std::abs(q.x) < 2.6f && std::abs(q.z) < 2.4f && q.y < 3.2f;
        };
        const glm::vec3 inside = house + glm::vec3(1.35f, 1.7f, -1.65f);
        const glm::vec3 portal = house + glm::vec3(1.35f, 1.7f, -2.75f);
        const glm::vec3 outside = house + glm::vec3(1.35f, 1.7f, -5.0f);
        const bool fromCave = inCave(route.back()), toCave = inCave(*goal);
        const glm::vec3 next = fromCave && !toCave ? inside : !fromCave && toCave ? outside : *goal;
        if (!out) {
            // Up beside the figure first, a little toward where it goes.
            const glm::vec2 toward = horizontal(route.back(), next, side);
            route.push_back(route.back() + glm::vec3(0.6f * toward.x, 1.4f, 0.6f * toward.y));
        }
        const auto rise = [&](glm::vec3 to) {
            return std::clamp(0.12f * glm::distance(xz(route.back()), xz(to)), 1.0f, 9.0f);
        };
        if (fromCave && !toCave) route.insert(route.end(), {inside, portal, outside});
        if (!fromCave && toCave) {
            arcTo(route, world, outside, rise(outside));
            route.insert(route.end(), {portal, inside});
        }
        // It ends on the side of the goal it comes from, on the circle it lingers on.
        const glm::vec2 from = horizontal(*goal, route.back(), side);
        const glm::vec3 end = *goal + glm::vec3(from.x, 0.0f, from.y) * kLingerRadius;
        if (toCave)
            route.push_back(end);
        else
            arcTo(route, world, end, rise(end));
        m_lingerCentre = *goal;
        m_lingerRadius = kLingerRadius;
        m_lingerAngle = std::atan2(from.y, from.x);
    } else {
        // Once round the figure, starting at its side as the camera sees it, a little higher at the far side.
        const glm::vec3 centre = feet + glm::vec3(0.0f, 1.3f, 0.0f);
        const float start = std::atan2(side.y, side.x) + 0.5f * kPi;
        constexpr int kSteps = 28;
        for (int i = 1; i <= kSteps; ++i) {
            const float t = float(i) / float(kSteps);
            const float a = start + (kTau + 0.6f) * t;
            const float r = glm::mix(0.6f, 2.2f, smoothstep(0.0f, 0.25f, t));
            route.push_back(centre + glm::vec3(std::cos(a) * r, 0.4f * std::sin(kPi * t), std::sin(a) * r));
        }
        m_lingerRadius = 0.0f;
    }
    m_route = quench::particles::Path(route, false);
    m_travel = m_lingerRadius > 0.0f ? std::clamp(m_route.length() / 6.0f, 2.5f, 18.0f)
                                     : std::clamp(m_route.length() / 3.0f, 4.0f, 7.0f);
    m_guideRestart = !out;
    m_guideAge = 0.0f;
}

glm::vec3 Particles::guideLead(float age) const {
    if (age < m_travel || m_lingerRadius <= 0.0f) {
        const float s = std::clamp(age / m_travel, 0.0f, 1.0f);
        return m_route.at(m_route.length() * s * s * (3.0f - 2.0f * s));
    }
    // Round the goal, slowly gathering speed from rest, bobbing.
    const float u = age - m_travel;
    const float a = m_lingerAngle + 1.1f * u * u / (u + 0.8f);
    return m_lingerCentre +
           glm::vec3(std::cos(a) * m_lingerRadius, 0.12f * std::sin(1.8f * u), std::sin(a) * m_lingerRadius);
}

float Particles::guideDistance() const {
    if (m_guideAge < 0.0f || m_lingerRadius <= 0.0f || m_guideAt.empty()) return 0.0f;
    return glm::distance(m_guideAt.front(), m_lingerCentre);
}

void Particles::updateGuide(render::Scene& scene, const Frame& f) {
    if (m_guideAge < 0.0f) return;
    const float end = m_travel + (m_lingerRadius > 0.0f ? kLinger : 0.0f);
    const bool emit = m_guideAge < end;
    if (m_guideRestart)
        for (glm::vec3& p : m_guideAt) p = m_route.at(0.0f);
    if (emit != m_guideEmitting || m_guideRestart) {
        for (const render::TrailId t : m_guideHeads) scene.setTrailEmitting(t, emit);
        for (const render::TrailId t : m_guideHalos) scene.setTrailEmitting(t, emit);
        m_guideEmitting = emit;
    }
    if (emit) {
        // Each head follows the one before it and weaves gently round its way.
        const float follow = 1.0f - std::exp(-f.dt / kFollow);
        m_guideAt[0] = guideLead(m_guideAge);
        for (std::size_t k = 1; k < m_guideAt.size(); ++k) m_guideAt[k] += (m_guideAt[k - 1] - m_guideAt[k]) * follow;
        for (std::size_t k = 0; k < m_guideHeads.size(); ++k) {
            const float w = 1.5f * float(f.time) + 2.4f * float(k);
            const glm::vec3 weave =
                (k == 0 ? 0.08f : 0.28f) * glm::vec3(std::sin(w), 0.6f * std::cos(0.8f * w + float(k)), std::cos(w));
            const glm::vec3 p = m_guideAt[k] + weave;
            scene.moveTrail(m_guideHeads[k], p, f.time, m_guideRestart);
            if (k < m_guideHalos.size()) scene.moveTrail(m_guideHalos[k], p, f.time, m_guideRestart);
        }
    }
    m_guideRestart = false;
    m_guideAge += f.dt;
    // Gone once its ribbons have faded.
    if (m_guideAge > end + kGuideTrail + 0.3f) m_guideAge = -1.0f;
}

void Particles::update(render::Scene& scene, const World& world, const Frame& f) {
    m_eye = f.eye;
    updateGuide(scene, f);

    // Add enough light to read over sunlit grass; keep the restrained night radiance.
    const float daylight = 1.0f - std::clamp(f.dusk, 0.0f, 1.0f);
    if (std::abs(daylight - m_wingDaylight) > 0.001f) {
        TrailDesc desc = m_wingDesc;
        desc.color *= glm::mix(1.0f, 8.0f, daylight);
        TrailDesc core = m_wingCoreDesc;
        core.color *= glm::mix(1.0f, 6.0f, daylight);
        for (const Wing& wing : m_wings) {
            scene.setTrail(wing.trails[0], desc);
            scene.setTrail(wing.trails[1], core);
        }
        m_wingDaylight = daylight;
    }

    // The cape's ribbons: on while flying (kept through the moment between a flap and a glide), fading out over
    // their life after landing.
    const bool jump = f.jump || !m_started;
    m_sinceFlight = f.flying ? 0.0f : m_sinceFlight + f.dt;
    const bool emit = f.flying || (m_emitting && m_sinceFlight < 0.3f && !f.jump);
    if (emit != m_emitting) {
        for (const Wing& wing : m_wings)
            for (const render::TrailId t : wing.trails) scene.setTrailEmitting(t, emit);
        m_emitting = emit;
    }
    const std::array<glm::vec3, 2> tips =
        m_wings.size() == 1 ? std::array{f.capeCentre, f.capeCentre} : std::array{f.capeLeft, f.capeRight};
    for (std::size_t i = 0; i < m_wings.size(); ++i)
        for (const render::TrailId t : m_wings[i].trails) scene.moveTrail(t, tips[i], f.time, jump);
    if (f.flapped) {
        // A flap scatters a puff of sparkles from the cape.
        std::array<glm::vec3, 8> puff;
        for (std::size_t i = 0; i < puff.size(); ++i) {
            const float t = float(i) / float(puff.size());
            puff[i] = glm::mix(f.capeLeft, f.capeRight, t) +
                      (f.capeCentre - (f.capeLeft + f.capeRight) * 0.5f) * (0.5f + std::sin(t * 19.0f));
        }
        scene.emitParticles(m_wingSparkles, puff, f.velocity * 0.2f);
    }
    m_started = true;

    scene.setParticlesEmitting(m_rising, f.updraft);
    emitField(scene, world, f);
}

// How much of the field stands at a place (0..1): over the flower fields, along the isles' rims, and in the cave,
// where it is dim.
float Particles::fieldDensity(const World& world, glm::vec2 at) const {
    float density = 0.0f;
    if (glm::distance(at, xz(m_cave)) < m_caveRadius) density = 0.9f;
    for (const Isle& isle : world.isles()) {
        const glm::vec2 rel = at - isle.desc.centre;
        const float t = glm::length(rel) / isle.outline(std::atan2(rel.y, rel.x));
        if (t >= 1.0f) continue;
        // The flower fields are World's: the same noise.
        const float field = fbm(at / 7.0f, 2, isle.desc.seed + 31) * 0.5f + 0.5f;
        const float flowers = std::min(isle.desc.flowers, 1.0f) * smoothstep(0.63f, 0.78f, field);
        density = std::max(density, std::max(0.45f * flowers, 0.3f * smoothstep(0.84f, 0.97f, t)));
        break;
    }
    return density;
}

void Particles::emitField(render::Scene& scene, const World& world, const Frame& f) {
    // Candidates owed by this frame: the rate thickens as the sun goes down.
    const Budget b = budgetOf(m_quality);
    const float dusk = 0.35f + 0.65f * std::clamp(f.dusk, 0.0f, 1.0f);
    m_fieldDue += double(b.fieldRate * dusk * f.dt);
    const int count = std::min(int(m_fieldDue), 256);
    m_fieldDue -= double(int(m_fieldDue));
    m_lanternDue += double(kLanternPoints * b.fieldRate / budgetOf(Quality::High).fieldRate * dusk * f.dt);
    const int perLantern = std::min(int(m_lanternDue), 8);
    m_lanternDue -= double(int(m_lanternDue));
    if (count <= 0 && perLantern <= 0) return;
    const auto random = [this] {
        m_random = m_random * 1664525u + 1013904223u;
        return float(m_random >> 8) * (1.0f / 16777216.0f);
    };
    m_spawns.clear();
    // A few round each lantern near the camera.
    for (const glm::vec3& lantern : m_lanterns) {
        if (glm::distance(lantern, f.eye) > 35.0f) continue;
        for (int i = 0; i < perLantern; ++i) {
            const float r = glm::mix(0.5f, 3.0f, std::sqrt(random())), a = 2.0f * kPi * random(), lift = random();
            const glm::vec2 p = xz(lantern) + r * glm::vec2(std::cos(a), std::sin(a));
            const std::optional<float> ground = world.terrainHeight(p);
            if (!ground) continue;
            const glm::vec3 point(p.x, *ground + kAltitude + 0.2f + 1.8f * lift * lift, p.y);
            if (glm::distance(point, f.eye) > 2.5f) m_spawns.push_back(point);
        }
    }
    // In a ring round the eye: points nearer than its inside would be large soft discs on the screen.
    constexpr float kInside = 6.0f, kRadius = 30.0f;
    for (int i = 0; i < count; ++i) {
        const float r = std::sqrt(kInside * kInside + (kRadius * kRadius - kInside * kInside) * random());
        const float a = 2.0f * kPi * random(), lift = random(), keep = random();
        const glm::vec2 p(f.eye.x + r * std::cos(a), f.eye.z + r * std::sin(a));
        const std::optional<float> ground = world.terrainHeight(p);
        // Only over the meadows near the camera, not far below it, and only where the field stands.
        if (!ground || f.eye.y - (*ground + kAltitude) > 14.0f || keep >= fieldDensity(world, p)) continue;
        m_spawns.emplace_back(p.x, *ground + kAltitude + 0.15f + 1.3f * lift * lift, p.y);
    }
    if (!m_spawns.empty()) scene.emitParticles(m_field, m_spawns);
}

} // namespace cloudsong
