#include "Journey.h"

#include "Player.h"

#include <algorithm>

namespace cloudsong {

namespace {

constexpr float kTakeRadius = 1.4f;   // m from the chest a light is gathered
constexpr float kWarmRate = 2.5f;     // marks per second near a flame
constexpr float kRestRate = 0.1f;     // marks per second standing anywhere: never stranded
constexpr float kFlameHeight = 1.05f; // the flames stand about this high over the ground

float horizontal(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

} // namespace

void Journey::reset(const Landmarks& landmarks) {
    m_landmarks = &landmarks;
    m_lightAge.assign(landmarks.lights.size(), -1.0f);
    m_candleLit.assign(landmarks.candles.size(), false);
    m_warm = false;
}

std::size_t Journey::lightsTaken() const {
    return std::size_t(std::count_if(m_lightAge.begin(), m_lightAge.end(), [](float a) { return a >= 0.0f; }));
}

std::size_t Journey::candlesLit() const {
    return std::size_t(std::count(m_candleLit.begin(), m_candleLit.end(), true));
}

JourneyEvents Journey::step(float dt, Player& player, glm::vec3 feet, bool called) {
    JourneyEvents events;
    if (!m_landmarks) return events;
    const Landmarks& l = *m_landmarks;
    const glm::vec3 chest = feet + glm::vec3(0.0f, 0.75f, 0.0f);

    for (std::size_t i = 0; i < l.lights.size(); ++i) {
        if (m_lightAge[i] >= 0.0f) {
            m_lightAge[i] += dt;
            continue;
        }
        if (glm::distance(chest, l.lights[i]) > kTakeRadius) continue;
        m_lightAge[i] = 0.0f;
        player.maxEnergy = std::min(player.maxEnergy + 1, kMaxMarks);
        player.energy = float(player.maxEnergy);
        events.lightTaken = int(i);
    }

    if (called)
        for (std::size_t i = 0; i < l.candles.size(); ++i)
            if (!m_candleLit[i] && glm::distance(chest, l.candles[i]) < kCallReach) {
                m_candleLit[i] = true;
                events.candleLit = int(i);
            }

    auto near = [&](glm::vec3 flame) {
        return horizontal(feet, flame) < kWarmRadius && std::abs(feet.y - (flame.y - kFlameHeight)) < 3.0f;
    };
    m_warm = std::any_of(l.lanterns.begin(), l.lanterns.end(), near);
    for (std::size_t i = 0; i < l.candles.size() && !m_warm; ++i) m_warm = m_candleLit[i] && near(l.candles[i]);
    const float rate = m_warm ? kWarmRate : player.grounded() ? kRestRate : 0.0f;
    player.energy = std::min(float(player.maxEnergy), player.energy + rate * dt);
    return events;
}

} // namespace cloudsong
