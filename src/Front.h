#pragma once

// What stands between the player and the world: the title, the pause menu with the settings, the controls' hint, and
// the touch controls on phones.

#include "Settings.h"

#include <quench/app/Input.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

struct ImFont;

namespace cloudsong {

enum class MenuAction { None, Begin, Resume, Quit, SettingsChanged };

class Menus {
public:
    void setFont(ImFont* font) { m_font = font; }
    void setTouch(bool touch) { m_touch = touch; }
    // The title over the drifting view: Begin, Settings and (where an app may quit itself) Quit.
    MenuAction title(Settings& settings, float alpha, bool canQuit);
    MenuAction pause(Settings& settings, bool canQuit);
    // A line of text low on the screen (the controls, a few seconds after the start).
    void hint(const char* text, float alpha);
    void windProgress(int count, int total, float cooldown);
    std::optional<glm::vec2> settingsPoint(std::string_view name) const {
        const auto it = m_settingsPoints.find(name);
        return m_settings && it != m_settingsPoints.end() ? std::optional(it->second) : std::nullopt;
    }
    bool settingsOpen() const { return m_settings; }
    void openSettings(bool open) { m_settings = open; }

private:
    MenuAction settingsPanel(Settings& settings);
    float scale() const;
    bool button(const char* label, float width, float scale);

    std::map<std::string, glm::vec2, std::less<>> m_settingsPoints;
    ImFont* m_font = nullptr;
    bool m_touch = false;
    bool m_settings = false;
};

struct TouchState {
    glm::vec2 move{0.0f}; // -1..1, +y forward
    glm::vec2 look{0.0f}; // points dragged this frame
    bool jump = false;    // held
    bool call = false;    // pressed this frame
    bool pause = false;   // pressed this frame
};

// A stick where the left thumb lands, the camera under the right thumb, the jump (held: glide) and call buttons at
// the bottom right, the pause button at the top right. Positions in window points; safe: x, y, width, height.
class TouchControls {
public:
    TouchState update(const quench::app::Input& input, glm::vec2 size, glm::vec4 safe);
    void reset();
    void draw(float alpha) const;

private:
    struct Layout {
        glm::vec2 jump{0.0f}, call{0.0f}, pause{0.0f}, stickHome{0.0f};
        float jumpRadius = 0.0f, callRadius = 0.0f, pauseRadius = 0.0f, stickRadius = 0.0f;
    };
    static Layout layout(glm::vec2 size, glm::vec4 safe);

    Layout m_layout;
    std::optional<std::uint64_t> m_stick, m_look, m_jump;
    glm::vec2 m_stickOrigin{0.0f}, m_stickAt{0.0f};
    bool m_jumpDown = false;
    float m_callGlow = 0.0f;
};

} // namespace cloudsong
