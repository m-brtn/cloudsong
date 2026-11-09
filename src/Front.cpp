#include "Front.h"

#include <imgui.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cloudsong {

namespace {

constexpr ImVec4 kText{1.0f, 0.97f, 0.9f, 1.0f};
constexpr ImVec4 kShadow{0.22f, 0.15f, 0.12f, 0.5f};

// The title font at `points` on the screen, whatever the UI's DPI scale.
void pushFont(ImFont* font, float points) { ImGui::PushFont(font, points / ImGui::GetStyle().FontScaleDpi); }

// A line centred on the screen at the height y (points), with a soft shadow under the letters.
void centredText(ImFont* font, const char* text, float points, float y, float alpha) {
    pushFont(font, points);
    const ImVec2 extent = ImGui::CalcTextSize(text);
    const float x = (ImGui::GetMainViewport()->Size.x - extent.x) * 0.5f;
    const float off = std::max(1.0f, points * 0.04f);
    ImGui::SetCursorPos(ImVec2(x + off, y + off));
    ImGui::TextColored(ImVec4(kShadow.x, kShadow.y, kShadow.z, kShadow.w * alpha), "%s", text);
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::TextColored(ImVec4(kText.x, kText.y, kText.z, alpha), "%s", text);
    ImGui::PopFont();
}

void beginScreen(const char* name, bool inputs) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!inputs) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin(name, nullptr, flags);
}

void pushButtonStyle(float s) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.96f, 0.88f, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.93f, 0.78f, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.88f, 0.66f, 0.48f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.95f, 0.85f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.96f, 0.88f, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.93f, 0.78f, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 0.88f, 0.66f, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 0.86f, 0.6f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.95f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.9f, 0.66f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(1.0f, 0.9f, 0.7f, 0.9f));
}

void popButtonStyle() {
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(2);
}

} // namespace

float Menus::scale() const {
    // 1 at a 720-point-high window; never smaller than a finger needs on a phone.
    return std::max(ImGui::GetMainViewport()->Size.y / 720.0f, m_touch ? 0.95f : 0.6f);
}

bool Menus::button(const char* label, float width, float s) {
    const float x = (ImGui::GetMainViewport()->Size.x - width) * 0.5f;
    ImGui::SetCursorPosX(x);
    pushFont(m_font, 24.0f * s);
    const bool pressed = ImGui::Button(label, ImVec2(width, 54.0f * s));
    ImGui::PopFont();
    return pressed;
}

MenuAction Menus::title(Settings& settings, float alpha, bool canQuit) {
    if (alpha <= 0.0f) return MenuAction::None;
    const float s = scale();
    const float h = ImGui::GetMainViewport()->Size.y;
    MenuAction action = MenuAction::None;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    beginScreen("##title", alpha > 0.5f);
    if (m_settings) {
        action = settingsPanel(settings);
    } else {
        centredText(m_font, "Cloudsong", 96.0f * s, h * 0.14f, 1.0f);
        centredText(m_font, "a song above the clouds", 26.0f * s, h * 0.14f + 112.0f * s, 0.9f);
        pushButtonStyle(s);
        ImGui::SetCursorPosY(h * 0.56f);
        if (button("Begin", 260.0f * s, s)) action = MenuAction::Begin;
        ImGui::SetItemDefaultFocus();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
        if (button("Settings", 260.0f * s, s)) m_settings = true;
        if (canQuit) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
            if (button("Quit", 260.0f * s, s)) action = MenuAction::Quit;
        }
        popButtonStyle();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return action;
}

MenuAction Menus::pause(Settings& settings, bool canQuit) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                  IM_COL32(30, 22, 34, 90));
    const float s = scale();
    const float h = vp->Size.y;
    MenuAction action = MenuAction::None;
    beginScreen("##pause", true);
    if (m_settings) {
        action = settingsPanel(settings);
    } else {
        centredText(m_font, "Resting", 64.0f * s, h * 0.2f, 1.0f);
        pushButtonStyle(s);
        ImGui::SetCursorPosY(h * 0.45f);
        if (button("Resume", 260.0f * s, s)) action = MenuAction::Resume;
        ImGui::SetItemDefaultFocus();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
        if (button("Settings", 260.0f * s, s)) m_settings = true;
        if (canQuit) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
            if (button("Quit", 260.0f * s, s)) action = MenuAction::Quit;
        }
        popButtonStyle();
    }
    ImGui::End();
    return action;
}

