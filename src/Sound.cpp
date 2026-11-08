#include "Sound.h"

#include <quench/core/Log.h>

#include <atomic>
#include <cmath>

namespace cloudsong {

namespace audio = quench::audio;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr float kTwoPi = 6.28318531f;

float midiHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

// A state-variable filter (Chamberlin): low, band and high pass from one input.
struct Svf {
    float low = 0.0f, band = 0.0f;
    float bandpass(float x, float hz, float q) {
        const float f = 2.0f * std::sin(kPi * std::min(hz, 12000.0f) / float(kRate));
        low += f * band;
        const float high = x - low - q * band;
        band += f * high;
        return band;
    }
    float lowpass(float x, float hz, float q) {
        bandpass(x, hz, q);
        return low;
    }
};

std::shared_ptr<const audio::SoundData> makeSound(float seconds, float peak, auto&& sample) {
    auto data = std::make_shared<audio::SoundData>();
    data->format = {.channels = 1, .sampleRate = kRate};
    data->samples.resize(std::size_t(seconds * float(kRate)));
    float top = 1e-6f;
    for (std::size_t i = 0; i < data->samples.size(); ++i) {
        data->samples[i] = sample(float(i) / float(kRate));
        top = std::max(top, std::abs(data->samples[i]));
    }
    // Normalized to the peak, with a short fade at the end.
    const std::size_t fade = std::min<std::size_t>(data->samples.size(), kRate / 50);
    for (std::size_t i = 0; i < data->samples.size(); ++i) {
        data->samples[i] *= peak / top;
        const std::size_t left = data->samples.size() - i;
        if (left < fade) data->samples[i] *= float(left) / float(fade);
    }
    return data;
}

float pluckTone(float t, float hz) {
    const float p = kTwoPi * hz * t;
    const float attack = std::min(1.0f, t * 500.0f);
    return attack * (std::sin(p) * std::exp(-2.6f * t) + 0.3f * std::sin(2.0f * p) * std::exp(-6.0f * t) +
                     0.12f * std::sin(5.4f * p) * std::exp(-16.0f * t));
}

float bell(float t, float hz) {
    if (t < 0.0f) return 0.0f;
    static constexpr float kRatio[5] = {1.0f, 2.01f, 2.76f, 4.18f, 5.43f};
    static constexpr float kDecay[5] = {1.1f, 1.8f, 2.6f, 4.0f, 6.0f};
    static constexpr float kLevel[5] = {1.0f, 0.45f, 0.3f, 0.18f, 0.1f};
    float s = 0.0f;
    for (int k = 0; k < 5; ++k) s += kLevel[k] * std::sin(kTwoPi * hz * kRatio[k] * t) * std::exp(-kDecay[k] * t);
    return s * std::min(1.0f, t * 800.0f);
}

} // namespace

// The score: a pad over four chords, pentatonic plucks, a bass and a shimmer as the spirits wake, an echo. Runs on
// the audio thread: everything is allocated in the constructor.
class Score final : public audio::Generator {
public:
    std::atomic<float> intensity{0.0f}; // spirits awake, 0..3
    std::atomic<float> ending{0.0f};    // 0..1

    Score() : Generator({.channels = 2, .sampleRate = kRate}) {
        m_delayL.assign(kDelayFrames, 0.0f);
        m_delayR.assign(kDelayFrames, 0.0f);
    }

