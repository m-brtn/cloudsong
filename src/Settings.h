#pragma once

// The player's settings, kept in the user directory (settings.json).

#include <quench/core/Result.h>

#include <string>

namespace cloudsong {

enum class Quality { Low, Medium, High };

struct Settings {
    static constexpr int kVersion = 1;
    Quality quality = Quality::High;
    float music = 0.8f;   // 0..1
    float effects = 1.0f; // 0..1
    float lookSpeed = 1.0f;
    bool invertLook = false;
    float motionBlur = .25f;  // 0..1: camera exposure as a fraction of a 60 Hz frame
    float colourBoost = .12f; // 0..0.3: restrained vibrance, preserving neutral whites
    bool dayNightCycle = false;
    float timeOfDay = 11.5f;        // hours, frozen until the player enables the cycle
    float dayLengthMinutes = 20.0f; // real minutes per full day
};

const char* qualityName(Quality quality);
// Phones start at Medium, other devices at High.
Settings defaultSettings(bool mobile);
// Missing or unreadable fields keep `defaults`.
Settings parseSettings(const std::string& text, const Settings& defaults);
std::string writeSettings(const Settings& settings);

} // namespace cloudsong
