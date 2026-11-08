#include "Player.h"

#include "Physics.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

namespace cloudsong {

namespace {

constexpr float kRadius = 0.22f;
constexpr float kHalfHeight = 0.34f;
constexpr float kGravity = 15.5f;
constexpr float kRunSpeed = 4.6f;
constexpr float kWalkSpeed = 1.9f;
constexpr float kJumpSpeed = 6.2f;
constexpr float kFlapSpeed = 7.4f;
constexpr float kGlideFall = 1.2f;
constexpr float kGlideSpeed = 7.6f;

JPH::Vec3 toJolt(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 toGlm(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

glm::vec2 approach2(glm::vec2 v, glm::vec2 target, float rate) {
    const glm::vec2 d = target - v;
    const float l = glm::length(d);
    return l <= rate ? target : v + d * (rate / l);
}

float turnToward(float yaw, float target, float rate) {
    const float d = wrapAngle(target - yaw);
    return wrapAngle(yaw + std::clamp(d, -rate, rate));
}

} // namespace

void Player::create(quench::PhysicsWorld& physics, glm::vec3 position, float yaw) {
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape =
        JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, kHalfHeight + kRadius, 0.0f), JPH::Quat::sIdentity(),
                                            new JPH::CapsuleShape(kHalfHeight, kRadius))
            .Create()
            .Get();
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(52.0f);
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -kRadius);
    settings->mCharacterPadding = 0.02f;
    settings->mPredictiveContactDistance = 0.1f;
    settings->mPenetrationRecoverySpeed = 1.0f;
    m_character = new JPH::CharacterVirtual(settings, JPH::RVec3(position.x, position.y, position.z),
                                            JPH::Quat::sIdentity(), 0, &physics.system());
    m_position = position;
    m_yaw = yaw;
}

void Player::destroy() { m_character = nullptr; }

void Player::teleport(glm::vec3 position, float yaw) {
    if (!m_character) return;
    m_character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    m_character->SetLinearVelocity(JPH::Vec3::sZero());
    m_position = position;
    m_velocity = glm::vec3(0.0f);
    m_yaw = yaw;
    m_glide = 0.0f;
    m_airTime = 0.0f;
}

void Player::step(float dt, const PlayerInput& in, quench::PhysicsWorld& physics) {
    m_flappedThisStep = m_jumpedThisStep = m_landedThisStep = false;
    if (!m_character) return;
    const glm::vec3 forward(std::sin(in.cameraYaw), 0.0f, std::cos(in.cameraYaw));
    const glm::vec3 right(-std::cos(in.cameraYaw), 0.0f, std::sin(in.cameraYaw));
    glm::vec3 wish = right * in.move.x + forward * in.move.y;
    const float amount = std::min(glm::length(wish), 1.0f);
    if (amount > 1e-3f) wish = glm::normalize(wish);

    m_character->UpdateGroundVelocity();
    const bool wasGrounded = m_grounded;
    const bool onGround = m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    glm::vec3 v = m_velocity;
    bool jump = false;
    if (onGround) {
        m_airTime = 0.0f;
        m_coyote = 0.12f;
        const float speed = (in.walk ? kWalkSpeed : kRunSpeed) * amount;
        const glm::vec2 h = approach2({v.x, v.z}, glm::vec2(wish.x, wish.z) * speed, 22.0f * dt);
        // No fall speed kept on the ground: it would eat the speed up a slope.
        v = glm::vec3(h.x, 0.0f, h.y);
        // A jump, or an updraft that takes the player up.
        if (in.jumpPressed || in.lift > 0.0f) jump = true;
        m_glide = approach(m_glide, 0.0f, 0.06f, dt);
        if (amount > 0.1f) m_yaw = turnToward(m_yaw, std::atan2(wish.x, wish.z), 11.0f * dt);
    } else {
        m_airTime += dt;
        m_coyote -= dt;
        if (in.jumpPressed && m_coyote > 0.0f && v.y <= 0.5f) {
            jump = true;
        } else if (in.jumpPressed && energy >= 1.0f) {
            energy -= 1.0f;
            v.y = std::max(v.y, 0.0f) * 0.3f + kFlapSpeed;
            m_flapTime = 0.0f;
            m_flappedThisStep = true;
        }
        // Gliding holds most of the fall: the sink settles a little above kGlideFall.
        v.y -= kGravity * dt * (1.0f - 0.9f * m_glide);
        const bool gliding = in.jumpHeld && v.y < 0.6f && m_flapTime > 0.32f && m_airTime > 0.18f;
        m_glide = approach(m_glide, gliding ? 1.0f : 0.0f, gliding ? 0.14f : 0.1f, dt);
        if (m_glide > 0.01f) {
            if (amount > 0.1f) m_yaw = turnToward(m_yaw, std::atan2(wish.x, wish.z), 2.4f * dt);
            if (v.y < -kGlideFall) v.y = glm::mix(v.y, approach(v.y, -kGlideFall, 0.2f, dt), m_glide);
            const glm::vec3 facing(std::sin(m_yaw), 0.0f, std::cos(m_yaw));
            const glm::vec2 target = glm::vec2(facing.x, facing.z) * kGlideSpeed * (0.75f + 0.25f * amount);
            const glm::vec2 h = approach2({v.x, v.z}, target, 7.0f * dt * m_glide + 2.0f * dt);
            v.x = h.x;
            v.z = h.y;
        } else {
            const float speed = (in.walk ? kWalkSpeed : kRunSpeed) * amount;
            const glm::vec2 current(v.x, v.z);
            const glm::vec2 target = glm::vec2(wish.x, wish.z) * std::max(speed, glm::length(current) * 0.98f);
            const glm::vec2 h = approach2(current, amount > 0.1f ? target : current * 0.995f, 7.0f * dt);
            v.x = h.x;
            v.z = h.y;
            if (amount > 0.1f) m_yaw = turnToward(m_yaw, std::atan2(wish.x, wish.z), 5.0f * dt);
        }
        if (in.lift > 0.0f) {
            // The rising air carries the weight.
            v.y = approach(v.y + kGravity * dt * (1.0f - 0.9f * m_glide), in.lift, glm::mix(0.6f, 0.3f, m_glide), dt);
            v.x += (in.pull.x - v.x * 0.8f) * dt;
            v.z += (in.pull.y - v.z * 0.8f) * dt;
        }
        v.y = std::max(v.y, -40.0f);
    }
    if (jump) {
        v.y = kJumpSpeed;
        m_coyote = 0.0f;
        m_jumpedThisStep = true;
    }
    m_flapTime += dt;

    m_character->SetLinearVelocity(toJolt(v));
    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = jump || v.y > 0.1f ? JPH::Vec3::sZero() : JPH::Vec3(0.0f, -0.45f, 0.0f);
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.42f, 0.0f);
    JPH::PhysicsSystem& system = physics.system();
    m_character->ExtendedUpdate(dt, JPH::Vec3(0.0f, -kGravity, 0.0f), update,
                                system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                                system.GetDefaultLayerFilter(kMovingLayer), {}, {}, physics.tempAllocator());
    const JPH::RVec3 p = m_character->GetPosition();
    m_position = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
    m_velocity = toGlm(m_character->GetLinearVelocity());
    m_grounded = m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    if (m_grounded && !wasGrounded && m_airTime > 0.15f) m_landedThisStep = true;
}

} // namespace cloudsong
