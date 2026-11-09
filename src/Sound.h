#pragma once

// The game's sound, all made in code and played through the engine's mixer: a gentle score (a slow pad over four
// chords and pentatonic plucks with an echo, more voices with every spirit awake, a swell at the end) synthesized on
// the audio thread; one-shot sounds synthesized once at startup (the call's chime, a gathered light, a candle lit, a
// spirit waking, footsteps on grass and on stone, the cape's flap, a landing); and the wind through the engine's
// weather sounds, louder with the speed of a glide.

#include "World.h"

#include <quench/audio/AudioSystem.h>
#include <quench/audio/WeatherAudio.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace cloudsong {

class Score;

struct SoundFrame {
    glm::vec3 listener{0.0f}; // world
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    float wind = 0.0f;   // m/s of air past the listener
    int spirits = 0;     // awake
    float ending = 0.0f; // 0..1
};

class Sound {
public:
    void init(quench::audio::AudioSystem& audio);
    void shutdown();
    void update(const SoundFrame& frame, double dt);

    void footstep(Ground ground, float speed);
    void land(Ground ground, float impact);
    void jump();
    void flap();
    void call();
    void harpNote(int note, glm::vec3 at);
    void lightTaken(int count);
    void chimeTaken(int count);
    void candleLit(glm::vec3 at);
    void spiritWakes(glm::vec3 at);

    void setVolumes(float music, float effects);

private:
    using Data = std::shared_ptr<const quench::audio::SoundData>;
    void play(const Data& data, float volume, float pitch = 1.0f, float send = 0.25f,
              std::optional<glm::vec3> at = std::nullopt);

    quench::audio::AudioSystem* m_audio = nullptr;
    std::unique_ptr<quench::audio::WeatherAudio> m_wind;
    std::shared_ptr<Score> m_score;
    quench::audio::SoundHandle m_music;
    quench::audio::BusId m_effects{}, m_musicBus{}, m_reverb{};
    Data m_chime, m_pluck, m_candle, m_wake, m_flap, m_jump;
    std::array<Data, 4> m_grass, m_stone;
    Data m_landGrass, m_landStone;
    int m_step = 0;
};

} // namespace cloudsong