    std::uint32_t generate(float* out, std::uint32_t frames) override {
        const float level = intensity.load(std::memory_order_relaxed);
        const float end = ending.load(std::memory_order_relaxed);
        const float dt = 1.0f / float(kRate);
        for (std::uint32_t n = 0; n < frames; ++n) {
            m_time += dt;
            // The chords: 8 s each, crossfading over 2 s.
            const float bar = m_time / kChordSeconds;
            const int chord = int(bar) % 4;
            const float into = (bar - std::floor(bar)) * kChordSeconds;
            const float fadeIn = std::min(1.0f, into / 2.0f);
            float l = 0.0f, r = 0.0f;
            const float breathe = 0.8f + 0.2f * std::sin(m_time * 0.6f);
            const float padLevel = 0.022f * breathe * (1.0f + 0.7f * end) * std::min(1.0f, m_time / 4.0f);
            for (int which = 0; which < 2; ++which) {
                const int c = which == 0 ? chord : (chord + 3) % 4;
                const float g = which == 0 ? std::sin(fadeIn * kPi * 0.5f) : std::cos(fadeIn * kPi * 0.5f);
                if (g < 1e-3f) continue;
                for (int k = 0; k < 4; ++k) {
                    for (int d = 0; d < 2; ++d) {
                        float& phase = m_pad[which == 0 ? c : c][k][d];
                        const float hz = midiHz(float(kChords[c][k])) * (d == 0 ? 0.9988f : 1.0012f);
                        phase += hz * dt;
                        phase -= std::floor(phase);
                        const float p = kTwoPi * phase;
                        const float v = (std::sin(p) + 0.28f * std::sin(2.0f * p)) * g * padLevel;
                        (d == 0 ? l : r) += v;
                    }
                }
            }
            // The bass from one spirit on.
            if (level > 0.5f) {
                m_bassPhase += midiHz(float(kChords[chord][0] - 12)) * dt;
                m_bassPhase -= std::floor(m_bassPhase);
                const float b = std::sin(kTwoPi * m_bassPhase) * 0.05f * std::min(1.0f, level - 0.5f) *
                                (0.6f + 0.4f * std::sin(fadeIn * kPi * 0.5f));
                l += b;
                r += b;
            }
            // Softened: brighter with every spirit and at the end.
            const float cutoff = 1400.0f + 600.0f * level + 2500.0f * end;
            l = m_padL.lowpass(l, cutoff, 1.2f);
            r = m_padR.lowpass(r, cutoff, 1.2f);

            // The plucks, on eighths at 72 beats a minute.
            if (m_time >= m_nextPluck) {
                m_nextPluck += kEighth;
                const float chance = 0.16f + 0.1f * level + 0.35f * end;
                if (random() < chance) {
                    m_note = std::clamp(m_note + int(random() * 5.0f) - 2, 0, int(std::size(kScale)) - 1);
                    const float amp = 0.6f + 0.4f * random();
                    const float pan = random() - 0.5f;
                    start(midiHz(float(kScale[m_note])), amp, pan);
                    if (level > 1.5f && random() < 0.4f) start(midiHz(float(kScale[m_note] + 12)), amp * 0.3f, -pan);
                }
            }
            for (Voice& v : m_voices) {
                if (v.amp <= 0.0f) continue;
                v.phase += v.hz * dt;
                v.phase -= std::floor(v.phase);
                v.age += dt;
                const float p = kTwoPi * v.phase;
                const float s =
                    std::min(1.0f, v.age * 400.0f) *
                    (std::sin(p) * std::exp(-2.2f * v.age) + 0.22f * std::sin(2.0f * p) * std::exp(-5.0f * v.age)) *
                    v.amp * 0.075f;
                l += s * (0.5f - 0.5f * v.pan);
                r += s * (0.5f + 0.5f * v.pan);
                if (v.age > 3.5f) v.amp = 0.0f;
            }

            // A ping-pong echo.
            const std::size_t at = m_delayAt;
            const float el = m_delayL[at], er = m_delayR[at];
            m_delayL[at] = r * 0.5f + er * 0.38f;
            m_delayR[at] = l * 0.5f + el * 0.38f;
            m_delayAt = (m_delayAt + 1) % kDelayFrames;
            out[2 * n] = std::tanh(l + el * 0.4f);
            out[2 * n + 1] = std::tanh(r + er * 0.4f);
        }
        return frames;
    }

private:
    struct Voice {
        float hz = 0.0f, phase = 0.0f, age = 0.0f, amp = 0.0f, pan = 0.0f;
    };
    static constexpr float kChordSeconds = 8.0f;
    static constexpr float kEighth = 60.0f / 72.0f / 2.0f;
    static constexpr std::size_t kDelayFrames = std::size_t(kRate * 0.625f);
    static constexpr int kChords[4][4] = {{50, 57, 64, 66}, {47, 54, 62, 69}, {43, 50, 59, 66}, {45, 52, 59, 62}};
    static constexpr int kScale[10] = {69, 71, 74, 76, 78, 81, 83, 86, 88, 90};

    float random() {
        m_seed ^= m_seed << 13;
        m_seed ^= m_seed >> 17;
        m_seed ^= m_seed << 5;
        return float(m_seed >> 8) * (1.0f / 16777216.0f);
    }
    void start(float hz, float amp, float pan) {
        Voice* slot = &m_voices[0];
        for (Voice& v : m_voices) {
            if (v.amp <= 0.0f) {
                slot = &v;
                break;
            }
            if (v.age > slot->age) slot = &v;
        }
        *slot = {.hz = hz, .phase = 0.0f, .age = 0.0f, .amp = amp, .pan = pan};
    }

