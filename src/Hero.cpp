#include "Hero.h"
#include "HeroData.h"

#include <quench/core/Log.h>

namespace cloudsong {
namespace {
using namespace quench;
using namespace hero_data;
constexpr std::array<std::string_view, kBodyJoints> bodyNames = {
    "",          "Hips",        "Spine",    "Chest",      "Neck",         "Head",
    "LeftUpLeg", "LeftLeg",     "LeftFoot", "RightUpLeg", "RightLeg",     "RightFoot",
    "LeftArm",   "LeftForeArm", "LeftHand", "RightArm",   "RightForeArm", "RightHand"};
int bone(std::string_view name) {
    for (int i = 0; i < int(bones.size()); ++i)
        if (bones[std::size_t(i)].name == name) return i;
    return -1;
}
glm::mat4 turn(glm::vec3 at, float angle, glm::vec3 axis) {
    return glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), angle, axis) *
           glm::translate(glm::mat4(1.0f), -at);
}
} // namespace

void Hero::create(render::Scene& scene, assets::AssetManager& assets, render::SurfaceId surface, Character& driver,
                  JobSystem& jobs, animation::GroundQuery ground) {
    m_assets = std::make_unique<render::SceneAssets>(scene, assets);
    m_model = m_assets->loadModel("hero.qmodel");
    m_animation = std::make_unique<animation::AnimationSystem>(assets, jobs);
    m_ground = std::move(ground);
    m_skeleton = m_animation->loadSkeleton("prepared/hero.qskel");
    m_clips = m_animation->loadClips("animations/hero-motion.hero.qanim");
    m_graph = m_animation->loadGraph("hero.graph.json");
    render::MaterialDesc marks{.surface = surface,
                               .roughness = 0.9f,
                               .blend = render::BlendMode::AlphaBlend,
                               .doubleSided = true,
                               .shadows = render::ShadowMode::None};
    marks.params[0].x = 5.0f;
    m_marks = scene.addMaterial(marks);
    std::array<glm::vec3, kBodyJoints> rest{};
    for (int j = 0; j < kBodyJoints; ++j) {
        m_correction[std::size_t(j)] = glm::mat4(1.0f);
        if (j > 0) rest[std::size_t(j)] = bones[std::size_t(bone(bodyNames[std::size_t(j)]))].position;
    }
    // Lower the source A-pose while preserving its actual limb lengths. A little
    // clearance at the armpits keeps the sleeves outside the inner tunic.
    for (int arm : {kLArm, kRArm}) {
        const float sign = arm == kLArm ? 1.0f : -1.0f;
        const glm::vec3 a = rest[std::size_t(arm)], b = rest[std::size_t(arm + 1)], c = rest[std::size_t(arm + 2)];
        const glm::vec3 down = glm::normalize(glm::vec3(sign * 0.22f, -1.0f, 0.0f));
        const glm::mat4 correction = glm::mat4_cast(glm::normalize(
            glm::quat(1.0f + glm::dot(glm::normalize(b - a), down), glm::cross(glm::normalize(b - a), down))));
        rest[std::size_t(arm + 1)] = a + glm::vec3(correction * glm::vec4(b - a, 0.0f));
        rest[std::size_t(arm + 2)] = a + glm::vec3(correction * glm::vec4(c - a, 0.0f));
        for (int j = arm; j <= arm + 2; ++j) m_correction[std::size_t(j)] = correction;
    }
    // A stable forward knee plane is required by two-bone IK. The imported
    // nearly straight legs have opposite tiny bends, which otherwise let the
    // solver choose a half-turn around one thigh even in the idle pose.
    for (int up : {kLUpLeg, kRUpLeg}) {
        const glm::vec3 a = rest[std::size_t(up)], b = rest[std::size_t(up + 1)], c = rest[std::size_t(up + 2)];
        const float t = glm::length(b - a) / (glm::length(b - a) + glm::length(c - b));
        glm::vec3 knee = glm::mix(a, c, t);
        knee.z += 0.018f;
        rest[std::size_t(up + 1)] = knee;
        const auto align = [](glm::vec3 from, glm::vec3 to) {
            from = glm::normalize(from);
            to = glm::normalize(to);
            return glm::mat4_cast(glm::normalize(glm::quat(1.0f + glm::dot(from, to), glm::cross(from, to))));
        };
        m_correction[std::size_t(up)] = align(b - a, knee - a);
        m_correction[std::size_t(up + 1)] = align(c - b, c - knee);
    }
    driver.setBodyBind(rest);
    for (glm::mat4& p : m_palette) p = glm::mat4(1.0f);
}

