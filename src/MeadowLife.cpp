#include "MeadowLife.h"
#include <quench/particles/Trail.h>
namespace cloudsong {
using namespace quench;
namespace {
const glm::vec3 altitude(0, kAltitude, 0);
}
Result<void> MeadowLife::create(render::Scene& scene, render::SurfaceId surface, render::SceneAssets& assets,
                                animation::AnimationSystem& animation, const World& world) {
    m_scene = &scene;
    m_assets = &assets;
    m_animation = &animation;
    m_world = &world;
    m_home = world.landmarks().meadowGlade;
    m_position = m_previous = m_goal = m_home;
    m_model = assets.loadModel("meadow_creature.qmodel");
    m_skeleton = animation.loadSkeleton("meadow_creature.qskel");
    m_clips = animation.loadClips("meadow_creature.qanim");
    m_graph = animation.loadGraph("meadow_creature.graph.json");
    m_instance = assets.addModel(m_model, placed(m_position + altitude));
    const auto mesh = scene.addMesh(render::sphereMesh(1, 32, 48).desc("soap bubble"));
    if (!mesh) return Error{mesh.error()};
    render::MaterialDesc soap{.surface = surface,
                              .roughness = .08f,
                              .blend = render::BlendMode::AlphaBlend,
                              .shadows = render::ShadowMode::None};
    soap.params[0] = {6, 0, 0, 0};
    const auto material = scene.addMaterial(soap);
    for (int i = 0; i < 2; ++i) {
        auto& b = m_bubbles[i];
        b.radius = i ? .34f : .68f;
        b.position = b.previous = m_home + glm::vec3(i ? 1.8f : -1.3f, b.radius + .35f, i ? -1.2f : 1.0f);
        b.instance = scene.addInstance({.mesh = *mesh,
                                        .material = material,
                                        .transform = placed(b.position + altitude, 0, glm::vec3(b.radius)),
                                        .castsShadows = false});
        m_trails[i] = scene.addTrail({.lifetime = .32f,
                                      .spacing = .018f,
                                      .maxPoints = 32,
                                      .maxLength = .34f,
                                      .subdivisions = 4,
                                      .width = .035f,
                                      .color = {1.5f, .45f, 2.1f},
                                      .opacity = .32f,
                                      .headSize = .018f,
                                      .headColor = {1.1f, .3f, 1.5f},
                                      .blend = particles::Blend::Additive,
                                      .softness = .05f,
                                      .minPixels = .7f});
    }
    return {};
}
void MeadowLife::step(float dt, glm::vec3 player, glm::vec3 velocity) {
    m_time += dt;
    m_previous = m_position;
    m_previousYaw = m_yaw;
    const float distance = glm::distance(player, m_position);
    const float fromHome = glm::distance(glm::vec2(player.x, player.z), glm::vec2(m_home.x, m_home.z));
    if (distance < 6.0f && fromHome < 7.0f) m_attention = true;
    if (distance > 8.5f || fromHome > 9.0f) m_attention = false;
    glm::vec3 desired(0), look(0);
    if (m_attention) {
        look = player - m_position;
        look.y = 0;
        if (glm::length(look) > 1.45f) {
            auto target = player - glm::normalize(look) * 1.4f;
            auto off = target - m_home;
            off.y = 0;
            if (glm::length(off) > 3.5f) target = m_home + glm::normalize(off) * 3.5f;
            auto delta = target - m_position;
            delta.y = 0;
            if (glm::length(delta) > .15f) desired = glm::normalize(delta) * std::min(1.35f, glm::length(delta) * 1.7f);
        }
    } else {
        m_wait -= dt;
        auto delta = m_goal - m_position;
        delta.y = 0;
        if (glm::length(delta) < .15f && m_wait < 0) {
            const float a = m_random.uniform(0, kTau), r = m_random.uniform(.6f, 3.0f);
            m_goal = m_home + glm::vec3(std::sin(a) * r, 0, std::cos(a) * r);
            m_wait = m_random.uniform(3, 6);
        } else if (glm::length(delta) > .15f) {
            desired = glm::normalize(delta) * .60f;
            look = delta;
        }
    }
    m_velocity = glm::mix(m_velocity, desired, 1 - std::exp(-dt * 7));
    m_position += m_velocity * dt;
    m_position.y = m_home.y;
    if (glm::length(look) > .05f) {
        const float target = std::atan2(look.x, look.z);
        m_yaw += std::remainder(target - m_yaw, kTau) * (1 - std::exp(-dt * 5));
    }
    for (auto& b : m_bubbles) {
        b.previous = b.position;
        b.contact = std::max(0.0f, b.contact - dt);
        const float ground = m_world->terrainHeight({b.position.x, b.position.z}).value_or(m_home.y);
        const float rest = ground + b.radius + .30f + .08f * std::sin(m_time * .65f + b.radius * 4);
        b.velocity.y += (rest - b.position.y) * 3.5f * dt;
        b.velocity *= std::exp(-dt * .65f);
        b.position += b.velocity * dt;
        if (b.position.y < ground + b.radius) {
            b.position.y = ground + b.radius;
            b.velocity.y = std::abs(b.velocity.y) * .65f + .12f;
        }
        const auto playerPoint = player + glm::vec3(0, std::clamp(b.position.y - player.y, .18f, 1.0f), 0);
        auto delta = b.position - playerPoint;
        const float d = glm::length(delta), reach = b.radius + .30f;
        if (d < reach && d > .001f) {
            const auto n = delta / d;
            b.position = playerPoint + n * reach;
            const float impulse = std::max(.25f, glm::dot(velocity - b.velocity, n) * 1.15f);
            b.velocity += n * impulse;
            b.velocity.y += .15f;
            if (b.contact <= 0) {
                ++m_pushes;
                b.contact = .4f;
            }
        }
        // A soft breeze brings a bubble back before it reaches the cliffs or the benches.
        auto off = b.position - m_home;
        off.y = 0;
        const float r = glm::length(off);
        if (r > 3.0f) b.velocity -= off / r * (r - 3.0f) * dt * 1.3f;
        if (r > 3.8f) {
            b.position -= off / r * (r - 3.8f);
            const auto n = off / r;
            b.velocity -= n * std::max(0.0f, glm::dot(b.velocity, n)) * 1.6f;
        }
        const float speed = glm::length(b.velocity);
        if (speed > 6) b.velocity *= 6 / speed;
    }
    auto& a = m_bubbles[0];
    auto& b = m_bubbles[1];
    auto delta = b.position - a.position;
    const float d = glm::length(delta), reach = a.radius + b.radius;
    if (d < reach && d > .001f) {
        const auto n = delta / d;
        const float inverseA = 1 / (a.radius * a.radius), inverseB = 1 / (b.radius * b.radius);
        const float split = inverseA / (inverseA + inverseB);
        a.position -= n * (reach - d) * split;
        b.position += n * (reach - d) * (1 - split);
        const float closing = glm::dot(a.velocity - b.velocity, n);
        if (closing > 0) {
            a.velocity -= n * closing * 1.7f * split;
            b.velocity += n * closing * 1.7f * (1 - split);
        }
    }
}
void MeadowLife::prepare(bool playing) {
    m_playing = playing;
    if (!m_character && m_skeleton->data() && m_model->state() == assets::AssetState::Ready) {
        const auto low = m_model->boundsMin(), high = m_model->boundsMax();
        const float scale = .83f / std::max(.01f, high.y - low.y);
        m_normalize = glm::scale(glm::mat4(1), glm::vec3(scale)) *
                      glm::translate(glm::mat4(1), -glm::vec3((low.x + high.x) * .5f, low.y, (low.z + high.z) * .5f));
        m_character = m_animation->addCharacter(
            {.skeleton = m_skeleton, .clips = {m_clips}, .graph = m_graph, .events = false, .height = .83f});
    }
    if (m_character) {
        m_animation->setTimeScale(m_character, playing ? 1 : 0);
        m_animation->setFloat(m_character, "speed", speed());
    }
}
bool MeadowLife::ready() const { return m_character && m_animation->state(m_character).bound; }
void MeadowLife::draw(float alpha) {
    const auto position = m_playing ? m_previous + (m_position - m_previous) * alpha : m_position;
    const float yaw = m_previousYaw + std::remainder(m_yaw - m_previousYaw, kTau) * alpha;
    m_transform = placed(position + altitude, yaw) * m_normalize;
    m_assets->setTransform(m_instance, m_transform);
    if (ready()) {
        const auto models = m_animation->modelMatrices(m_character);
        glm::mat4 head(1);
        for (std::size_t s = 0; s < m_model->skins().size(); ++s) {
            const auto& skin = m_model->skins()[s];
            std::vector<glm::mat4> palette(skin.joints.size(), glm::mat4(1));
            for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                const int bone = m_skeleton->data()->findJoint(skin.joints[j]);
                if (bone >= 0 && std::size_t(bone) < models.size())
                    palette[j] = models[bone] * skin.inverseBindMatrices[j];
                if (skin.joints[j] == "Head") head = palette[j];
            }
            m_assets->setJointPalette(m_instance, int(s), palette);
        }
        // Purple iris centroids in the untouched export, carried by the same head
        // palette as the skin (including the imported bind orientation).
        constexpr std::array<glm::vec3, 2> eyes{{{-.03826f, .05460f, .89032f}, {.17714f, .04073f, .80926f}}};
        for (int i = 0; i < 2; ++i) {
            const auto eye = glm::vec3(m_transform * head * glm::vec4(eyes[i], 1));
            m_scene->moveTrail(m_trails[i], eye, m_time);
        }
    }
    for (const auto& b : m_bubbles) {
        const auto p = m_playing ? b.previous + (b.position - b.previous) * alpha : b.position;
        m_scene->setTransform(b.instance, placed(p + altitude, 0, glm::vec3(b.radius)));
    }
}
float MeadowLife::bubbleSpeed() const {
    return std::max(glm::length(m_bubbles[0].velocity), glm::length(m_bubbles[1].velocity));
}
void MeadowLife::destroy() {
    if (m_character) m_animation->removeCharacter(m_character);
    m_character = {};
    m_model = {};
    m_skeleton = {};
    m_clips = {};
    m_graph = {};
}
} // namespace cloudsong
