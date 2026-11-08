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
    return json::write(v) + "\n";
}

} // namespace cloudsong