void Hero::update(Character& driver, const PoseInput& pose, float dt, float energy, int count, float flash) {
    if (!m_assets) return;
    m_assets->update();
    if (m_model->state() == assets::AssetState::Ready && m_version != m_model->version()) {
        m_version = m_model->version();
        for (const auto instance : m_instances) m_assets->scene().removeInstance(instance);
        m_instances.clear();
        const auto parts = m_model->parts();
        if (parts.size() != 2 || m_model->skins().size() != 1 || m_model->skins()[0].joints.size() != bones.size()) {
            log::error("hero", "Hero export does not match its rig metadata");
            return;
        }
        for (std::size_t i = 0; i < bones.size(); ++i)
            if (m_model->skins()[0].joints[i] != bones[i].name) {
                log::error("hero", "Hero joint order does not match its rig metadata");
                return;
            }
        for (std::size_t i = 0; i < parts.size(); ++i)
            m_instances.push_back(m_assets->scene().addInstance(
                {.mesh = parts[i].mesh, .material = i == 1 ? m_marks : parts[i].material}));
        log::info("hero", "Cloudsong hero: 68 bones, body and garment rig loaded");
    }
    m_world = driver.transform();
    if (!m_character && m_skeleton->data()) {
        m_character = m_animation->addCharacter(
            {.skeleton = m_skeleton, .clips = {m_clips}, .graph = m_graph, .transform = m_world, .height = 1.12f});
    }
    m_speed = approach(m_speed, pose.speed, 0.08f, dt);
    if (m_character) {
        m_animation->setTransform(m_character, m_world);
        m_animation->setFloat(m_character, "speed", m_speed);
        m_animation->setFloat(m_character, "cadence", glm::mix(0.95f, 2.05f, smoothstep(0.2f, 4.6f, m_speed)));
        m_animation->setBool(m_character, "grounded", pose.grounded);
        m_animation->update(dt);
    }
    m_stepped = false;
    if (pose.grounded && pose.speed > 0.2f)
        for (const auto& event : m_animation->events())
            if (event.name == "footstep") m_stepped = true;
    // The controller owns displacement. Clip root motion is removed by the
    // authoring step and deliberately not applied a second time here.
    m_special = approach(m_special,
                         std::max({pose.grounded && pose.jumpCrouch <= 0 && pose.landingCrouch <= 0 ? 0.0f : 1.0f,
                                   pose.glide, pose.levitate, pose.sit, pose.pray}),
                         0.12f, dt);

    // Reset physical cloth after teleports; ordinary movement keeps its inertia.
    const glm::vec3 position(m_world[3]);
    const bool resetMotion = !m_motionStarted || glm::distance(position, glm::vec3(m_previousWorld[3])) > 2.0f;
    m_motionStarted = true;
    m_previousWorld = m_world;
    m_time += dt;
    const float flap = pose.flap > 0.0f && pose.flap < 1.0f ? std::sin(kPi * pose.flap) : 0.0f;
    m_wings = approach(m_wings, std::max(pose.glide, flap), 0.12f, dt);
    m_swing = approach(m_swing, smoothstep(0.2f, 5.0f, pose.speed), 0.2f, dt);
    // Matrix palettes operate in normalized model space, independent of the
    // service's arbitrary joint-local axes. Fingers and toes inherit their parent.
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const Bone& b = bones[i];
        const auto found = std::find(bodyNames.begin(), bodyNames.end(), b.name);
        if (found != bodyNames.end()) {
            const std::size_t j = std::size_t(found - bodyNames.begin());
            m_palette[i] = driver.modelJoint(Joint(j)) * m_correction[j] * glm::translate(glm::mat4(1.0f), -b.position);
        } else {
            m_palette[i] = b.parent >= 0 ? m_palette[std::size_t(b.parent)] : glm::mat4(1.0f);
        }
    }
    if (animated() && m_model->state() == assets::AssetState::Ready) {
        const auto& skin = m_model->skins()[0];
        const auto& skeleton = *m_skeleton->data();
        const auto models = m_animation->modelMatrices(m_character);
        std::array<glm::mat4, 68> recorded, procedural, blended;
        for (std::size_t i = 0; i < bones.size(); ++i) {
            recorded[i] = models[std::size_t(skeleton.findJoint(bones[i].name))];
            procedural[i] = m_palette[i] * glm::inverse(skin.inverseBindMatrices[i]);
        }
        // Blend joint-local rotations, not skin matrices: limb lengths survive
        // transitions between recorded ground motion and the gameplay flight pose.
        for (std::size_t i = 0; i < bones.size(); ++i) {
            const int parent = bones[i].parent;
            const glm::mat4 a = parent < 0 ? recorded[i] : glm::inverse(recorded[std::size_t(parent)]) * recorded[i];
            const glm::mat4 b =
                parent < 0 ? procedural[i] : glm::inverse(procedural[std::size_t(parent)]) * procedural[i];
            glm::mat4 local = glm::mat4_cast(
                glm::slerp(glm::normalize(glm::quat_cast(a)), glm::normalize(glm::quat_cast(b)), m_special));
            local[3] = glm::mix(a[3], b[3], m_special);
            blended[i] = parent < 0 ? local : blended[std::size_t(parent)] * local;
            m_palette[i] = blended[i] * skin.inverseBindMatrices[i];
        }
    }
    // The source walk crosses its feet and its jog barely clears the ground.
    // Keep the recorded upper-body rhythm, but author the support, toe-off and
    // return arcs explicitly. They use the same normalized phase as the jog.
    const float running = smoothstep(1.8f, 4.3f, m_speed);
    const float moving = smoothstep(0.10f, 0.85f, m_speed);
    m_gaitWeight = approach(m_gaitWeight, pose.grounded ? moving : 0.0f, 0.08f, dt);
    // Plant the soles while crouching, then push through the balls of the feet
    // during extension. The falling half of jumpCrouch identifies that phase.
    if (resetMotion || pose.sit >= 0.1f) {
        m_jumpHeel = 0.0f;
    } else if (pose.grounded && pose.jumpCrouch > 0.0f) {
        if (pose.jumpCrouch < m_previousJumpCrouch - 0.001f || m_jumpHeel > 0.001f)
            m_jumpHeel = 0.48f * (1.0f - moving) * smoothstep(0.0f, 0.8f, 1.0f - pose.jumpCrouch);
    } else {
        m_jumpHeel *= std::exp(-18.0f * dt);
    }
    m_previousJumpCrouch = pose.jumpCrouch;
    if (m_character) {
        const auto state = m_animation->state(m_character);
        if (state.layerState == "move")
            m_gaitPhase = state.normalizedTime;
        else if (pose.grounded && moving > 0.0f)
            m_gaitPhase += dt * glm::mix(1.1f, 2.37f, running);
    }
    std::array<float, 2> solePitch{}, support{};
    m_footClearance = m_toeFlex = 0.0f;
    // The forward pole removes only retargeting twist, not the knee's lift.
    if (pose.sit < 0.1f && pose.glide < 0.1f) {
        std::array<glm::vec3, 2> targets{};
        const float standing = 1.0f - moving;
        for (int leg = 0; leg < 2; ++leg) {
            const auto foot = std::size_t(bone(leg == 0 ? "LeftFoot" : "RightFoot"));
            auto& target = targets[std::size_t(leg)];
            target = glm::vec3(m_palette[foot] * glm::vec4(bones[foot].position, 1));
            // Preserve modest natural side motion, but never let feet cross.
            const float side = leg == 0 ? 1.0f : -1.0f;
            target.x = side * std::clamp(side * target.x, 0.055f, 0.095f);
            if (pose.grounded) {
                float phase = m_gaitPhase - 0.23f + (leg == 0 ? 0.0f : 0.5f);
                phase -= std::floor(phase);
                const float stance = glm::mix(0.59f, 0.34f, running);
                const float reach = glm::mix(0.15f, 0.24f, running);
                float z, lift, pitch;
                if (phase < stance) {
                    const float t = phase / stance;
                    z = glm::mix(reach, -reach, t);
                    const float heel = smoothstep(0.55f, 1.0f, t);
                    pitch = -0.16f * (1.0f - smoothstep(0.0f, 0.22f, t)) + 0.58f * heel;
                    // Rotate the ankle around the planted ball during push-off.
                    lift = 0.055f * std::sin(std::max(pitch, 0.0f));
                    support[std::size_t(leg)] = 1.0f;
                } else {
                    const float t = (phase - stance) / (1.0f - stance);
                    z = glm::mix(-reach, reach, smooth01(t));
                    // A clear return arc lifts the knee, with the heel folding
                    // behind it first and the shin extending before touchdown.
                    lift = glm::mix(0.055f, 0.18f, running) * std::pow(std::sin(kPi * t), 0.85f);
                    pitch = 0.58f * (1.0f - smoothstep(0.0f, 0.40f, t)) - 0.16f * smoothstep(0.62f, 1.0f, t);
                    support[std::size_t(leg)] = 1.0f - smoothstep(0.0f, 0.12f, t);
                }
                glm::vec3 gait = bones[foot].position;
                gait.z += z;
                gait.y += lift;
                target = glm::mix(bones[foot].position, gait, m_gaitWeight);
                solePitch[std::size_t(leg)] = pitch * m_gaitWeight + m_jumpHeel;
                support[std::size_t(leg)] = glm::mix(1.0f, support[std::size_t(leg)], m_gaitWeight);
            }
            float offset = 0;
            glm::vec3 normal(0, 1, 0);
            if (pose.grounded) {
                glm::vec3 from = glm::vec3(m_world * glm::vec4(target, 1));
                from.y = m_world[3].y + 0.65f;
                if (const auto hit = m_ground(from, 0.95f)) {
                    offset = std::clamp(hit->position.y - m_world[3].y, -0.30f, 0.24f);
                    normal = glm::normalize(glm::transpose(glm::mat3(m_world)) * hit->normal);
                }
            }
            m_footOffset[std::size_t(leg)] = approach(m_footOffset[std::size_t(leg)], offset, 0.06f, dt);
            if (pose.grounded) target.y += m_footOffset[std::size_t(leg)];
            if (m_jumpHeel > 0.0f) {
                glm::quat frame = glm::normalize(glm::quat_cast(m_palette[foot]));
                if (pose.grounded) {
                    const glm::vec3 right = glm::normalize(glm::cross(normal, glm::vec3(0, 0, 1)));
                    frame = glm::quat_cast(glm::mat3(right, normal, glm::cross(right, normal)));
                }
                // Pivot at the ball, not the ankle: the planted toe does not slide.
                const glm::vec3 lever = bones[foot + 1].position - bones[foot].position;
                target += frame * (lever - glm::angleAxis(m_jumpHeel, glm::vec3(1, 0, 0)) * lever);
            }
        }
        glm::vec3 balance(0);
        if (pose.grounded) {
            balance.y = std::min({m_footOffset[0], m_footOffset[1], 0.0f}) - 0.025f * m_gaitWeight;
            if (const auto hit = m_ground(glm::vec3(m_world[3]) + glm::vec3(0, 0.65f, 0), 0.95f)) {
                const glm::vec3 normal = glm::transpose(glm::mat3(m_world)) * hit->normal;
                const float slope = smoothstep(0.10f, 0.6f, glm::length(glm::vec2(normal.x, normal.z)));
                balance += glm::vec3(normal.x * 0.10f, -0.025f, normal.z * 0.10f) * slope * standing;
            }
        }
        m_balance = approach(m_balance, balance, 0.08f, dt);
        const glm::vec3 compression(0, -0.105f * std::max(pose.jumpCrouch, pose.landingCrouch * 0.65f), 0);
        for (auto& matrix : m_palette) matrix[3] += glm::vec4(m_balance + compression, 0);
        for (int leg = 0; leg < 2; ++leg) {
            const auto hip = std::size_t(bone(leg == 0 ? "LeftUpLeg" : "RightUpLeg"));
            const auto knee = hip + 1, foot = hip + 2;
            const glm::vec3 h = glm::vec3(m_palette[hip] * glm::vec4(bones[hip].position, 1));
            const float upper = glm::distance(bones[hip].position, bones[knee].position);
            const float lower = glm::distance(bones[knee].position, bones[foot].position);
            const glm::vec3 delta = targets[std::size_t(leg)] - h;
            const glm::vec3 axis = glm::normalize(delta);
            const float distance =
                std::clamp(glm::length(delta), std::abs(upper - lower) + 0.001f, upper + lower - 0.001f);
            const float along = (upper * upper + distance * distance - lower * lower) / (2 * distance);
            const float bend = std::sqrt(std::max(upper * upper - along * along, 0.0f));
            const glm::vec3 pole = glm::normalize(glm::vec3(0, 0, 1) - axis * axis.z);
            const glm::vec3 k = h + axis * along + pole * bend;
            const glm::vec3 f = h + axis * distance;
            const auto segment = [&](std::size_t joint, glm::vec3 origin, glm::vec3 direction) {
                const glm::vec3 rest = glm::normalize(bones[joint + 1].position - bones[joint].position);
                const glm::vec3 goal = glm::normalize(direction);
                const glm::quat rotation =
                    glm::dot(rest, goal) < -0.999f
                        ? glm::angleAxis(kPi, glm::vec3(1, 0, 0))
                        : glm::normalize(glm::quat(1.0f + glm::dot(rest, goal), glm::cross(rest, goal)));
                return glm::translate(glm::mat4(1), origin) * glm::mat4_cast(rotation) *
                       glm::translate(glm::mat4(1), -bones[joint].position);
            };
            m_palette[hip] = segment(hip, h, k - h);
            m_palette[knee] = segment(knee, k, f - k);
            m_palette[foot][3] += glm::vec4(f - glm::vec3(m_palette[foot] * glm::vec4(bones[foot].position, 1)), 0);
        }
    }
    // The ankle rotates the heel; the ball flexes the forefoot independently.
    // Shaft weights remain on the shin, so neither joint folds the boot's calf.
    for (int leg = 0; leg < 2; ++leg) {
        const std::size_t foot = std::size_t(bone(leg == 0 ? "LeftFoot" : "RightFoot"));
        const std::size_t toe = foot + 1;
        const glm::vec3 ankle(m_palette[foot] * glm::vec4(bones[foot].position, 1));
        glm::quat sole = glm::normalize(glm::quat_cast(m_palette[foot]));
        glm::quat bend(1, 0, 0, 0);
        if (pose.grounded && pose.sit < 0.1f) {
            glm::vec3 normal(0, 1, 0);
            glm::vec3 from = glm::vec3(m_world * glm::vec4(ankle, 1));
            from.y = m_world[3].y + 0.65f;
            if (const auto hit = m_ground(from, 0.95f)) normal = glm::transpose(glm::mat3(m_world)) * hit->normal;
            normal = glm::normalize(normal);
            const glm::vec3 right = glm::normalize(glm::cross(normal, glm::vec3(0, 0, 1)));
            const glm::vec3 forward = glm::normalize(glm::cross(right, normal));
            const glm::quat slope = glm::quat_cast(glm::mat3(right, normal, forward));
            const float pitch = solePitch[std::size_t(leg)];
            sole = slope * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
            // The toe stays on the surface through the heel rise, then releases.
            const float flex = -std::max(pitch, 0.0f) * support[std::size_t(leg)];
            bend = glm::angleAxis(flex, glm::vec3(1, 0, 0));
        } else if (pose.sit < 0.1f) {
            sole *= glm::angleAxis(m_jumpHeel, glm::vec3(1, 0, 0));
        }
        m_toeBend[std::size_t(leg)] = glm::slerp(m_toeBend[std::size_t(leg)], bend, 1.0f - std::exp(-30.0f * dt));
        m_toeFlex =
            std::max(m_toeFlex, 2.0f * std::acos(std::clamp(std::abs(m_toeBend[std::size_t(leg)].w), 0.0f, 1.0f)));
        m_palette[foot] = glm::translate(glm::mat4(1), ankle) * glm::mat4_cast(sole) *
                          glm::translate(glm::mat4(1), -bones[foot].position);
        m_palette[toe] = m_palette[foot] * glm::translate(glm::mat4(1), bones[toe].position) *
                         glm::mat4_cast(m_toeBend[std::size_t(leg)]) *
                         glm::translate(glm::mat4(1), -bones[toe].position);
    }
    updateGarments(pose, dt, resetMotion);
    m_toeSlopeError = m_groundSlope = 0.0f;
    if (pose.grounded)
        for (const auto name : {"LeftFoot", "RightFoot"}) {
            const std::size_t i = std::size_t(bone(name));
            const glm::mat4 foot = m_world * m_palette[i];
            glm::vec3 from = glm::vec3(foot * glm::vec4(bones[i].position, 1));
            const float ankleHeight = from.y;
            from.y = m_world[3].y + 0.65f;
            if (const auto hit = m_ground(from, 0.95f)) {
                m_footClearance = std::max(m_footClearance, ankleHeight - hit->position.y - bones[i].position.y);
                const glm::vec3 up = glm::normalize(glm::mat3(m_world * m_palette[i + 1]) * glm::vec3(0, 1, 0));
                m_toeSlopeError =
                    std::max(m_toeSlopeError, std::acos(std::clamp(glm::dot(up, hit->normal), -1.0f, 1.0f)));
                m_groundSlope = std::max(m_groundSlope, std::acos(std::clamp(hit->normal.y, -1.0f, 1.0f)));
            }
        }
    if (m_bindPose)
        for (glm::mat4& p : m_palette) p = glm::mat4(1.0f);
    for (const auto instance : m_instances) {
        m_assets->scene().setTransform(instance, m_world);
        m_assets->scene().setJointPalette(instance, m_palette);
        m_assets->scene().setInstanceColor(instance, glm::vec4(1.0f), {energy, float(count), flash, 0.0f});
    }
}