    float m_time = 0.0f;
    float m_pad[4][4][2]{};
    float m_bassPhase = 0.0f;
    Svf m_padL, m_padR;
    std::array<Voice, 12> m_voices{};
    float m_nextPluck = 1.0f;
    int m_note = 4;
    std::uint32_t m_seed = 0x2545f491u;
    std::vector<float> m_delayL, m_delayR;
    std::size_t m_delayAt = 0;
};

void Sound::init(audio::AudioSystem& audio) {
    m_audio = &audio;
    m_effects = audio.bus("effects");
    m_musicBus = audio.bus("music");
    m_reverb = audio.findBus("reverb").value_or(audio::BusId{});

    Random random{811u};
    auto noise = [&random] { return random.uniform(-1.0f, 1.0f); };
    m_pluck = makeSound(2.4f, 0.8f, [](float t) { return pluckTone(t, midiHz(74.0f)); });
    // The call: three strikes rising (D, A, D).
    m_chime = makeSound(4.5f, 0.8f, [](float t) {
        return bell(t, midiHz(74.0f)) + 0.8f * bell(t - 0.16f, midiHz(81.0f)) + 0.7f * bell(t - 0.32f, midiHz(86.0f));
    });
    {
        Svf f;
        m_candle = makeSound(2.2f, 0.7f, [&](float t) {
            const float sweep = 250.0f + 1400.0f * std::exp(-std::pow((t - 0.12f) / 0.12f, 2.0f));
            const float whoosh = f.lowpass(noise(), sweep, 0.9f) * std::min(1.0f, t / 0.06f) * std::exp(-4.0f * t);
            return whoosh * 1.6f + 0.5f * bell(t - 0.05f, midiHz(62.0f)) + 0.3f * bell(t - 0.05f, midiHz(69.0f));
        });
    }
    m_wake = makeSound(3.4f, 0.75f, [](float t) {
        static constexpr int kNotes[6] = {62, 66, 69, 74, 78, 81};
        float s = 0.0f;
        for (int k = 0; k < 6; ++k)
            s += pluckTone(std::max(0.0f, t - 0.11f * float(k)), midiHz(float(kNotes[k]))) *
                 (t >= 0.11f * float(k) ? 1.0f : 0.0f);
        const float glide = 293.66f * std::exp2(std::min(t / 1.6f, 1.0f));
        const float swell = std::sin(kPi * std::min(t / 3.2f, 1.0f));
        s += 0.35f * std::sin(kTwoPi * glide * t) * swell * (0.8f + 0.2f * std::sin(kTwoPi * 6.0f * t));
        return s;
    });
    {
        Svf f;
        m_flap = makeSound(0.5f, 0.7f, [&](float t) {
            const float hz = 260.0f + 900.0f * std::sin(kPi * std::min(t / 0.4f, 1.0f));
            const float env = std::min(1.0f, t / 0.05f) * std::exp(-7.0f * t);
            return f.bandpass(noise(), hz, 0.7f) * env;
        });
    }
    {
        Svf f;
        m_jump = makeSound(0.25f, 0.5f, [&](float t) {
            const float env = std::min(1.0f, t / 0.02f) * std::exp(-14.0f * t);
            return f.bandpass(noise(), 700.0f + 1600.0f * t / 0.25f, 0.8f) * env;
        });
    }
    for (std::size_t k = 0; k < m_grass.size(); ++k) {
        Svf f, g;
        const float hz = 2200.0f + 500.0f * float(k);
        m_grass[k] = makeSound(0.16f, 0.6f, [&](float t) {
            const float crunch = std::exp(-55.0f * t) +
                                 0.6f * std::exp(-90.0f * std::max(0.0f, t - 0.035f)) * (t > 0.035f ? 1.0f : 0.0f);
            return f.bandpass(noise(), hz, 1.1f) * crunch +
                   0.4f * g.lowpass(noise(), 300.0f, 1.0f) * std::exp(-40.0f * t);
        });
        Svf h;
        const float thump = 95.0f + 12.0f * float(k);
        m_stone[k] = makeSound(0.14f, 0.6f, [&](float t) {
            const float click = h.bandpass(noise(), 3200.0f + 300.0f * float(k), 1.4f) * std::exp(-160.0f * t);
            return click * 0.8f + std::sin(kTwoPi * thump * t) * std::exp(-45.0f * t);
        });
    }
    {
        Svf f;
        m_landGrass = makeSound(0.35f, 0.7f, [&](float t) {
            return f.lowpass(noise(), 500.0f, 1.0f) * std::exp(-14.0f * t) +
                   std::sin(kTwoPi * 70.0f * t) * std::exp(-22.0f * t) * 0.6f;
        });
    }
    {
        Svf f;
        m_landStone = makeSound(0.3f, 0.7f, [&](float t) {
            return f.bandpass(noise(), 2800.0f, 1.2f) * std::exp(-90.0f * t) * 0.7f +
                   std::sin(kTwoPi * 82.0f * t) * std::exp(-28.0f * t);
        });
    }

    m_score = std::make_shared<Score>();
    audio::PlayParams music{.bus = m_musicBus, .volume = 0.9f, .fadeIn = 3.0f};
    music.sendBus = m_reverb;
    music.sendLevel = 0.3f;
    m_music = audio.playGenerator(m_score, music);

    // The wind only: the rain's and the thunder's sounds stay unloaded.
    audio::WeatherSoundDesc wind;
    for (std::size_t k = 0; k < 4; ++k) wind.sounds[k].clear();
    wind.thunder = {"", ""};
    wind.volume = 0.55f;
    wind.smoothing = 0.6f;
    m_wind = std::make_unique<audio::WeatherAudio>(audio, wind);
}