MenuAction Menus::settingsPanel(Settings& settings) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = scale();
    const float width = std::min(520.0f * s, vp->Size.x * 0.9f);
    const float height = std::min(650.0f * s, vp->Size.y * 0.9f);
    const ImVec2 at((vp->Size.x - width) * 0.5f, std::max((vp->Size.y - height) * 0.62f, vp->Size.y * 0.07f));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(vp->Pos.x + at.x, vp->Pos.y + at.y),
                                              ImVec2(vp->Pos.x + at.x + width, vp->Pos.y + at.y + height),
                                              IM_COL32(38, 34, 46, 220), 16.0f * s);
    const float headerBottom = at.y + 76.0f * s;
    const float footerTop = at.y + height - 88.0f * s;
    for (const float y : {headerBottom, footerTop})
        ImGui::GetWindowDrawList()->AddLine(ImVec2(vp->Pos.x + at.x, vp->Pos.y + y),
                                            ImVec2(vp->Pos.x + at.x + width, vp->Pos.y + y),
                                            IM_COL32(245, 226, 192, 65), std::max(1.0f, s));
    m_settingsPoints.clear();
    auto remember = [&](const char* name) {
        if (!ImGui::IsItemVisible()) return;
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        m_settingsPoints[name] = {(lo.x + hi.x) * 0.5f - vp->Pos.x, (lo.y + hi.y) * 0.5f - vp->Pos.y};
    };
    MenuAction action = MenuAction::None;
    pushButtonStyle(s);
    ImGui::SetCursorPos(ImVec2(at.x + 28.0f * s, at.y + 22.0f * s));
    pushFont(m_font, 30.0f * s);
    ImGui::TextColored(kText, "Settings");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(at.x + 28.0f * s, headerBottom));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f, 0.86f, 0.6f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f, 0.9f, 0.7f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 0.95f, 0.8f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 5.0f * s);
    ImGui::BeginChild("Settings controls", ImVec2(width - 36.0f * s, footerTop - headerBottom));
    pushFont(m_font, 21.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * s, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 22.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f * s);
    ImGui::Dummy(ImVec2(0.0f, 12.0f * s));
    ImGui::TextColored(kText, "Picture");
    const float controlsWidth = ImGui::GetContentRegionAvail().x - 12.0f * s;
    const float third = (controlsWidth - 20.0f * s) / 3.0f;
    const Quality levels[] = {Quality::Low, Quality::Medium, Quality::High};
    const char* labels[] = {"Light", "Balanced", "Beautiful"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool on = settings.quality == levels[i];
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.86f, 0.6f, 0.45f));
        if (ImGui::Button(labels[i], ImVec2(third, 44.0f * s)) && !on) {
            settings.quality = levels[i];
            action = MenuAction::SettingsChanged;
        }
        if (on) ImGui::PopStyleColor();
    }
    auto slider = [&](const char* label, const char* id, float& value, float lo, float hi, const char* format) {
        ImGui::TextColored(kText, "%s", label);
        ImGui::SameLine(controlsWidth * 0.36f);
        ImGui::SetNextItemWidth(controlsWidth * 0.64f);
        if (ImGui::SliderFloat(id, &value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp))
            action = MenuAction::SettingsChanged;
    };
    ImGui::Dummy(ImVec2(0.0f, 2.0f * s));
    float blur = settings.motionBlur * 100.0f, colour = settings.colourBoost * 100.0f;
    slider("Motion blur", "##motion_blur", blur, 0.0f, 100.0f, "%.0f %%");
    slider("Colour boost", "##colour_boost", colour, 0.0f, 30.0f, "%.0f %%");
    settings.motionBlur = blur / 100.0f;
    settings.colourBoost = colour / 100.0f;
    float music = settings.music * 100.0f, effects = settings.effects * 100.0f;
    slider("Music", "##music", music, 0.0f, 100.0f, "%.0f %%");
    slider("Sounds", "##effects", effects, 0.0f, 100.0f, "%.0f %%");
    settings.music = music / 100.0f;
    settings.effects = effects / 100.0f;
    slider("Camera speed", "##look", settings.lookSpeed, 0.25f, 3.0f, "%.2f");
    ImGui::TextColored(kText, "Invert camera");
    ImGui::SameLine(controlsWidth * 0.36f);
    if (ImGui::Checkbox("##invert", &settings.invertLook)) action = MenuAction::SettingsChanged;
    ImGui::Separator();
    ImGui::TextColored(kText, "Day / night cycle");
    ImGui::SameLine(controlsWidth * 0.56f);
    if (ImGui::Checkbox("##day_cycle", &settings.dayNightCycle)) action = MenuAction::SettingsChanged;
    remember("day_cycle_toggle");
    slider("Time of day", "##day_hour", settings.timeOfDay, 0.0f, 23.99f, "%.2f h");
    slider("Day length", "##day_length", settings.dayLengthMinutes, 1.0f, 60.0f, "%.0f min");
    const char* times[] = {"Morning", "Noon", "Evening"};
    const float hours[] = {8.0f, 12.0f, 17.5f};
    const char* names[] = {"morning", "noon", "evening"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(times[i], ImVec2(third, 44.0f * s))) {
            settings.timeOfDay = hours[i];
            action = MenuAction::SettingsChanged;
        }
        remember(names[i]);
    }
    ImGui::Dummy(ImVec2(0.0f, 12.0f * s));
    ImGui::PopStyleVar(4);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::SetCursorPosY(footerTop + 22.0f * s);
    if (button("Back", 200.0f * s, s)) m_settings = false;
    remember("settings_back");
    popButtonStyle();
    return action;
}