void Hero::updateGarments(const PoseInput& pose, float dt, bool reset) {
    // A small cloth cage drives the imported garment, retaining the actual
    // mesh and its sewn collar. The solver has its own fixed clock and a nearby
    // origin: differentiating absolute float positions at 2200 m fed rounding
    // noise back into velocity and made both garments shake even while resting.
    static const std::array<std::array<int, 3>, 4> chains = {{
        {bone("LeftCapeEdge1"), bone("LeftCapeEdge2"), bone("LeftCapeEdge3")},
        {bone("LeftCapeRear1"), bone("LeftCapeRear2"), bone("LeftCapeRear3")},
        {bone("RightCapeRear1"), bone("RightCapeRear2"), bone("RightCapeRear3")},
        {bone("RightCapeEdge1"), bone("RightCapeEdge2"), bone("RightCapeEdge3")},
    }};
    const glm::vec3 origin(m_world[3]);
    if (!reset) {
        const glm::vec3 shift = m_garmentOrigin - origin;
        for (auto& point : m_cloth) point += shift;
        for (auto& point : m_crown) point += shift;
        for (auto& point : m_garmentInput.target) point += shift;
        for (auto& point : m_garmentInput.body) point += shift;
        m_garmentInput.chest[3] += glm::vec4(shift, 0);
        m_garmentInput.head[3] += glm::vec4(shift, 0);
    }
    m_garmentOrigin = origin;
    glm::mat4 localWorld = m_world;
    localWorld[3] = glm::vec4(0, 0, 0, 1);
    GarmentInput input;
    input.chest = localWorld * m_palette[std::size_t(bone("Chest"))];
    input.head = localWorld * m_palette[std::size_t(bone("Head"))];
    input.wings = smoothstep(0.0f, 1.0f, m_wings);
    input.swing = m_swing;
    input.wind = pose.wind;
    const glm::mat4 toModel = glm::inverse(localWorld);
    std::array<glm::vec3, 16> rest{};
    for (int col = 0; col < 4; ++col) {
        const float side = col < 2 ? 1.0f : -1.0f;
        const bool edge = col == 0 || col == 3;
        for (int row = 0; row < 4; ++row) {
            const auto index = std::size_t(row * 4 + col);
            const glm::vec3 root = bones[std::size_t(chains[std::size_t(col)][0])].position;
            const glm::vec3 end = bones[std::size_t(chains[std::size_t(col)][2])].position;
            rest[index] = row < 3 ? bones[std::size_t(chains[std::size_t(col)][std::size_t(row)])].position
                                  : end + (end - root) * 0.18f;
            const float v = float(row) / 3.0f;
            const float angle = m_swing * 0.52f * v;
            const glm::vec3 loose = glm::vec3(turn(root, angle, {1, 0, 0}) * glm::vec4(rest[index], 1));
            const glm::vec3 direction = glm::normalize(edge ? glm::vec3(side * 0.88f, -0.42f, -0.22f)
                                                            : glm::vec3(side * 0.30f, -0.86f, -0.42f));
            const glm::vec3 spread = root + direction * glm::distance(rest[index], root);
            input.target[index] = glm::vec3(input.chest * glm::vec4(glm::mix(loose, spread, input.wings), 1));
        }
    }
    std::array<glm::vec3, 4> crownRest{};
    for (int row = 0; row < 3; ++row) crownRest[std::size_t(row)] = bones[std::size_t(bone("Hood1") + row)].position;
    crownRest[3] = {0.022f, 1.085f, -0.029f};
    const auto bodyPoint = [&](std::string_view name, glm::vec3 offset) {
        const auto joint = std::size_t(bone(name));
        return glm::vec3(localWorld * m_palette[joint] * glm::vec4(bones[joint].position + offset, 1));
    };
    input.body = {{bodyPoint("Chest", {0, -0.04f, 0}), bodyPoint("Hips", {0, -0.025f, 0}),
                   bodyPoint("LeftUpLeg", {0, -0.11f, 0}), bodyPoint("RightUpLeg", {0, -0.11f, 0}),
                   bodyPoint("LeftArm", {0, -0.025f, 0}), bodyPoint("RightArm", {0, -0.025f, 0}),
                   bodyPoint("LeftForeArm", {0, -0.015f, 0}), bodyPoint("RightForeArm", {0, -0.015f, 0}),
                   bodyPoint("Head", {0, 0.025f, 0})}};
    constexpr std::array<float, 9> radii = {0.108f, 0.115f, 0.063f, 0.063f, 0.065f, 0.065f, 0.06f, 0.06f, 0.165f};
    if (reset) {
        m_garmentInput = input;
        m_garmentWind = input.wind;
        m_garmentRemainder = 0;
        m_garmentTime = 0;
        m_cloth = input.target;
        m_clothVelocity.fill(glm::vec3(0));
        const glm::mat4 fromChest = glm::inverse(input.chest);
        for (std::size_t i = 0; i < m_cloth.size(); ++i)
            m_clothLocal[i] = glm::vec3(fromChest * glm::vec4(m_cloth[i], 1));
        for (std::size_t i = 0; i < m_crown.size(); ++i) {
            m_crown[i] = glm::vec3(input.head * glm::vec4(crownRest[i], 1));
            m_crownLocal[i] = crownRest[i];
        }
        m_crownVelocity.fill(glm::vec3(0));
        m_clothPrevious = m_clothLocal;
        m_crownPrevious = m_crownLocal;
        m_clothSlowVelocity.fill(glm::vec3(0));
        m_crownSlowVelocity.fill(glm::vec3(0));
        m_capeMotionSpeed = m_hoodMotionSpeed = m_capeJitter = m_hoodJitter = 0;
    }
    const auto constrain = [](glm::vec3& a, glm::vec3& b, float distance, bool pinned, float stiffness) {
        const glm::vec3 delta = b - a;
        const float length = glm::length(delta);
        if (length < 0.00001f) return;
        const glm::vec3 correction = delta * ((length - distance) / length) * stiffness;
        if (pinned)
            b -= correction;
        else {
            a += correction * 0.5f;
            b -= correction * 0.5f;
        }
    };
    const auto blendFrame = [](const glm::mat4& a, const glm::mat4& b, float t) {
        glm::mat4 frame(
            glm::mat3_cast(glm::slerp(glm::normalize(glm::quat_cast(a)), glm::normalize(glm::quat_cast(b)), t)));
        frame[3] = glm::mix(a[3], b[3], t);
        return frame;
    };
    constexpr double tick = 1.0 / 120.0;
    constexpr float step = float(tick);
    const double frameTime = std::clamp(double(dt), 0.0, 0.05);
    double elapsed = -m_garmentRemainder;
    m_garmentRemainder += frameTime;
    if (m_garmentRemainder + 1e-8 >= tick) m_capeMotionSpeed = m_hoodMotionSpeed = m_capeJitter = m_hoodJitter = 0;
    while (m_garmentRemainder + 1e-8 >= tick) {
        elapsed += tick;
        const float t = frameTime > 0 ? float(std::clamp(elapsed / frameTime, 0.0, 1.0)) : 1.0f;
        const glm::mat4 chest = blendFrame(m_garmentInput.chest, input.chest, t);
        const glm::mat4 head = blendFrame(m_garmentInput.head, input.head, t);
        std::array<glm::vec3, 16> target{};
        std::array<glm::vec3, 9> body{};
        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] = glm::mix(m_garmentInput.target[i], input.target[i], t);
        for (std::size_t i = 0; i < body.size(); ++i) body[i] = glm::mix(m_garmentInput.body[i], input.body[i], t);
        const float wings = glm::mix(m_garmentInput.wings, input.wings, t);
        const float swing = glm::mix(m_garmentInput.swing, input.swing, t);
        m_garmentWind =
            glm::mix(m_garmentWind, glm::mix(m_garmentInput.wind, input.wind, t), 1.0f - std::exp(-step / 0.3f));
        m_garmentTime += tick;
        const auto before = m_cloth;
        for (std::size_t i = 0; i < m_cloth.size(); ++i) {
            if (i < 4) {
                m_cloth[i] = target[i];
                continue;
            }
            const float v = float(i / 4) / 3.0f;
            // Resting cloth is driven by the slowly varying breeze and body,
            // not an always-on oscillation. Flutter belongs to moving airfoil tips.
            const float flutter = std::sin(float(m_garmentTime * 7.0) - v * 4.5f + float(i % 4) * 1.3f);
            const glm::vec3 air =
                m_garmentWind * 0.35f + glm::vec3(chest[2]) * (flutter * (swing * 0.45f + wings * 0.12f) * v);
            const glm::vec3 force = glm::vec3(0, -6.0f, 0) + (air - m_clothVelocity[i]) * glm::mix(1.4f, 2.0f, wings) +
                                    (target[i] - m_cloth[i]) * glm::mix(65.0f, 13.0f + 27.0f * wings, v);
            m_clothVelocity[i] += force * step;
            m_cloth[i] += m_clothVelocity[i] * step;
        }
        for (int iteration = 0; iteration < 6; ++iteration) {
            for (int row = 1; row < 4; ++row)
                for (int col = 0; col < 4; ++col) {
                    const auto i = std::size_t(row * 4 + col), up = i - 4;
                    constrain(m_cloth[up], m_cloth[i], glm::distance(rest[up], rest[i]), row == 1, 1.0f);
                    if (col < 3) {
                        const float span = glm::distance(target[i], target[i + 1]);
                        // Resist extension firmly and permit crosswise folds.
                        const float firmness = glm::distance(m_cloth[i], m_cloth[i + 1]) > span ? 0.85f : 0.12f;
                        constrain(m_cloth[i], m_cloth[i + 1], span, false, firmness);
                        constrain(m_cloth[up], m_cloth[i + 1], glm::distance(target[up], target[i + 1]), row == 1,
                                  0.10f);
                        constrain(m_cloth[up + 1], m_cloth[i], glm::distance(target[up + 1], target[i]), row == 1,
                                  0.10f);
                    }
                    if (row > 1)
                        constrain(m_cloth[i - 8], m_cloth[i], glm::distance(rest[i - 8], rest[i]), row == 2, 0.10f);
                }
            for (std::size_t i = 4; i < m_cloth.size(); ++i)
                for (std::size_t b = 0; b < body.size(); ++b) {
                    if (b == body.size() - 1 && i >= 8) continue;
                    const glm::vec3 delta = m_cloth[i] - body[b];
                    const float length = glm::length(delta);
                    if (length > 0.00001f && length < radii[b]) m_cloth[i] = body[b] + delta * (radii[b] / length);
                }
        }
        for (std::size_t i = 0; i < m_cloth.size(); ++i) {
            // Finite bending stiffness limits a fold before the sparsely sampled
            // cage can turn the front hem through itself in a fast glide.
            const glm::vec3 deviation = m_cloth[i] - target[i];
            const float limit = glm::mix(0.14f, 0.075f, wings) * float(i / 4) / 3.0f;
            if (glm::length(deviation) > limit && limit > 0) m_cloth[i] = target[i] + glm::normalize(deviation) * limit;
            m_clothVelocity[i] = (m_cloth[i] - before[i]) / step * std::exp(-3.2f * step);
        }

        // The crown is a light, tapered strip sewn onto the hood. Its three
        // sections bend independently, so the tip lags without rotating the face.
        const auto crownBefore = m_crown;
        m_crown[0] = glm::vec3(head * glm::vec4(crownRest[0], 1));
        for (std::size_t i = 1; i < m_crown.size(); ++i) {
            const float v = float(i) / 3.0f;
            glm::vec3 goal = crownRest[i];
            goal.z -= 0.008f * v * v;
            const glm::vec3 targetPoint = glm::vec3(head * glm::vec4(goal, 1));
            m_crownVelocity[i] += ((targetPoint - m_crown[i]) * glm::mix(155.0f, 85.0f, v) +
                                   (m_garmentWind * 0.18f - m_crownVelocity[i]) * 4.0f + glm::vec3(0, -1.8f, 0)) *
                                  step;
            m_crown[i] += m_crownVelocity[i] * step;
        }
        for (int iteration = 0; iteration < 5; ++iteration)
            for (std::size_t i = 1; i < m_crown.size(); ++i) {
                constrain(m_crown[i - 1], m_crown[i], glm::distance(crownRest[i - 1], crownRest[i]), i == 1, 1.0f);
                // A soft bend limit prevents inversion into the head on a sudden stop.
                const glm::vec3 goal = glm::vec3(head * glm::vec4(crownRest[i], 1));
                const glm::vec3 deviation = m_crown[i] - goal;
                const float limit = 0.012f + float(i) * 0.010f;
                if (glm::length(deviation) > limit) m_crown[i] = goal + glm::normalize(deviation) * limit;
            }
        for (std::size_t i = 0; i < m_crown.size(); ++i)
            m_crownVelocity[i] = (m_crown[i] - crownBefore[i]) / step * std::exp(-4.0f * step);

        // Keep interpolation and diagnostics in each garment's sewn frame. A
        // render frame without a solver tick still follows the body exactly.
        const auto localize = [&](const auto& points, auto& local, auto& previous, auto& slowVelocity,
                                  const glm::mat4& frame, float& motion, float& jitter) {
            previous = local;
            const glm::mat4 inverse = glm::inverse(frame);
            for (std::size_t i = 0; i < points.size(); ++i) {
                local[i] = glm::vec3(inverse * glm::vec4(points[i], 1));
                const glm::vec3 velocity = (local[i] - previous[i]) / step;
                slowVelocity[i] = glm::mix(slowVelocity[i], velocity, 1.0f - std::exp(-step / 0.08f));
                motion = std::max(motion, glm::length(velocity));
                jitter = std::max(jitter, glm::length(velocity - slowVelocity[i]));
            }
        };
        localize(m_cloth, m_clothLocal, m_clothPrevious, m_clothSlowVelocity, chest, m_capeMotionSpeed, m_capeJitter);
        localize(m_crown, m_crownLocal, m_crownPrevious, m_crownSlowVelocity, head, m_hoodMotionSpeed, m_hoodJitter);
        m_garmentRemainder = std::max(0.0, m_garmentRemainder - tick);
    }
    m_garmentInput = input;
    const float alpha = float(std::clamp(m_garmentRemainder / tick, 0.0, 1.0));
    std::array<glm::vec3, 16> cloth{};
    std::array<glm::vec3, 4> crown{};
    for (std::size_t i = 0; i < cloth.size(); ++i)
        cloth[i] = glm::vec3(input.chest * glm::vec4(glm::mix(m_clothPrevious[i], m_clothLocal[i], alpha), 1));
    for (std::size_t i = 0; i < crown.size(); ++i)
        crown[i] = glm::vec3(input.head * glm::vec4(glm::mix(m_crownPrevious[i], m_crownLocal[i], alpha), 1));
    const auto basis = [](glm::vec3 down, glm::vec3 across) {
        const glm::vec3 y = glm::normalize(-down);
        glm::vec3 z = glm::cross(across, y);
        if (glm::length(z) < 0.00001f) z = glm::cross(glm::vec3(1, 0, 0), y);
        z = glm::normalize(z);
        return glm::mat3(glm::cross(y, z), y, z);
    };
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 3; ++row) {
            const auto i = std::size_t(row * 4 + col);
            const auto left = std::size_t(row * 4 + std::max(col - 1, 0));
            const auto right = std::size_t(row * 4 + std::min(col + 1, 3));
            const glm::mat3 restBasis = basis(rest[i + 4] - rest[i], rest[right] - rest[left]);
            const glm::mat3 nowBasis = basis(cloth[i + 4] - cloth[i], cloth[right] - cloth[left]);
            const glm::mat3 targetBasis =
                basis(input.target[i + 4] - input.target[i], input.target[right] - input.target[left]);
            glm::quat deviation = glm::normalize(glm::quat_cast(nowBasis * glm::transpose(targetBasis)));
            if (deviation.w < 0) deviation = -deviation;
            const float angle = 2.0f * std::acos(std::clamp(deviation.w, 0.0f, 1.0f));
            if (angle > 0.45f) deviation = glm::slerp(glm::quat(1, 0, 0, 0), deviation, 0.45f / angle);
            glm::mat4 transform(glm::mat3_cast(deviation) * targetBasis * glm::transpose(restBasis));
            transform[3] = glm::vec4(cloth[i], 1);
            const auto joint = std::size_t(chains[std::size_t(col)][std::size_t(row)]);
            m_palette[joint] = toModel * transform * glm::translate(glm::mat4(1), -bones[joint].position);
        }
    for (std::size_t row = 0; row < 3; ++row) {
        const glm::mat3 restBasis = basis(crownRest[row + 1] - crownRest[row], {1, 0, 0});
        const glm::mat3 nowBasis = basis(crown[row + 1] - crown[row], glm::vec3(input.head[0]));
        glm::mat4 transform(nowBasis * glm::transpose(restBasis));
        transform[3] = glm::vec4(crown[row], 1);
        const auto joint = std::size_t(bone("Hood1")) + row;
        m_palette[joint] = toModel * transform * glm::translate(glm::mat4(1), -crownRest[row]);
    }
}

