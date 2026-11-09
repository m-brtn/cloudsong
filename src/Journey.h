#pragma once

// The journey's state: the drifting lights gathered into the cape (each one more mark of flight, up to kMaxMarks),
// the flames that warm the cape back to full (the lanterns and the candles once lit), the candles lit by the call.
// It only keeps state and reports what happened in a step; the game turns that into pictures and sounds.

#include "World.h"

#include <vector>

namespace cloudsong {

class Player;

inline constexpr int kStartMarks = 2;
inline constexpr int kMaxMarks = 8;

struct JourneyEvents {
    int chimeTaken = -1;
    int lightTaken = -1; // the light gathered in this step
    int candleLit = -1;  // the candle the call lit in this step
};

class Journey {
public:
    void reset(const Landmarks& landmarks);
    // One fixed step. `feet`: the player's feet (local); `called`: the call was made in this step.
    JourneyEvents step(float dt, Player& player, glm::vec3 feet, bool called);

    std::size_t chimesTaken() const;
    bool windSong() const { return chimesTaken() >= 3; }
    float chimeAge(std::size_t i) const { return m_chimeAge[i]; }
    std::size_t lightsTaken() const;
    bool lightTaken(std::size_t light) const { return m_lightAge[light] >= 0.0f; }
    // Seconds since the light was taken (negative: not taken).
    float lightAge(std::size_t light) const { return m_lightAge[light]; }
    bool candleLit(std::size_t candle) const { return m_candleLit[candle]; }
    void lightCandle(std::size_t candle) { m_candleLit[candle] = true; }
    std::size_t candlesLit() const;
    bool warm() const { return m_warm; } // near a flame in the last step

    static constexpr float kWarmRadius = 4.5f; // m from a flame
    static constexpr float kCallReach = 4.0f;  // m from a candle the call lights it

private:
    const Landmarks* m_landmarks = nullptr;
    std::vector<float> m_lightAge, m_chimeAge;
    std::vector<bool> m_candleLit;
    bool m_warm = false;
};

} // namespace cloudsong
