#pragma once
#include "World.h"
#include <quench/animation/AnimationSystem.h>
namespace cloudsong {
// A curious resident and two buoyant soap bubbles on the quiet meadow court.
class MeadowLife {
public:
    Result<void> create(quench::render::Scene& scene, quench::render::SurfaceId surface,
                        quench::render::SceneAssets& assets, quench::animation::AnimationSystem& animation,
                        const World& world);
    void step(float dt, glm::vec3 player, glm::vec3 velocity);
    void prepare(bool playing);
    void draw(float alpha);
    void destroy();
    bool ready() const;
    int state() const { return m_attention ? 1 : 0; }
    int pushes() const { return m_pushes; }
    float speed() const { return glm::length(m_velocity); }
    glm::vec3 position() const { return m_position; }
    float yaw() const { return m_yaw; }
    glm::vec3 bubble(int i) const { return m_bubbles[std::clamp(i, 0, 1)].position; }
    float bubbleSpeed() const;

private:
    quench::render::Scene* m_scene = nullptr;
    quench::render::SceneAssets* m_assets = nullptr;
    quench::animation::AnimationSystem* m_animation = nullptr;
    const World* m_world = nullptr;
    quench::assets::AssetRef<quench::render::ModelAsset> m_model;
    quench::assets::AssetRef<quench::animation::SkeletonAsset> m_skeleton;
    quench::assets::AssetRef<quench::animation::ClipSetAsset> m_clips;
    quench::assets::AssetRef<quench::animation::GraphAsset> m_graph;
    quench::animation::CharacterId m_character;
    quench::render::ModelInstanceId m_instance;
    std::array<quench::render::TrailId, 2> m_trails;
    struct Bubble {
        glm::vec3 position{0}, previous{0}, velocity{0};
        float radius = .5f, contact = 0;
        quench::render::InstanceId instance;
    };
    std::array<Bubble, 2> m_bubbles;
    glm::vec3 m_home{0}, m_position{0}, m_previous{0}, m_velocity{0}, m_goal{0};
    glm::mat4 m_normalize{1}, m_transform{1};
    float m_yaw = 0, m_previousYaw = 0, m_wait = 1, m_time = 0;
    bool m_attention = false, m_playing = true;
    int m_pushes = 0;
    Random m_random{93051};
};
} // namespace cloudsong
