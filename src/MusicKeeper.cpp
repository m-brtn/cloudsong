#include "MusicKeeper.h"
namespace cloudsong {
Result<void> MusicKeeper::create(quench::render::Scene& scene, quench::render::SurfaceId surface,
                                 const Landmarks& places) {
    CharacterLook look;
    look.cape = srgb(55, 137, 147);
    look.capeInner = srgb(39, 82, 100);
    look.hood = srgb(224, 214, 181);
    look.scale = 1.05f;
    look.capeLength = .8f;
    const auto mesh = Character::buildMesh(look);
    auto desc = mesh.mesh.desc("wind harp keeper");
    desc.skin = mesh.skin;
    desc.jointCount = kJointCount;
    auto id = scene.addMesh(desc);
    if (!id) return Error{id.error()};
    quench::render::MaterialDesc material{.surface = surface, .roughness = .85f, .doubleSided = true};
    material.params[0] = {2, 0, 0, 0};
    m_figure = Character(look);
    m_feet = places.musician;
    m_figure.setPlacement(m_feet + glm::vec3(0, kAltitude, 0), 1.25f);
    m_instance =
        scene.addInstance({.mesh = *id, .material = scene.addMaterial(material), .transform = m_figure.transform()});
    return {};
}
bool MusicKeeper::call(glm::vec3 localFeet) {
    if (!nearby(localFeet) || (m_age >= 0 && m_age < 4.5f)) return false;
    m_age = 0;
    m_note = -1;
    ++m_answers;
    return true;
}
int MusicKeeper::step(float dt) {
    if (m_age < 0) return -1;
    m_age += dt;
    const int note = int(m_age / .45f);
    if (note != m_note && note < 6) {
        m_note = note;
        return note;
    }
    return -1;
}
void MusicKeeper::draw(float dt, glm::vec3 player, quench::render::Scene& scene) {
    const glm::vec3 feet = m_feet + glm::vec3(0, kAltitude, 0);
    m_music = glm::mix(m_music, answering() ? 1.0f : 0.0f, 1.0f - std::exp(-dt * 5));
    PoseInput pose;
    pose.music = m_music;
    if (glm::distance(feet, player) < 6 && !answering())
        pose.lookAt = player + glm::vec3(0, .85f, 0);
    else
        pose.lookAt = feet + glm::vec3(.8f, .9f, .2f);
    m_figure.update(dt, pose, [&](glm::vec3) { return std::optional<float>(feet.y); });
    scene.setJointPalette(m_instance, m_figure.palette());
}
} // namespace cloudsong
