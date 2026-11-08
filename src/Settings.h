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
};

const char* qualityName(Quality quality);
// Phones start at Medium, other devices at High.
Settings defaultSettings(bool mobile);
// Missing or unreadable fields keep `defaults`.
Settings parseSettings(const std::string& text, const Settings& defaults);
std::string writeSettings(const Settings& settings);

} // namespace cloudsong
