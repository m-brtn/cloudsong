#include "src/Settings.h"

#include <cmath>
#include <iostream>

int main() {
    using namespace cloudsong;
    Settings original = defaultSettings(false);
    original.dayNightCycle = true;
    original.timeOfDay = 17.5f;
    original.dayLengthMinutes = 7.0f;
    original.music = 0.25f;
    original.motionBlur = 0;
    original.colourBoost = .2f;
    const Settings saved = parseSettings(writeSettings(original), defaultSettings(true));
    if (!saved.dayNightCycle || std::abs(saved.timeOfDay - 17.5f) > 0.001f || saved.dayLengthMinutes != 7.0f ||
        saved.music != 0.25f || saved.quality != Quality::High || saved.motionBlur != 0 || saved.colourBoost != .2f) {
        std::cerr << "Daylight and existing preferences did not survive saving\n";
        return 1;
    }
    const Settings old = parseSettings(R"({"version":1,"quality":"low","music":0.3})", defaultSettings(false));
    if (old.quality != Quality::Low || old.music != 0.3f || old.dayNightCycle || old.timeOfDay != 11.5f ||
        old.motionBlur != .25f || old.colourBoost != .12f) {
        std::cerr << "Existing settings lost their preferences or daylight defaults\n";
        return 1;
    }
    const Settings bad = parseSettings(R"({"version":1,"time_of_day":-20,"day_length_minutes":0})", original);
    if (bad.timeOfDay != 0.0f || bad.dayLengthMinutes < 1.0f) {
        std::cerr << "Out-of-range saved times can break the clock\n";
        return 1;
    }
    const auto picture = parseSettings(R"({"version":1,"motion_blur":-2,"colour_boost":9})", original);
    if (picture.motionBlur != 0 || picture.colourBoost != .3f) return 1;
    return 0;
}
