#include "Spirits.h"

#include "Physics.h"

#include <quench/core/Log.h>

namespace cloudsong {

namespace {

namespace nav = quench::navigation;
namespace render = quench::render;

const glm::vec3 kAlt(0.0f, kAltitude, 0.0f);

constexpr float kWakeSeconds = 1.8f;
constexpr float kFlySpeed = 11.0f;
constexpr int kMarks = 3;

const nav::AgentParams kAgent{.radius = 0.3f,
                              .height = 1.0f,
                              .maxSpeed = 5.4f,
                              .maxAcceleration = 14.0f,
                              .separation = 1.0f,
                              .avoidance = nav::Avoidance::Medium,
                              .arrivalDistance = 0.5f};

float horizontal(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

} // namespace

Result<void> Spirits::create(render::Scene& scene, render::SurfaceId surface, const Landmarks& landmarks,
                             nav::Geometry geometry, quench::JobSystem& jobs) {
    m_landmarks = &landmarks;
    render::MaterialDesc figure{.surface = surface, .roughness = 0.8f, .doubleSided = true};
    figure.params[0] = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    const render::MaterialId material = scene.addMaterial(figure);
    const glm::vec3 capes[3] = {srgb(84, 150, 206), srgb(232, 178, 74), srgb(156, 112, 196)};
    const glm::vec3 inners[3] = {srgb(40, 78, 120), srgb(150, 98, 36), srgb(86, 56, 120)};
    for (std::size_t i = 0; i < landmarks.spirits.size(); ++i) {
        Spirit s;
        s.look.cape = capes[i % 3];
        s.look.capeInner = inners[i % 3];
        s.look.hood = s.look.cape * 0.85f;
        s.look.scale = 0.78f;
        s.look.capeLength = 0.9f;
        s.look.longHood = i != 1;
        CharacterMesh mesh = Character::buildMesh(s.look);
        render::MeshDesc desc = mesh.mesh.desc("spirit");
        desc.skin = mesh.skin;
        desc.jointCount = kJointCount;
        Result<render::MeshId> id = scene.addMesh(desc);
        if (!id) return Error{id.error()};
        s.figure = Character(s.look);
        s.feet = s.previous = landmarks.spirits[i] + kAlt;
        s.yaw = landmarks.spiritYaw[i];
        s.figure.setPlacement(s.feet, s.yaw);
        s.instance = scene.addInstance({.mesh = *id, .material = material, .transform = s.figure.transform()});
        m_spirits.push_back(std::move(s));
    }

    // The navmesh, built on jobs over the next frames.
    nav::BuildSettings build;
    build.agent = {.radius = 0.3f, .height = 1.1f, .maxClimb = 0.45f, .maxSlope = 50.0f};
    build.cellSize = 0.25f;
    build.cellHeight = 0.1f;
    build.tileSize = 64;
    build.boundsMin = glm::vec3(-85.0f, -40.0f, -55.0f);
    build.boundsMax = glm::vec3(95.0f, 90.0f, 250.0f);
    Result<nav::NavMesh> mesh = nav::NavMesh::build(std::move(geometry), build, jobs);
    if (!mesh) {
        // The spirits still follow, through the air only.
        quench::log::warning("game", "spirits: no navmesh: {}", mesh.error());
        return {};
    }
    m_mesh.emplace(std::move(*mesh));
    m_crowd.emplace(*m_mesh, nav::CrowdSettings{.maxAgents = 8, .maxAgentRadius = 0.4f});
    m_query.emplace(*m_mesh);
    m_query->setSearchExtents(glm::vec3(1.5f, 3.0f, 1.5f));
    return {};
}

void Spirits::destroy() {
    m_crowd.reset();
    m_query.reset();
    m_mesh.reset();
    m_spirits.clear();
}

int Spirits::awake() const {
    return int(
        std::count_if(m_spirits.begin(), m_spirits.end(), [](const Spirit& s) { return s.state != State::Dormant; }));
}

int Spirits::walking() const {
    return int(
        std::count_if(m_spirits.begin(), m_spirits.end(), [](const Spirit& s) { return s.state == State::Walking; }));
}

int Spirits::seated() const {
    return int(std::count_if(m_spirits.begin(), m_spirits.end(),
                             [](const Spirit& s) { return s.state == State::Gathered && s.sit > 0.9f; }));
}

float Spirits::glow(int spirit) const { return m_spirits[std::size_t(spirit)].wake; }

void Spirits::wake(int spirit) {
    if (spirit < 0 || spirit >= int(m_spirits.size())) return;
    Spirit& s = m_spirits[std::size_t(spirit)];
    if (s.state != State::Dormant) return;
    s.slot = awake();
    s.state = State::Waking;
}

void Spirits::gather() {
    m_gathering = true;
    for (Spirit& s : m_spirits)
        if (s.state == State::Walking || s.state == State::Flying) {
            if (s.agent) takeOff(s);
            s.state = State::Gathered;
        }
}

glm::vec3 Spirits::followPoint(const Spirit& s, const SpiritTarget& target) const {
    const glm::vec3 forward(std::sin(target.yaw), 0.0f, std::cos(target.yaw));
    const glm::vec3 right(std::cos(target.yaw), 0.0f, -std::sin(target.yaw));
    const float back = 1.5f + 0.7f * float(s.slot);
    const float side = s.slot == 0 ? -0.9f : s.slot == 1 ? 0.9f : 0.0f;
    return target.feet - forward * back + right * side;
}

bool Spirits::land(Spirit& s) {
    if (!m_mesh || !m_mesh->ready() || !m_crowd || !m_query) return false;
    const Result<nav::NavPoint> p = m_query->nearestPoint(s.feet - kAlt);
    if (!p || std::abs(p->position.y + kAltitude - s.feet.y) > 2.5f) return false;
    Result<nav::AgentId> agent = m_crowd->addAgent(p->position, kAgent);
    if (!agent) return false;
    s.agent = *agent;
    s.state = State::Walking;
    s.retarget = 0.0f;
    return true;
}

void Spirits::takeOff(Spirit& s) {
    if (s.agent && m_crowd) m_crowd->removeAgent(*s.agent);
    s.agent.reset();
    s.state = State::Flying;
    s.settled = 0.0f;
}

void Spirits::update(float dt, const SpiritTarget& target, render::Scene& scene, const quench::PhysicsWorld& physics) {
    m_time += dt;
    if (m_mesh) m_mesh->update();
    const Landmarks& l = *m_landmarks;

    auto fly = [&](Spirit& s, glm::vec3 goal, float speed) {
        const glm::vec3 to = goal - s.feet;
        const float d = glm::length(to);
        const glm::vec3 want = d > 0.01f ? to / d * std::min(d * 1.6f, speed) : glm::vec3(0.0f);
        s.velocity = approach(s.velocity, want, 0.3f, dt);
        s.feet += s.velocity * dt;
        // Over the ground, never through it.
        if (const std::optional<float> g = groundBelow(physics, s.feet + glm::vec3(0.0f, 2.0f, 0.0f), 2.5f))
            s.feet.y = std::max(s.feet.y, *g);
    };

    for (std::size_t i = 0; i < m_spirits.size(); ++i) {
        Spirit& s = m_spirits[i];
        s.previous = s.feet;
        switch (s.state) {
        case State::Dormant:
            s.wake = 0.0f;
            s.sit = 1.0f;
            break;
        case State::Waking:
            s.wake = std::min(1.0f, s.wake + dt / kWakeSeconds);
            s.sit = 1.0f - smoothstep(0.35f, 0.9f, s.wake);
            if (s.wake >= 1.0f && !land(s)) takeOff(s);
            break;
        case State::Walking: {
            const float far = glm::distance(s.feet, target.feet);
            const bool away = !target.grounded && target.airTime > 0.9f &&
                              (target.feet.y - s.feet.y > 2.5f || horizontal(s.feet, target.feet) > 7.0f);
            const nav::AgentState st = m_crowd->state(*s.agent);
            if (far > 18.0f || away || (st.partial && far > 6.0f) || st.move == nav::AgentMove::Lost) {
                takeOff(s);
                break;
            }
            s.retarget -= dt;
            if (s.retarget <= 0.0f) {
                s.retarget = 0.25f;
                if (const Result<nav::NavPoint> p = m_query->nearestPoint(followPoint(s, target) - kAlt))
                    m_crowd->setTarget(*s.agent, p->position);
            }
            break;
        }
        case State::Flying: {
            glm::vec3 goal = followPoint(s, target);
            if (!target.grounded) goal.y += 1.2f;
            fly(s, goal, kFlySpeed);
            s.settled = target.grounded && glm::distance(s.feet, goal) < 2.5f ? s.settled + dt : 0.0f;
            if (s.settled > 0.3f) land(s);
            break;
        }
        case State::Gathered: {
            const float a = kTau * float(i) / float(m_spirits.size()) + 0.6f;
            const glm::vec3 seat = l.altar + kAlt + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 2.6f;
            fly(s, seat, 6.0f);
            const bool there = glm::distance(s.feet, seat) < 0.25f;
            s.sit = approach(s.sit, there ? 1.0f : 0.0f, 0.5f, dt);
            if (there) s.velocity = glm::vec3(0.0f);
            break;
        }
        }
    }
    if (m_crowd && m_mesh && m_mesh->ready()) m_crowd->update(dt);

    for (std::size_t i = 0; i < m_spirits.size(); ++i) {
        Spirit& s = m_spirits[i];
        if (s.state == State::Walking && s.agent) {
            const nav::AgentState st = m_crowd->state(*s.agent);
            s.feet = st.position + kAlt;
            s.velocity = st.velocity;
        }
        const bool flying =
            s.state == State::Flying || (s.state == State::Gathered && s.sit < 0.5f && glm::length(s.velocity) > 1.5f);
        s.glide = approach(s.glide, flying ? 1.0f : 0.0f, 0.2f, dt);
        const glm::vec2 h(s.velocity.x, s.velocity.z);
        if (glm::length(h) > 0.3f) {
            s.yaw = wrapAngle(s.yaw + wrapAngle(std::atan2(h.x, h.y) - s.yaw) * std::min(1.0f, dt * 8.0f));
        } else if (s.state == State::Gathered) {
            const glm::vec3 to = l.altar + kAlt - s.feet;
            s.yaw = wrapAngle(s.yaw + wrapAngle(std::atan2(to.x, to.z) - s.yaw) * std::min(1.0f, dt * 3.0f));
        } else if (s.state == State::Walking) {
            const glm::vec3 to = target.feet - s.feet;
            s.yaw = wrapAngle(s.yaw + wrapAngle(std::atan2(to.x, to.z) - s.yaw) * std::min(1.0f, dt * 3.0f));
        }
        PoseInput pose;
        pose.speed = glm::length(h);
        pose.grounded = s.glide < 0.5f;
        pose.verticalSpeed = s.velocity.y;
        pose.glide = s.glide;
        pose.sit = s.sit;
        pose.pray = s.state == State::Gathered ? s.sit * (0.5f + 0.5f * std::sin(m_time * 0.8f + float(i))) : 0.0f;
        if (s.state != State::Dormant) pose.lookAt = target.feet + glm::vec3(0.0f, 1.0f, 0.0f);
        // Floating a little while it glides; bobbing.
        const glm::vec3 lift(0.0f, s.glide * (0.15f + 0.1f * std::sin(m_time * 2.3f + float(i))), 0.0f);
        s.figure.setPlacement(s.feet + lift, s.yaw, s.glide * 0.9f);
        s.figure.update(dt, pose, [&](glm::vec3 from) { return groundBelow(physics, from, 1.2f); });
        scene.setTransform(s.instance, s.figure.transform());
        scene.setJointPalette(s.instance, s.figure.palette());
        const float flash = s.state == State::Waking ? std::sin(s.wake * kPi) : 0.0f;
        scene.setInstanceColor(s.instance, glm::vec4(1.0f),
                               glm::vec4(float(kMarks) * s.wake, float(kMarks), flash, 1.0f - s.wake));
    }
}

} // namespace cloudsong
