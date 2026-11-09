#include "Bird.h"
#include <quench/core/Log.h>
namespace cloudsong {
using namespace quench;
void Bird::create(render::SceneAssets& assets, animation::AnimationSystem& animation, glm::vec3 at) {
    m_assets = &assets;
    m_animation = &animation;
    m_model = assets.loadModel("bird.qmodel");
    m_skeleton = animation.loadSkeleton("bird.qskel");
    m_clips = animation.loadClips("bird.qanim");
    m_graph = animation.loadGraph("bird.graph.json");
    // The authored branch centre is y=-.8; a 20 cm scale fits the cage aperture.
    m_transform = glm::translate(glm::mat4(1), at + glm::vec3(0, kAltitude + .16f, 0)) *
                  glm::rotate(glm::mat4(1), kPi, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(.20f));
    m_instance = assets.addModel(m_model, m_transform);
}
void Bird::update(bool playing) {
    if (!m_animation) return;
    if (!m_character && m_skeleton->data())
        m_character = m_animation->addCharacter({.skeleton = m_skeleton,
                                                 .clips = {m_clips},
                                                 .graph = m_graph,
                                                 .transform = m_transform,
                                                 .events = false,
                                                 .height = .36f});
    if (!m_character) return;
    m_animation->setTimeScale(m_character, playing ? 1 : 0);
    if (!ready()) return;
    const auto models = m_animation->modelMatrices(m_character);
    for (std::size_t s = 0; s < m_model->skins().size(); ++s) {
        const auto& skin = m_model->skins()[s];
        std::vector<glm::mat4> palette(skin.joints.size(), glm::mat4(1));
        for (std::size_t j = 0; j < skin.joints.size(); ++j) {
            const auto& name = skin.joints[j];
            const int bone = m_skeleton->data()->findJoint(name);
            if (bone >= 0 && std::size_t(bone) < models.size())
                palette[j] = models[std::size_t(bone)] * skin.inverseBindMatrices[j];
        }
        m_assets->setJointPalette(m_instance, int(s), palette);
    }
}
bool Bird::ready() const {
    return m_character && m_model->state() == assets::AssetState::Ready && m_animation->state(m_character).bound;
}
float Bird::phase() const { return ready() ? m_animation->state(m_character).normalizedTime : 0; }
void Bird::destroy() {
    if (m_character) m_animation->removeCharacter(m_character);
    m_character = {};
    m_model = {};
    m_skeleton = {};
    m_clips = {};
    m_graph = {};
    m_assets = nullptr;
    m_animation = nullptr;
}
} // namespace cloudsong
