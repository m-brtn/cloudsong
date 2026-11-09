#pragma once

#include "Character.h"

#include <quench/animation/AnimationSystem.h>
#include <quench/render/SceneAssets.h>

namespace cloudsong {

// Recorded locomotion on the imported rig, with gameplay flight poses and
// independent garment motion. Spirits keep their smaller procedural skeleton.
class Hero {
public:
    void create(quench::render::Scene& scene, quench::assets::AssetManager& assets, quench::render::SurfaceId surface,
                Character& driver, quench::JobSystem& jobs, quench::animation::GroundQuery ground);
    void update(Character& driver, const PoseInput& pose, float dt, float energy, int count, float flash);
    void destroy();
    quench::render::SceneAssets& sceneAssets() { return *m_assets; }
    quench::animation::AnimationSystem& animationSystem() { return *m_animation; }
    bool ready() const;
    bool validPose() const;
    float feetForward() const;
    float facingForward() const;
    float jumpLegSeparation() const;
    float kneeBend() const;
    float footClearance() const { return m_footClearance; }
    float toeFlex() const { return m_toeFlex; }
    float feetLateralError() const;
    float kneesForward() const;
    bool animated() const;
    bool stepped() const { return m_stepped; }
    float hoodBend() const;
    // Cage motion in the sewn body's frame, in m/s; jitter removes slow drape.
    float capeMotionSpeed() const { return m_capeMotionSpeed; }
    float hoodMotionSpeed() const { return m_hoodMotionSpeed; }
    float capeJitter() const { return m_capeJitter; }
    float hoodJitter() const { return m_hoodJitter; }
    float toeSlopeError() const { return m_toeSlopeError; }
    float groundSlope() const { return m_groundSlope; }
    void setBindPose(bool bind) { m_bindPose = bind; }
    std::pair<glm::vec3, glm::vec3> mark(int index) const;
    glm::vec3 trail(int index) const;

private:
    std::pair<glm::vec3, glm::vec3> anchor(int index) const;
    void updateGarments(const PoseInput& pose, float dt, bool reset);
    std::unique_ptr<quench::render::SceneAssets> m_assets;
    quench::assets::AssetRef<quench::render::ModelAsset> m_model;
    std::unique_ptr<quench::animation::AnimationSystem> m_animation;
    quench::assets::AssetRef<quench::animation::SkeletonAsset> m_skeleton;
    quench::assets::AssetRef<quench::animation::ClipSetAsset> m_clips;
    quench::assets::AssetRef<quench::animation::GraphAsset> m_graph;
    quench::animation::CharacterId m_character;
    quench::animation::GroundQuery m_ground;
    float m_toeSlopeError = 0.0f, m_groundSlope = 0.0f;
    std::vector<quench::render::InstanceId> m_instances;
    quench::render::MaterialId m_marks;
    std::array<glm::mat4, 68> m_palette{};
    std::array<glm::mat4, kBodyJoints> m_correction{};
    glm::mat4 m_world{1.0f};
    glm::mat4 m_previousWorld{1.0f};
    glm::vec3 m_balance{0.0f};
    std::array<float, 2> m_footOffset{};
    float m_gaitPhase = 0.0f, m_gaitWeight = 0.0f;
    float m_previousJumpCrouch = 0.0f, m_jumpHeel = 0.0f;
    float m_footClearance = 0.0f, m_toeFlex = 0.0f;
    std::array<glm::quat, 2> m_toeBend{glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
    struct GarmentInput {
        glm::mat4 chest{1.0f}, head{1.0f};
        std::array<glm::vec3, 16> target{};
        std::array<glm::vec3, 9> body{};
        glm::vec3 wind{0.0f};
        float wings = 0.0f, swing = 0.0f;
    };
    // World-oriented, translation-relative coordinates retain inertia without
    // subtracting millimetre cloth displacements from the isles' 2200 m height.
    glm::vec3 m_garmentOrigin{0.0f}, m_garmentWind{0.0f};
    GarmentInput m_garmentInput;
    double m_garmentRemainder = 0.0, m_garmentTime = 0.0;
    std::array<glm::vec3, 16> m_cloth{}, m_clothVelocity{}, m_clothLocal{}, m_clothPrevious{}, m_clothSlowVelocity{};
    std::array<glm::vec3, 4> m_crown{}, m_crownVelocity{}, m_crownLocal{}, m_crownPrevious{}, m_crownSlowVelocity{};
    float m_capeMotionSpeed = 0.0f, m_hoodMotionSpeed = 0.0f, m_capeJitter = 0.0f, m_hoodJitter = 0.0f;
    float m_special = 0.0f, m_speed = 0.0f;
    bool m_motionStarted = false, m_stepped = false;
    bool m_bindPose = false;
    std::uint32_t m_version = 0;
    float m_time = 0.0f, m_wings = 0.0f, m_swing = 0.0f;
};

} // namespace cloudsong
