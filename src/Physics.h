#pragma once

// Ray queries against the static world (the isles, the ruins).

#include <Jolt/Physics/Body/BodyLock.h>
#include <quench/animation/AnimationSystem.h>
#include <quench/physics/PhysicsWorld.h>

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <glm/glm.hpp>

#include <optional>

namespace cloudsong {

inline constexpr JPH::ObjectLayer kMovingLayer = quench::PhysicsLayers::kMoving;

// The fraction of the way from `from` to `to` where the ray first hits the world, or nothing.
inline std::optional<float> castRay(const quench::PhysicsWorld& physics, glm::vec3 from, glm::vec3 to) {
    const JPH::RRayCast ray{JPH::RVec3(from.x, from.y, from.z), JPH::Vec3(to.x - from.x, to.y - from.y, to.z - from.z)};
    JPH::RayCastResult hit;
    const JPH::PhysicsSystem& system = physics.system();
    if (!system.GetNarrowPhaseQuery().CastRay(ray, hit, system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                                              system.GetDefaultLayerFilter(kMovingLayer)))
        return std::nullopt;
    return hit.mFraction;
}

// The height of the ground below a point within `reach` metres, or nothing.
inline std::optional<float> groundBelow(const quench::PhysicsWorld& physics, glm::vec3 from, float reach) {
    const std::optional<float> f = castRay(physics, from, from - glm::vec3(0.0f, reach, 0.0f));
    if (!f) return std::nullopt;
    return from.y - *f * reach;
}

// Includes the triangle's normal, so boots can follow slopes as well as height.
// Called between physics steps, including from the animation job.
inline std::optional<quench::animation::GroundHit> footGround(const quench::PhysicsWorld& physics,
                                                              const glm::vec3& from, float reach) {
    const JPH::RRayCast ray{JPH::RVec3(from.x, from.y, from.z), JPH::Vec3(0, -reach, 0)};
    JPH::RayCastResult hit;
    const auto& system = physics.system();
    if (!system.GetNarrowPhaseQuery().CastRay(ray, hit, system.GetDefaultBroadPhaseLayerFilter(kMovingLayer),
                                              system.GetDefaultLayerFilter(kMovingLayer)))
        return std::nullopt;
    const JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (!lock.Succeeded()) return std::nullopt;
    const auto point = ray.GetPointOnRay(hit.mFraction);
    const auto normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
    return quench::animation::GroundHit{{float(point.GetX()), float(point.GetY()), float(point.GetZ())},
                                        {normal.GetX(), normal.GetY(), normal.GetZ()}};
}

} // namespace cloudsong