void Sound::shutdown() {
    m_wind.reset();
    if (m_audio) m_audio->stop(m_music);
    m_score.reset();
    m_audio = nullptr;
}

void Sound::setVolumes(float music, float effects) {
    if (!m_audio) return;
    m_audio->setBusVolume(m_musicBus, music);
    m_audio->setBusVolume(m_effects, effects);
}

void Sound::play(const Data& data, float volume, float pitch, float send, std::optional<glm::vec3> at) {
    if (!m_audio || !data) return;
    audio::PlayParams p{.bus = m_effects, .volume = volume, .pitch = pitch};
    if (at) {
        p.spatial = true;
        p.emitter.position = *at;
        p.emitter.minDistance = 4.0f;
        p.emitter.maxDistance = 60.0f;
    }
    p.sendBus = m_reverb;
    p.sendLevel = send;
    m_audio->play(data, p);
}

void Sound::update(const SoundFrame& frame, double dt) {
    if (!m_audio) return;
    m_audio->setListener({.position = frame.listener, .forward = frame.forward});
    if (m_score) {
        m_score->intensity.store(float(frame.spirits), std::memory_order_relaxed);
        m_score->ending.store(frame.ending, std::memory_order_relaxed);
    }
    if (m_wind) m_wind->update({.wind = frame.wind}, dt);
}

void Sound::footstep(Ground ground, float speed) {
    if (ground == Ground::None) return;
    const float pitch = 0.92f + 0.04f * float(m_step % 5);
    const float volume = std::clamp(speed / 4.6f, 0.35f, 1.0f);
    const std::size_t k = std::size_t(m_step++) % m_grass.size();
    if (ground == Ground::Stone)
        play(m_stone[k], 0.22f * volume, pitch, 0.15f);
    else
        play(m_grass[k], 0.16f * volume, pitch, 0.05f);
}

void Sound::land(Ground ground, float impact) {
    const float volume = std::clamp(impact / 8.0f, 0.25f, 1.0f);
    play(ground == Ground::Stone ? m_landStone : m_landGrass, 0.35f * volume, 1.0f, 0.1f);
}

void Sound::jump() { play(m_jump, 0.18f, 1.0f, 0.05f); }

void Sound::flap() { play(m_flap, 0.35f, 0.95f + 0.1f * float(m_step++ % 3) * 0.5f, 0.15f); }

void Sound::call() { play(m_chime, 0.45f, 1.0f, 0.6f); }

void Sound::lightTaken(int count) {
    // Up the pentatonic scale with every light gathered.
    static constexpr float kSteps[8] = {0.0f, 2.0f, 4.0f, 7.0f, 9.0f, 12.0f, 14.0f, 16.0f};
    const float step = kSteps[std::size_t(std::clamp(count - 1, 0, 7))];
    play(m_pluck, 0.4f, std::exp2(step / 12.0f), 0.5f);
    play(m_pluck, 0.25f, std::exp2((step + 7.0f) / 12.0f), 0.5f);
}

void Sound::candleLit(glm::vec3 at) { play(m_candle, 0.6f, 1.0f, 0.4f, at); }

void Sound::spiritWakes(glm::vec3 at) { play(m_wake, 0.6f, 1.0f, 0.6f, at); }

} // namespace cloudsong
