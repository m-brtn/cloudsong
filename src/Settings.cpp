#include "Settings.h"

#include <quench/core/Json.h>

#include <algorithm>

namespace cloudsong {

using namespace quench;

const char* qualityName(Quality quality) {
    switch (quality) {
    case Quality::Low: return "low";
    case Quality::Medium: return "medium";
    case Quality::High: return "high";
    }
    return "high";
}

Settings defaultSettings(bool mobile) {
    Settings s;
    s.quality = mobile ? Quality::Medium : Quality::High;
    return s;
}

Settings parseSettings(const std::string& text, const Settings& defaults) {
    Result<json::Value> parsed = json::parse(text);
    if (!parsed || !parsed->isObject() || (*parsed)["version"].numberOr(0.0) != Settings::kVersion) return defaults;
    const json::Value& v = *parsed;
    Settings s = defaults;
    const std::string quality = v["quality"].stringOr(qualityName(defaults.quality));
    s.quality = quality == "low" ? Quality::Low : quality == "medium" ? Quality::Medium : Quality::High;
    s.music = std::clamp(float(v["music"].numberOr(defaults.music)), 0.0f, 1.0f);
    s.effects = std::clamp(float(v["effects"].numberOr(defaults.effects)), 0.0f, 1.0f);
    s.lookSpeed = std::clamp(float(v["look_speed"].numberOr(defaults.lookSpeed)), 0.25f, 3.0f);
    s.invertLook = v["invert_look"].boolOr(defaults.invertLook);
    s.motionBlur = std::clamp(float(v["motion_blur"].numberOr(defaults.motionBlur)), 0.0f, 1.0f);
    s.colourBoost = std::clamp(float(v["colour_boost"].numberOr(defaults.colourBoost)), 0.0f, .3f);
    s.dayNightCycle = v["day_night_cycle"].boolOr(defaults.dayNightCycle);
    s.timeOfDay = std::clamp(float(v["time_of_day"].numberOr(defaults.timeOfDay)), 0.0f, 23.99f);
    s.dayLengthMinutes = std::clamp(float(v["day_length_minutes"].numberOr(defaults.dayLengthMinutes)), 1.0f, 60.0f);
    return s;
}

std::string writeSettings(const Settings& s) {
    json::Value v = json::Value::makeObject();
    v.object["version"] = json::Value::makeNumber(Settings::kVersion);
    v.object["quality"] = json::Value::makeString(qualityName(s.quality));
    v.object["music"] = json::Value::makeNumber(double(s.music));
    v.object["effects"] = json::Value::makeNumber(double(s.effects));
    v.object["look_speed"] = json::Value::makeNumber(double(s.lookSpeed));
    v.object["invert_look"] = json::Value::makeBool(s.invertLook);
    v.object["motion_blur"] = json::Value::makeNumber(double(s.motionBlur));
    v.object["colour_boost"] = json::Value::makeNumber(double(s.colourBoost));
    v.object["day_night_cycle"] = json::Value::makeBool(s.dayNightCycle);
    v.object["time_of_day"] = json::Value::makeNumber(double(s.timeOfDay));
    v.object["day_length_minutes"] = json::Value::makeNumber(double(s.dayLengthMinutes));
    return json::write(v) + "\n";
}

} // namespace cloudsong
