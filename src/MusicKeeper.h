#pragma once
#include "Character.h"
#include "World.h"

namespace cloudsong {
// A resident, separate from the three journey spirits. Answers a nearby call.
class MusicKeeper {
public:
    Result<void> create(quench::render::Scene& scene, quench::render::SurfaceId surface, const Landmarks& places);
    bool call(glm::vec3 localFeet);
    int step(float dt); // newly played note, or -1
    void draw(float dt, glm::vec3 playerWorld, quench::render::Scene& scene);
    bool nearby(glm::vec3 localFeet) const { return glm::distance(localFeet, m_feet) < 3.8f; }
    bool answering() const { return m_age >= 0 && m_age < 3.2f; }
    int answers() const { return m_answers; }
    glm::vec3 position() const { return m_feet; }

private:
    Character m_figure;
    quench::render::InstanceId m_instance;
    glm::vec3 m_feet{0};
    float m_age = -1, m_music = 0;
    int m_note = -1, m_answers = 0;
};
} // namespace cloudsong