void Menus::hint(const char* text, float alpha) {
    if (alpha <= 0.0f) return;
    const float s = scale();
    beginScreen("##hint", false);
    // Above the touch controls on phones.
    const float h = ImGui::GetMainViewport()->Size.y;
    centredText(m_font, text, 22.0f * s, m_touch ? h * 0.12f : h - 64.0f * s, alpha * 0.9f);
    ImGui::End();
}

void Menus::windProgress(int count, int total, float cooldown) {
    const float s = scale();
    beginScreen("##wind_progress", false);
    const auto vp = ImGui::GetMainViewport();
    // Keep the top right corner free for the touch pause button.
    const ImVec2 at(vp->Pos.x + 28 * s, vp->Pos.y + 26 * s);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(ImVec2(at.x + 9 * s, at.y + 12 * s), 8 * s, IM_COL32(247, 219, 154, 235), 32, 2 * s);
    draw->AddCircleFilled(ImVec2(at.x + 9 * s, at.y + 12 * s), 2 * s, IM_COL32(200, 237, 219, 235));
    pushFont(m_font, 21 * s);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 28 * s, at.y));
    ImGui::TextColored(ImVec4(1, .95f, .81f, .95f), "%d / %d", count, total);
    ImGui::PopFont();
    if (count >= 3) {
        const float ready = 1 - std::clamp(cooldown / 5.0f, 0.0f, 1.0f);
        draw->AddLine(ImVec2(at.x, at.y + 33 * s), ImVec2(at.x + 92 * s, at.y + 33 * s), IM_COL32(247, 219, 154, 65),
                      2 * s);
        draw->AddLine(ImVec2(at.x, at.y + 33 * s), ImVec2(at.x + 92 * s * ready, at.y + 33 * s),
                      IM_COL32(247, 219, 154, 230), 2 * s);
    }
    ImGui::End();
}

TouchControls::Layout TouchControls::layout(glm::vec2 size, glm::vec4 safe) {
    (void)size;
    const float right = safe.x + safe.z, bottom = safe.y + safe.w;
    Layout l;
    l.jumpRadius = 50.0f;
    l.callRadius = 36.0f;
    l.pauseRadius = 22.0f;
    l.stickRadius = 62.0f;
    l.jump = {right - 86.0f, bottom - 92.0f};
    l.call = {right - 192.0f, bottom - 62.0f};
    l.pause = {right - 34.0f, safe.y + 34.0f};
    l.stickHome = {safe.x + 120.0f, bottom - 110.0f};
    return l;
}

void TouchControls::reset() {
    m_stick.reset();
    m_look.reset();
    m_jump.reset();
    m_jumpDown = false;
}