std::pair<glm::vec3, glm::vec3> Hero::anchor(int index) const {
    const auto& a = hero_data::anchors[std::size_t(index)];
    glm::mat4 skin(0.0f);
    for (int i = 0; i < 4; ++i) skin += m_palette[std::size_t(a.joints[std::size_t(i)])] * a.weights[i];
    const glm::mat4 world = m_world * skin;
    return {glm::vec3(world * glm::vec4(a.position, 1.0f)), glm::normalize(glm::mat3(world) * a.normal)};
}
std::pair<glm::vec3, glm::vec3> Hero::mark(int index) const { return anchor(index); }
glm::vec3 Hero::trail(int index) const { return anchor(8 + index).first; }
bool Hero::animated() const { return m_character && m_animation && m_animation->state(m_character).bound; }
bool Hero::ready() const {
    if (m_instances.empty() || !animated()) return false;
    for (const auto& texture : m_model->textures())
        if (texture->state() != assets::AssetState::Ready) return false;
    return true;
}
float Hero::hoodBend() const {
    const glm::vec4 tip(0, 1.12f, 0, 1);
    return glm::length(glm::vec3((m_palette[std::size_t(bone("Hood3"))] - m_palette[std::size_t(bone("Head"))]) * tip));
}
float Hero::kneesForward() const {
    float forward = 1.0f;
    for (const std::string side : {"Left", "Right"}) {
        const auto position = [this](std::string_view name) {
            const std::size_t i = std::size_t(bone(name));
            return glm::vec3(m_palette[i] * glm::vec4(bones[i].position, 1));
        };
        const glm::vec3 hip = position(side + "UpLeg"), knee = position(side + "Leg"), ankle = position(side + "Foot");
        const glm::vec3 axis = glm::normalize(ankle - hip);
        const glm::vec3 bend = knee - hip - axis * glm::dot(knee - hip, axis);
        forward = std::min(forward, bend.z);
    }
    return forward;
}
float Hero::kneeBend() const {
    float bend = kPi;
    for (const std::string side : {"Left", "Right"}) {
        const auto position = [this](std::string_view name) {
            const auto i = std::size_t(bone(name));
            return glm::vec3(m_palette[i] * glm::vec4(bones[i].position, 1));
        };
        const glm::vec3 upper = position(side + "Leg") - position(side + "UpLeg");
        const glm::vec3 lower = position(side + "Foot") - position(side + "Leg");
        bend =
            std::min(bend, std::acos(std::clamp(glm::dot(glm::normalize(upper), glm::normalize(lower)), -1.0f, 1.0f)));
    }
    return bend;
}
float Hero::feetLateralError() const {
    float error = 0;
    for (const auto name : {"LeftFoot", "RightFoot"}) {
        const auto i = std::size_t(bone(name));
        const glm::vec3 position(m_palette[i] * glm::vec4(bones[i].position, 1));
        error = std::max(error, std::abs(position.x - bones[i].position.x));
    }
    return error;
}
float Hero::facingForward() const {
    const glm::vec3 forward(m_palette[std::size_t(bone("Head"))][2]);
    return forward.z / std::max(glm::length(glm::vec2(forward.x, forward.z)), 0.001f);
}
float Hero::jumpLegSeparation() const {
    const auto position = [this](std::string_view name) {
        const std::size_t i = std::size_t(bone(name));
        return glm::vec3(m_palette[i] * glm::vec4(bones[i].position, 1));
    };
    const glm::vec3 delta = position("LeftFoot") - position("RightFoot");
    return glm::length(glm::vec2(delta.y, delta.z));
}
float Hero::feetForward() const {
    float heading = 1.0f;
    for (const auto name : {"LeftFoot", "RightFoot"}) {
        // Isolate yaw from the foot's swing. Projecting its forward vector
        // exaggerates yaw when the heel rises and the toes point nearly down.
        const glm::quat q = glm::normalize(glm::quat_cast(m_palette[std::size_t(bone(name))]));
        const float length = std::sqrt(q.w * q.w + q.y * q.y);
        if (length > 0.05f) heading = std::min(heading, std::cos(2.0f * std::atan2(q.y, q.w)));
    }
    return heading;
}
bool Hero::validPose() const {
    if (!ready()) return false;
    for (const glm::mat4& m : m_palette)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!std::isfinite(m[c][r])) return false;
    for (int i = 0; i < int(hero_data::anchors.size()); ++i) {
        const glm::vec3 p = anchor(i).first;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            glm::distance(p, glm::vec3(m_world[3])) > 2.0f)
            return false;
    }
    return true;
}
void Hero::destroy() {
    if (m_animation) {
        if (m_character) m_animation->removeCharacter(m_character);
        m_character = {};
        m_skeleton = {};
        m_clips = {};
        m_graph = {};
        m_animation.reset();
    }
    if (m_assets) {
        for (const auto instance : m_instances) m_assets->scene().removeInstance(instance);
        m_instances.clear();
        m_assets->scene().removeMaterial(m_marks);
        m_model = {};
        m_assets.reset();
    }
}
} // namespace cloudsong
