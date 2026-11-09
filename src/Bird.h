#pragma once
#include "World.h"
#include <quench/animation/AnimationSystem.h>

namespace cloudsong {
// A perched resident shares the hero's animation and scene-asset owners.
class Bird {
public:
    void create(quench::render::SceneAssets& assets, quench::animation::AnimationSystem& animation, glm::vec3 at);
    void update(bool playing);
    void destroy();
    bool ready() const;
    float phase() const;

private:
    quench::render::SceneAssets* m_assets = nullptr;
    quench::animation::AnimationSystem* m_animation = nullptr;
    quench::assets::AssetRef<quench::render::ModelAsset> m_model;
    quench::assets::AssetRef<quench::animation::SkeletonAsset> m_skeleton;
    quench::assets::AssetRef<quench::animation::ClipSetAsset> m_clips;
    quench::assets::AssetRef<quench::animation::GraphAsset> m_graph;
    quench::animation::CharacterId m_character;
    quench::render::ModelInstanceId m_instance;
    glm::mat4 m_transform{1};
};
} // namespace cloudsong