TouchState TouchControls::update(const quench::app::Input& input, glm::vec2 size, glm::vec4 safe) {
    m_layout = layout(size, safe);
    const Layout& l = m_layout;
    TouchState out;
    m_callGlow = std::max(0.0f, m_callGlow - 0.05f);
    for (const quench::app::Finger& f : input.fingers()) {
        const glm::vec2 p = f.position;
        if (f.pressed) {
            if (glm::distance(p, l.pause) < l.pauseRadius * 1.6f) {
                out.pause = true;
            } else if (glm::distance(p, l.jump) < l.jumpRadius * 1.25f) {
                m_jump = f.id;
            } else if (glm::distance(p, l.call) < l.callRadius * 1.3f) {
                out.call = true;
                m_callGlow = 1.0f;
            } else if (p.x < size.x * 0.45f && !m_stick) {
                m_stick = f.id;
                m_stickOrigin = m_stickAt = p;
            } else if (!m_look) {
                m_look = f.id;
            }
        }
        if (m_stick && f.id == *m_stick) m_stickAt = p;
        if (m_look && f.id == *m_look && !f.pressed) out.look += f.delta;
        if (f.released) {
            if (m_stick && f.id == *m_stick) m_stick.reset();
            if (m_look && f.id == *m_look) m_look.reset();
            if (m_jump && f.id == *m_jump) m_jump.reset();
        }
    }
    if (m_stick) {
        glm::vec2 d = (m_stickAt - m_stickOrigin) / l.stickRadius;
        if (const float len = glm::length(d); len > 1.0f) d /= len;
        out.move = glm::vec2(d.x, -d.y);
    }
    out.jump = m_jump.has_value();
    m_jumpDown = out.jump;
    return out;
}

void TouchControls::draw(float alpha) const {
    if (alpha <= 0.0f) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Layout& l = m_layout;
    auto col = [&](float a, int r = 255, int g = 246, int b = 228) {
        return IM_COL32(r, g, b, int(255.0f * a * alpha));
    };
    auto at = [](glm::vec2 p) { return ImVec2(p.x, p.y); };
    // The stick: where the thumb landed, or a faint ring where it usually lands.
    const glm::vec2 origin = m_stick ? m_stickOrigin : l.stickHome;
    dl->AddCircleFilled(at(origin), l.stickRadius, col(m_stick ? 0.12f : 0.06f), 48);
    dl->AddCircle(at(origin), l.stickRadius, col(m_stick ? 0.5f : 0.25f), 48, 2.0f);
    glm::vec2 knob = origin;
    if (m_stick) {
        glm::vec2 d = m_stickAt - m_stickOrigin;
        if (const float len = glm::length(d); len > l.stickRadius) d *= l.stickRadius / len;
        knob = origin + d;
    }
    dl->AddCircleFilled(at(knob), 24.0f, col(m_stick ? 0.55f : 0.22f), 32);
    // Jump: a rising chevron, brighter while held.
    dl->AddCircleFilled(at(l.jump), l.jumpRadius, col(m_jumpDown ? 0.35f : 0.14f), 48);
    dl->AddCircle(at(l.jump), l.jumpRadius, col(0.55f), 48, 2.0f);
    const float c = l.jumpRadius * 0.34f;
    dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(l.jump.x - c, l.jump.y + c * 0.45f),
                                          ImVec2(l.jump.x, l.jump.y - c * 0.6f),
                                          ImVec2(l.jump.x + c, l.jump.y + c * 0.45f)}
                        .data(),
                    3, col(0.9f), ImDrawFlags_None, 3.5f);
    // Call: rings like the call's own.
    dl->AddCircleFilled(at(l.call), l.callRadius, col(0.14f + 0.3f * m_callGlow), 48);
    dl->AddCircle(at(l.call), l.callRadius, col(0.55f), 48, 2.0f);
    dl->AddCircle(at(l.call), l.callRadius * 0.48f, col(0.9f), 32, 2.5f);
    dl->AddCircleFilled(at(l.call), l.callRadius * 0.16f, col(0.9f), 16);
    // Pause: two bars.
    dl->AddCircleFilled(at(l.pause), l.pauseRadius, col(0.14f), 32);
    const float b = l.pauseRadius * 0.38f;
    dl->AddRectFilled(ImVec2(l.pause.x - b, l.pause.y - b), ImVec2(l.pause.x - b * 0.35f, l.pause.y + b), col(0.85f),
                      1.5f);
    dl->AddRectFilled(ImVec2(l.pause.x + b * 0.35f, l.pause.y - b), ImVec2(l.pause.x + b, l.pause.y + b), col(0.85f),
                      1.5f);
}

} // namespace cloudsong
