// Cloudsong: a small caped figure wanders floating isles of grass above a sea of clouds at the golden hour, glides
// between them, gathers drifting lights, lights candles, wakes the spirits and brings them to the temple of light.

#include "Character.h"
#include "Front.h"
#include "Glow.h"
#include "Journey.h"
#include "Physics.h"
#include "Player.h"
#include "Settings.h"
#include "Sound.h"
#include "Spirits.h"
#include "World.h"

#include <quench/app/App.h>
#include <quench/app/Input.h>
#include <quench/app/Scenario.h>
#include <quench/core/FileSystem.h>
#include <quench/core/Log.h>
#include <quench/physics/PhysicsWorld.h>
#include <quench/render/Daylight.h>
#include <quench/render/Renderer.h>
#include <quench/render/Scene.h>
#include <quench/weather/Weather.h>

#include <foliage_shaders.h>
#include <world_shaders.h>

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>

namespace {

using namespace quench;
using namespace cloudsong;
using app::App;

float parseFloat(std::string_view s, float fallback = 0.0f) {
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    return end == text.c_str() ? fallback : v;
}

// A camera that orbits the player: turned by the mouse, the right stick or a finger, drifting behind the player's
// heading when left alone, pulled in by what stands between it and the player.
class CameraRig {
public:
    float yaw = 0.0f;    // heading the camera looks along (0: +z)
    float pitch = 0.26f; // down (radians)
    float distance = 5.6f;

    void reset(glm::vec3 target, float heading) {
        yaw = heading;
        pitch = 0.26f;
        m_pivot = target;
        m_distance = distance;
        m_idle = 0.0f;
    }

    void update(float dt, glm::vec2 look, glm::vec3 target, glm::vec3 velocity, float heading, float glide,
                const PhysicsWorld& physics) {
        const bool turned = glm::length(look) > 1e-4f;
        yaw = wrapAngle(yaw + look.x);
        pitch = std::clamp(pitch + look.y, -0.9f, 1.25f);
        m_idle = turned ? 0.0f : m_idle + dt;
        const float speed = glm::length(glm::vec2(velocity.x, velocity.z));
        // Behind the player when it moves and nobody turns the camera.
        if (m_idle > 1.2f && speed > 0.8f) {
            const float follow = glide > 0.3f ? 1.4f : 3.5f;
            yaw = wrapAngle(yaw +
                            wrapAngle(heading - yaw) * (1.0f - std::exp(-dt / follow)) * std::min(speed / 4.0f, 1.0f));
            const float wantPitch = glide > 0.3f ? 0.24f : 0.26f;
            pitch = approach(pitch, wantPitch, 2.5f, dt);
        }
        m_pivot.x = approach(m_pivot.x, target.x, 0.06f, dt);
        m_pivot.z = approach(m_pivot.z, target.z, 0.06f, dt);
        m_pivot.y = approach(m_pivot.y, target.y, glide > 0.3f ? 0.12f : 0.2f, dt);
        const float want = distance * (1.0f + 0.45f * glide);
        const glm::vec3 dir = direction();
        float reach = want;
        if (const std::optional<float> hit = castRay(physics, m_pivot, m_pivot - dir * (want + 0.3f)))
            reach = std::max(0.6f, *hit * (want + 0.3f) - 0.3f);
        m_distance = reach < m_distance ? reach : approach(m_distance, reach, 0.35f, dt);
    }

    glm::vec3 direction() const {
        return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }
    glm::vec3 eye() const { return m_pivot - direction() * m_distance; }
    glm::vec3 pivot() const { return m_pivot; }
    glm::mat4 view() const { return glm::lookAt(eye(), m_pivot, glm::vec3(0.0f, 1.0f, 0.0f)); }

private:
    glm::vec3 m_pivot{0.0f};
    float m_distance = 5.2f;
    float m_idle = 0.0f;
};

class Cloudsong final : public app::Game {
public:
    Result<void> init(App& app) override {
        Result<render::Renderer> renderer = render::Renderer::create(app.device());
        if (!renderer) return Error{renderer.error()};
        m_renderer.emplace(std::move(*renderer));
        m_scene.emplace(app.device());
        m_physics.emplace(&app.jobs());

        Result<render::SurfaceId> world = m_renderer->addSurface({
            .vertexShader = shaders::world.find("surface_vs"),
            .fragmentShader = shaders::world.find("surface_fs"),
            .basicFragmentShader = shaders::world.find("surface_basic_fs"),
            .name = "world",
        });
        if (!world) return Error{world.error()};
        Result<render::SurfaceId> foliage = m_renderer->addSurface({
            .vertexShader = shaders::foliage.find("surface_vs"),
            .fragmentShader = shaders::foliage.find("surface_fs"),
            .basicFragmentShader = shaders::foliage.find("surface_basic_fs"),
            .shadowVertexShader = shaders::foliage.find("surface_shadow_vs"),
            .name = "foliage",
        });
        if (!foliage) return Error{foliage.error()};
        m_worldSurface = *world;

        // The player's settings (scripted runs start from the defaults and leave the user's file alone).
        m_settings = defaultSettings(app.mobile());
        if (!app.scripted())
            if (Result<std::string> text = fs::readUserText("settings.json"); text)
                m_settings = parseSettings(*text, m_settings);
        m_screen = app.scripted() ? Screen::Playing : Screen::Title;
        m_touchUi = app.mobile();
        // The grass's density is the quality's at startup.
        const float grass = m_settings.quality == Quality::High     ? 1.0f
                            : m_settings.quality == Quality::Medium ? 0.55f
                                                                    : 0.35f;
        if (Result<void> built = m_world.build(*m_scene, *m_physics, *world, *foliage, grass); !built)
            return Error{built.error()};
        m_physics->optimizeBroadPhase();

        // The player.
        const Landmarks& l = m_world.landmarks();
        const glm::vec3 start = l.start + glm::vec3(0.0f, kAltitude + 0.05f, 0.0f);
        m_player.create(*m_physics, start, l.startYaw);
        m_previous = m_current = start;
        {
            CharacterLook look;
            look.cape = srgb(214, 88, 70);
            look.capeInner = srgb(140, 46, 40);
            look.hood = srgb(196, 78, 64);
            CharacterMesh mesh = Character::buildMesh(look);
            render::MeshDesc desc = mesh.mesh.desc("player");
            desc.skin = mesh.skin;
            desc.jointCount = kJointCount;
            Result<render::MeshId> id = m_scene->addMesh(desc);
            if (!id) return Error{id.error()};
            render::MaterialDesc figure{.surface = *world, .roughness = 0.8f, .doubleSided = true};
            figure.params[0] = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
            m_figureMaterial = m_scene->addMaterial(figure);
            m_figure = Character(look);
            m_figure.setPlacement(start, l.startYaw);
            m_figureInstance =
                m_scene->addInstance({.mesh = *id, .material = m_figureMaterial, .transform = m_figure.transform()});
        }
        m_camera.reset(start + glm::vec3(0.0f, 1.1f, 0.0f), l.startYaw);
        m_journey.reset(l);
        if (Result<void> spirits = m_spirits.create(*m_scene, *world, l, m_world.takeNavGeometry(), app.jobs());
            !spirits)
            return Error{spirits.error()};
        m_sound.init(app.audio());

        m_sun = m_scene->addLight({.type = render::LightType::Directional});
        applyMood();

        weather::Settings ws;
        ws.seed = 7;
        ws.coverage = 0.7f;
        ws.type = weather::CloudType::Cumulus;
        ws.typeVariation = 0.3f;
        ws.cirrus = 0.35f;
        ws.wind = {.direction = {1.0f, 0.35f}, .speed = 4.0f, .gusts = 0.3f, .turbulence = 0.15f};
        ws.evolution = 0.4f;
        ws.featureSize = 3200.0f;
        ws.cloudBase = 700.0f;
        m_weather.setSettings(ws);
        m_weather.setTime(1800.0);

        applySettings(app);
        if (app.mobile()) {
            // A calm game: 60 Hz on phones, less when they grow hot.
            app.setMaxFrameRate(60.0);
            app.thermalGovernor().enable({});
        }

        m_glow.attach(*m_renderer, app.device());

        // The titles' font (Marcellus, SIL Open Font License; assets/fonts/OFL.txt); ImGui's own without it.
        if (Result<fs::Bytes> font = fs::readAsset("fonts/Marcellus-Regular.ttf"); font && !font->empty()) {
            void* data = IM_ALLOC(font->size());
            std::memcpy(data, font->data(), font->size());
            m_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, int(font->size()), 20.0f);
        } else {
            log::warning("game", "no title font: {}", font ? "empty file" : font.error());
        }
        m_menus.setFont(m_font);
        m_menus.setTouch(m_touchUi);
        // A gamepad moves through the menus; the keyboard's Space, Return and Escape are the game's own.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        using app::Binding;
        using app::GamepadAxis;
        using app::GamepadButton;
        app::Input& input = app.input();
        m_left = input.addAction(
            "left", {*Binding::key("A"), *Binding::key("Left"), Binding::gamepadAxis(GamepadAxis::LeftX, false)});
        m_right = input.addAction(
            "right", {*Binding::key("D"), *Binding::key("Right"), Binding::gamepadAxis(GamepadAxis::LeftX, true)});
        m_forward = input.addAction(
            "forward", {*Binding::key("W"), *Binding::key("Up"), Binding::gamepadAxis(GamepadAxis::LeftY, false)});
        m_back = input.addAction(
            "back", {*Binding::key("S"), *Binding::key("Down"), Binding::gamepadAxis(GamepadAxis::LeftY, true)});
        m_lookLeft =
            input.addAction("look_left", {*Binding::key("J"), Binding::gamepadAxis(GamepadAxis::RightX, false)});
        m_lookRight =
            input.addAction("look_right", {*Binding::key("L"), Binding::gamepadAxis(GamepadAxis::RightX, true)});
        m_lookUp = input.addAction("look_up", {*Binding::key("I"), Binding::gamepadAxis(GamepadAxis::RightY, false)});
        m_lookDown =
            input.addAction("look_down", {*Binding::key("K"), Binding::gamepadAxis(GamepadAxis::RightY, true)});
        m_jump = input.addAction("jump", {*Binding::key("Space"), Binding::gamepad(GamepadButton::South)});
        m_walk = input.addAction("walk", {*Binding::key("Left Shift")});
        m_call = input.addAction("call", {*Binding::key("E"), Binding::gamepad(GamepadButton::West)});
        m_pause = input.addAction("pause", {*Binding::key("Escape"), Binding::gamepad(GamepadButton::Start)});
        m_confirm = input.addAction("confirm", {*Binding::key("Return"), *Binding::key("Space")});
        return {};
    }

    void fixedStep(App& app, double dt) override {
        if (m_screen != Screen::Playing) {
            m_previous = m_current;
            return;
        }
        const app::Input& input = app.input();
        PlayerInput in;
        in.move =
            glm::vec2(input.axis(m_left, m_right), input.axis(m_back, m_forward)) + m_scriptMove + m_touchState.move;
        if (const float l = glm::length(in.move); l > 1.0f) in.move /= l;
        in.walk = input.down(m_walk);
        in.jumpHeld = input.down(m_jump) || m_scriptJump || m_touchState.jump;
        in.jumpPressed = in.jumpHeld && !m_jumpWasDown;
        m_jumpWasDown = in.jumpHeld;
        in.cameraYaw = m_camera.yaw;
        const Landmarks& l = m_world.landmarks();
        const glm::vec3 local = m_current - glm::vec3(0.0f, kAltitude, 0.0f);
        // The updraft, once every candle burns: it carries the player up to the height of the temple.
        if (const glm::vec2 off(local.x - l.updraft.x, local.z - l.updraft.z);
            updraftOpen() && glm::length(off) < l.updraftRadius * (local.y > l.updraft.y + 2.0f ? 1.6f : 1.0f) &&
            local.y < l.updraftTop) {
            in.lift = 9.0f;
            in.pull = -off * 2.5f;
        }
        if (m_ending >= 0.0f) in = PlayerInput{.cameraYaw = m_camera.yaw};
        m_previous = m_current;
        const float fall = -m_player.velocity().y;
        m_player.step(float(dt), in, *m_physics);
        m_current = m_player.position();
        const glm::vec3 feet = m_current - glm::vec3(0.0f, kAltitude, 0.0f);
        if (m_player.flapped()) m_sound.flap();
        if (m_player.jumped()) m_sound.jump();
        if (m_player.landed()) m_sound.land(m_world.groundAt(feet), fall);
        const bool called =
            (((input.down(m_call) || m_scriptCall) && !m_callWasDown) || m_touchCall) && m_ending < 0.0f;
        m_callWasDown = input.down(m_call) || m_scriptCall;
        m_scriptCall = false;
        m_touchCall = false;
        if (called) {
            m_callAge = 0.0f;
            m_sound.call();
        }
        const JourneyEvents events = m_journey.step(float(dt), m_player, feet, called);
        if (events.lightTaken >= 0) {
            m_flash = 1.6f;
            m_sound.lightTaken(int(m_journey.lightsTaken()));
        }
        if (events.candleLit >= 0) candleLit(events.candleLit);
        // The end: standing at the altar.
        if (m_ending < 0.0f && m_player.grounded() &&
            glm::length(glm::vec2(feet.x - l.altar.x, feet.z - l.altar.z)) < 3.2f &&
            std::abs(feet.y - l.altar.y) < 2.5f) {
            m_ending = 0.0f;
            m_endYaw = m_camera.yaw;
            m_spirits.gather();
        }
        // Fallen into the clouds: back to the last firm ground.
        if (m_player.grounded() && m_safeTimer <= 0.0f) {
            m_safe = m_current;
            m_safeYaw = m_player.yaw();
            m_safeTimer = 0.5f;
        }
        m_safeTimer -= float(dt);
        if (m_current.y < kAltitude - 70.0f) {
            m_player.teleport(m_safe + glm::vec3(0.0f, 0.3f, 0.0f), m_safeYaw);
            m_previous = m_current = m_player.position();
            m_figure.resetCape();
        }
    }

    void update(App& app, double frameDt) override {
        const float dt = float(frameDt);
        m_time += dt;
        const app::Input& input = app.input();
        updateScreen(app, dt);
        // Camera input: drag with a mouse button or a finger, the right stick, the look keys.
        glm::vec2 look(input.axis(m_lookLeft, m_lookRight), -input.axis(m_lookDown, m_lookUp));
        look *= 2.2f * dt;
        if (!input.uiCapturesMouse() && !input.mouseFromTouch() &&
            (input.mouseDown(app::MouseButton::Left) || input.mouseDown(app::MouseButton::Right)))
            look += input.mouseDelta() * 0.005f;
        look += m_touchState.look * 0.006f;
        look.x = -look.x;
        look *= m_settings.lookSpeed;
        if (m_settings.invertLook) look.y = -look.y;
        if (m_scriptCamera || m_screen != Screen::Playing) look = glm::vec2(0.0f);

        const glm::vec3 feet = glm::mix(m_previous, m_current, float(app.alpha()));
        if (m_ending >= 0.0f) {
            // The closing moment: the camera turns slowly round the altar, drawing back and up past the columns.
            const glm::vec3 altar = m_world.landmarks().altar + glm::vec3(0.0f, kAltitude, 0.0f);
            const float t = smoothstep(0.0f, 16.0f, m_ending);
            const float a = m_endYaw + 0.05f * m_ending;
            const glm::vec3 from = altar + glm::vec3(-std::sin(a), 0.0f, -std::cos(a)) * glm::mix(4.5f, 19.0f, t) +
                                   glm::vec3(0.0f, glm::mix(1.6f, 9.0f, t), 0.0f);
            m_endView = std::pair(from, altar + glm::vec3(0.0f, glm::mix(1.2f, 4.0f, t), 0.0f));
        }
        if (!m_scriptCamera)
            m_camera.update(dt, look, feet + glm::vec3(0.0f, 1.05f, 0.0f), m_player.velocity(), m_player.yaw(),
                            m_player.glide(), *m_physics);

        // The figure.
        PoseInput pose;
        pose.speed = glm::length(glm::vec2(m_player.velocity().x, m_player.velocity().z));
        pose.grounded = m_player.grounded();
        pose.verticalSpeed = m_player.velocity().y;
        pose.glide = m_player.glide();
        pose.flap = m_player.flap();
        pose.wind = m_weather.windAt(feet + glm::vec3(0.0f, 0.8f, 0.0f)) * 0.3f;
        if (m_ending >= 0.0f) pose.pray = smoothstep(1.0f, 3.0f, m_ending) * (1.0f - smoothstep(9.0f, 11.0f, m_ending));
        const float pitch = m_player.glide() * 1.1f;
        m_figure.setPlacement(feet, m_player.yaw(), pitch);
        m_figure.update(dt, pose, [this](glm::vec3 from) { return groundBelow(*m_physics, from, 1.2f); });
        m_scene->setTransform(m_figureInstance, m_figure.transform());
        m_scene->setJointPalette(m_figureInstance, m_figure.palette());
        if (m_figure.stepped()) m_sound.footstep(m_world.groundAt(feet - glm::vec3(0.0f, kAltitude, 0.0f)), pose.speed);
        if (m_ending >= 0.0f && m_screen == Screen::Playing) m_ending += dt;
        m_spirits.update(
            dt, {.feet = feet, .yaw = m_player.yaw(), .grounded = m_player.grounded(), .airTime = m_player.airTime()},
            *m_scene, *m_physics);
        {
            const glm::vec3 eyeNow = eye();
            const glm::vec3 v = m_player.velocity();
            const float air = m_player.grounded() ? 0.0f : glm::length(v) * (0.4f + 0.6f * m_player.glide());
            m_sound.update({.listener = eyeNow,
                            .forward = glm::normalize(feet + glm::vec3(0.0f, 1.0f, 0.0f) - eyeNow),
                            .wind = 2.5f + air,
                            .spirits = m_spirits.awake(),
                            .ending = smoothstep(0.0f, 8.0f, m_ending)},
                           frameDt);
        }
        m_flash = std::max(0.0f, m_flash - dt * 2.0f);
        if (m_player.flapped()) m_flash = 1.0f;
        m_scene->setInstanceColor(m_figureInstance, glm::vec4(1.0f),
                                  glm::vec4(m_player.energy, float(m_player.maxEnergy), m_flash, 0.0f));

        if (m_callAge < 50.0f) {
            if (m_callAge == 0.0f) m_callCentre = feet;
            m_callAge += dt;
            m_world.setCallRing(*m_scene, m_callCentre, m_callAge);
        }
        m_weather.update(frameDt);
        m_scene->setWeather(m_weather);
        m_world.updateDetail(*m_scene, eye());

        // The glow of this frame.
        const glm::mat4 view = viewMatrix();
        const glm::mat4 inv = glm::inverse(view);
        m_glow.begin(glm::vec3(inv[0]), glm::vec3(inv[1]), m_time);
        const glm::vec3 alt(0.0f, kAltitude, 0.0f);
        const Landmarks& l = m_world.landmarks();
        const float bright =
            m_ending >= 0.0f ? smoothstep(2.0f, 7.0f, m_ending) * (0.5f + 0.5f * float(m_spirits.awake())) : 0.0f;
        m_glow.beam(l.beamBase + alt, 1.4f + 0.8f * bright, l.beamBase + alt + glm::vec3(0.0f, 900.0f, 0.0f),
                    3.0f + 2.0f * bright, glm::vec3(3.2f, 2.6f, 1.8f) * (1.0f + 1.6f * bright), 0.02f, 0.4f);
        if (bright > 0.0f)
            m_glow.beam(l.beamBase + alt, 0.6f + 0.6f * bright, l.beamBase + alt + glm::vec3(0.0f, 60.0f, 0.0f),
                        1.6f + 1.5f * bright, glm::vec3(3.0f, 2.5f, 1.7f) * bright, 0.01f, 0.85f, 5.0f);
        if (m_ending >= 2.0f) {
            // Rings of light rise from the altar.
            for (int k = 0; k < 3; ++k) {
                const float age = std::fmod(m_ending - 2.0f + float(k) * 1.3f, 4.0f);
                m_glow.ring(l.altar + alt + glm::vec3(0.0f, 0.2f + age * 3.0f, 0.0f), 2.0f + age * 2.5f, 0.35f,
                            bright * (1.0f - age / 4.0f), glm::vec3(2.4f, 2.0f, 1.3f));
            }
        }
        // The updraft: a column of rising motes and a faint shaft once it is open.
        if (updraftOpen()) {
            m_glow.beam(l.updraft + alt, l.updraftRadius * 0.8f,
                        l.updraft + alt + glm::vec3(0.0f, l.updraftTop - l.updraft.y, 0.0f), l.updraftRadius,
                        glm::vec3(0.9f, 1.0f, 1.1f) * 0.5f, 0.05f, 0.6f, 3.0f);
            m_glow.motes(l.updraft + alt + glm::vec3(0.0f, 10.0f, 0.0f),
                         glm::vec3(l.updraftRadius, 10.0f, l.updraftRadius), 120, 0.05f, 2.0f,
                         glm::vec3(2.4f, 2.4f, 2.0f), 9.0f);
        }
        // The spirits' soft glow.
        for (int i = 0; i < int(m_spirits.count()); ++i)
            if (const float g = m_spirits.glow(i); g > 0.0f)
                m_glow.sprite(m_spirits.position(i) + glm::vec3(0.0f, 0.62f, 0.0f), 0.75f,
                              (m_spirits.color(i) * 0.9f + glm::vec3(0.25f)) * g, 0.0f);
        for (const glm::vec3& shaft : l.caveShafts)
            m_glow.beam(shaft + alt + glm::vec3(-1.6f, 7.2f, 0.8f), 0.55f, shaft + alt, 0.95f,
                        glm::vec3(1.2f, 1.1f, 0.8f), 0.1f, 0.75f);
        m_glow.motes(l.caveCentre + alt + glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(8.0f, 2.5f, 8.0f), 90, 0.035f, 0.6f,
                     glm::vec3(4.0f, 3.6f, 1.6f), 3.0f);
        m_glow.motes(l.start + alt + glm::vec3(0.0f, 2.0f, 20.0f), glm::vec3(30.0f, 2.0f, 30.0f), 160, 0.03f, 1.0f,
                     glm::vec3(3.0f, 2.6f, 1.4f), 5.0f);
        // The drifting lights; a gathered one flies into the cape and fades.
        const glm::vec3 chest = feet + glm::vec3(0.0f, 0.75f, 0.0f);
        for (std::size_t i = 0; i < l.lights.size(); ++i) {
            const glm::vec3 light = l.lights[i];
            glm::vec3 p = light + alt + glm::vec3(0.0f, 0.12f * std::sin(m_time * 1.7f + light.x), 0.0f);
            const float age = m_journey.lightAge(i);
            if (age > 0.6f) continue;
            float size = 0.7f;
            if (age >= 0.0f) {
                const float t = smoothstep(0.0f, 0.6f, age);
                p = glm::mix(p, chest, t);
                size *= 1.0f - t * 0.8f;
            }
            m_glow.sprite(p, size, glm::vec3(2.2f, 1.9f, 1.2f), 0.6f);
        }
        // Flames: the lanterns and the lit candles.
        for (const glm::vec3& lantern : l.lanterns)
            m_glow.sprite(lantern + alt, 1.3f + 0.08f * std::sin(m_time * 9.0f + lantern.x),
                          glm::vec3(2.4f, 1.5f, 0.6f), 0.15f);
        for (std::size_t i = 0; i < l.candles.size(); ++i)
            if (m_journey.candleLit(i))
                m_glow.sprite(l.candles[i] + alt + glm::vec3(0.0f, 0.08f, 0.0f),
                              1.1f + 0.08f * std::sin(m_time * 11.0f + float(i)), glm::vec3(2.4f, 1.5f, 0.6f), 0.15f);
    }

    void ui(App& app) override {
        const bool canQuit = !app.mobile();
        MenuAction action = MenuAction::None;
        if (m_screen == Screen::Title) {
            action = m_menus.title(m_settings, 1.0f, canQuit);
        } else if (m_screen == Screen::Paused) {
            action = m_menus.pause(m_settings, canQuit);
        } else {
            const float since = m_time - m_begunAt;
            const float fade = smoothstep(1.5f, 2.5f, since) * (1.0f - smoothstep(10.0f, 11.5f, since)) +
                               smoothstep(12.5f, 13.5f, since) * (1.0f - smoothstep(22.0f, 24.0f, since));
            if (fade > 0.0f && m_ending < 0.0f) m_menus.hint(controlsHint(app), fade);
            if (m_touchUi && m_ending < 0.0f) m_touch.draw(1.0f);
        }
        if (m_screen != Screen::Title && m_beginFade < 1.0f) m_menus.title(m_settings, 1.0f - m_beginFade, canQuit);
        switch (action) {
        case MenuAction::Begin: begin(); break;
        case MenuAction::Resume: m_screen = Screen::Playing; break;
        case MenuAction::Quit: app.quit(); break;
        case MenuAction::SettingsChanged:
            applySettings(app);
            if (!app.scripted())
                if (Result<void> saved = fs::writeUserText("settings.json", writeSettings(m_settings)); !saved)
                    log::warning("game", "settings not saved: {}", saved.error());
            break;
        case MenuAction::None: break;
        }
        if (m_ending >= 6.0f) endingCard();
        if (!m_debugUi) return;
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Cloudsong", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const glm::vec3 p = m_player.position() - glm::vec3(0.0f, kAltitude, 0.0f);
        ImGui::Text("%.1f %.1f %.1f energy %.1f/%d", double(p.x), double(p.y), double(p.z), double(m_player.energy),
                    m_player.maxEnergy);
        ImGui::End();
    }

    void render(App& app, app::Frame& frame) override {
        (void)app;
        const render::View view{.view = viewMatrix(),
                                .fovY = glm::radians(52.0f + 8.0f * m_player.glide()),
                                .nearPlane = 0.1f,
                                .cut = m_cut,
                                .time = double(m_time)};
        m_cut = false;
        m_renderer->render(*m_scene, view, frame.commands, frame.target, frame.output);
    }

    void shutdown(App& app) override {
        (void)app;
        m_glow.detach();
        m_sound.shutdown();
        m_spirits.destroy();
        m_player.destroy();
        m_scene.reset();
        m_renderer.reset();
        m_physics.reset();
    }

    std::optional<double> scenarioValue(std::string_view name) const override {
        const glm::vec3 p = m_player.position() - glm::vec3(0.0f, kAltitude, 0.0f);
        if (name == "x") return p.x;
        if (name == "y") return p.y;
        if (name == "z") return p.z;
        if (name == "grounded") return m_player.grounded() ? 1.0 : 0.0;
        if (name == "glide") return m_player.glide();
        if (name == "energy") return m_player.energy;
        if (name == "max_energy") return m_player.maxEnergy;
        if (name == "glow_draws") return m_glow.draws();
        if (name == "lights") return double(m_journey.lightsTaken());
        if (name == "candles") return double(m_journey.candlesLit());
        if (name == "warm") return m_journey.warm() ? 1.0 : 0.0;
        if (name == "cape_up") return m_figure.capePoint(kCapeRows - 1).y - m_figure.capePoint(0).y;
        if (name == "cape_back") {
            const glm::vec3 d = m_figure.capePoint(kCapeRows - 1) - m_figure.capePoint(0);
            return -(d.x * std::sin(m_player.yaw()) + d.z * std::cos(m_player.yaw()));
        }
        if (name == "vx")
            return m_player.velocity().x * std::sin(m_player.yaw()) + m_player.velocity().z * std::cos(m_player.yaw());
        if (name == "vy") return m_player.velocity().y;
        if (name == "awake") return m_spirits.awake();
        if (name == "walking") return m_spirits.walking();
        if (name == "nav_ready") return m_spirits.navReady() ? 1.0 : 0.0;
        if (name == "ending") return m_ending;
        if (name == "screen") return double(int(m_screen));
        if (name == "quality") return double(int(m_settings.quality));
        if (name == "render_scale") return m_renderer->settings().renderScale;
        if (name == "updraft") return updraftOpen() ? 1.0 : 0.0;
        if (name == "seated") return m_spirits.seated();
        if (name == "altar_dist") {
            const glm::vec3 a = m_world.landmarks().altar;
            return glm::length(glm::vec2(p.x - a.x, p.z - a.z));
        }
        if (name == "candle_dist") {
            double best = 1e9;
            const Landmarks& l = m_world.landmarks();
            for (std::size_t i = 0; i < l.candles.size(); ++i)
                if (!m_journey.candleLit(i))
                    best = std::min(best, double(glm::length(glm::vec2(p.x - l.candles[i].x, p.z - l.candles[i].z))));
            return best;
        }
        if (name == "spirit_far") {
            double far = 0.0;
            for (int i = 0; i < int(m_spirits.count()); ++i)
                if (m_spirits.state(i) != Spirits::State::Dormant)
                    far = std::max(far, double(m_spirits.distanceTo(i, m_player.position())));
            return far;
        }
        return std::nullopt;
    }

    // teleport X Y Z [YAW]: the player to a local point. camera YAW PITCH DISTANCE: the orbit (orbit: its yaw from
    // behind the player). light I: candle I lit. aim X Z: the orbit's heading toward a local point. view EX EY EZ TX TY
    // TZ: a fixed camera (local points); view off: the orbit again. move X Y: hold the stick; jump down|up; call.
    app::ScenarioExec scenarioCommand(App& app, const app::ScenarioCommand& command) override {
        const auto& a = command.args;
        auto f = [&](std::size_t i) { return parseFloat(a[i]); };
        if (command.op == "teleport" && a.size() >= 3) {
            const glm::vec3 p = glm::vec3(f(0), f(1), f(2)) + glm::vec3(0.0f, kAltitude, 0.0f);
            const float yaw = a.size() > 3 ? f(3) : m_player.yaw();
            m_player.teleport(p, yaw);
            m_previous = m_current = p;
            m_figure.setPlacement(p, yaw);
            m_figure.resetCape();
            m_camera.reset(p + glm::vec3(0.0f, 1.05f, 0.0f), yaw);
            m_cut = true;
            return app::ScenarioExec::Done;
        }
        if ((command.op == "camera" || command.op == "orbit") && a.size() >= 3) {
            const float yaw = command.op == "orbit" ? wrapAngle(m_player.yaw() + f(0)) : f(0);
            m_camera.distance = f(2);
            m_camera.reset(m_current + glm::vec3(0.0f, 1.05f, 0.0f), yaw);
            m_camera.pitch = f(1);
            m_cut = true;
            return app::ScenarioExec::Done;
        }
        if (command.op == "light" && a.size() == 1) {
            // light I: candle I lit (its spirit wakes), as if called; for checks of the later parts.
            const int i = int(f(0));
            if (i < 0 || i >= int(m_world.landmarks().candles.size())) return app::ScenarioExec::Unknown;
            if (!m_journey.candleLit(std::size_t(i))) {
                m_journey.lightCandle(std::size_t(i));
                candleLit(i);
            }
            return app::ScenarioExec::Done;
        }
        if (command.op == "aim" && a.size() >= 2) {
            // The orbit behind the player, looking toward a local point.
            const glm::vec3 p = m_current - glm::vec3(0.0f, kAltitude, 0.0f);
            m_camera.yaw = std::atan2(f(0) - p.x, f(1) - p.z);
            return app::ScenarioExec::Done;
        }
        if (command.op == "view" && a.size() == 1 && a[0] == "off") {
            m_scriptCamera.reset();
            m_cut = true;
            return app::ScenarioExec::Done;
        }
        if (command.op == "view" && a.size() >= 6) {
            const glm::vec3 alt(0.0f, kAltitude, 0.0f);
            m_scriptCamera = std::pair(glm::vec3(f(0), f(1), f(2)) + alt, glm::vec3(f(3), f(4), f(5)) + alt);
            m_cut = true;
            return app::ScenarioExec::Done;
        }
        if (command.op == "move" && a.size() >= 2) {
            m_scriptMove = glm::vec2(f(0), f(1));
            return app::ScenarioExec::Done;
        }
        if (command.op == "call" && a.empty()) {
            m_scriptCall = true;
            return app::ScenarioExec::Done;
        }
        if (command.op == "jump" && a.size() == 1) {
            m_scriptJump = a[0] == "down";
            return app::ScenarioExec::Done;
        }
        if (command.op == "clouds" && a.size() == 1) {
            render::RenderSettings rs = m_renderer->settings();
            rs.clouds = a[0] == "layer" ? render::Clouds::Layer
                        : a[0] == "off" ? render::Clouds::Off
                                        : render::Clouds::Volumetric;
            m_renderer->setSettings(rs);
            return app::ScenarioExec::Done;
        }
        if (command.op == "mood" && a.size() >= 2) {
            // mood KEY VALUES: the light, the sky and the haze, for tuning by captures.
            Mood& m = m_mood;
            auto v3 = [&] { return a.size() >= 4 ? glm::vec3(f(1), f(2), f(3)) : glm::vec3(f(1)); };
            const std::string_view key = a[0];
            if (key == "sun" && a.size() >= 3)
                m.sunElevation = f(1), m.sunAzimuth = f(2);
            else if (key == "tint")
                m.sunTint = v3();
            else if (key == "scale")
                m.sunScale = f(1);
            else if (key == "atmosphere")
                m.atmosphere = f(1);
            else if (key == "zenith")
                m.zenith = v3();
            else if (key == "horizon")
                m.horizon = v3();
            else if (key == "glow")
                m.glow = v3();
            else if (key == "ambient_tint")
                m.ambientTint = v3();
            else if (key == "ambient")
                m.ambient = f(1);
            else if (key == "fog" && a.size() >= 3)
                m.fogDensity = f(1), m.fogMax = f(2);
            else if (key == "fog_color")
                m.fogColor = v3();
            else if (key == "exposure")
                m.exposure = f(1);
            else
                return app::ScenarioExec::Unknown;
            applyMood();
            return app::ScenarioExec::Done;
        }
        if (command.op == "screen" && a.size() == 1) {
            // screen title|play|pause|settings: the title, play, the pause menu (with its settings).
            m_menus.openSettings(a[0] == "settings");
            if (a[0] == "title") {
                m_screen = Screen::Title;
                m_beginFade = 0.0f;
            } else if (a[0] == "play") {
                if (m_screen == Screen::Title) begin();
                m_screen = Screen::Playing;
            } else if (a[0] == "pause" || a[0] == "settings") {
                m_screen = Screen::Paused;
            } else {
                return app::ScenarioExec::Unknown;
            }
            return app::ScenarioExec::Done;
        }
        if (command.op == "touch_controls" && a.size() == 1) {
            m_touchUi = a[0] == "on";
            m_menus.setTouch(m_touchUi);
            return app::ScenarioExec::Done;
        }
        if (command.op == "quality" && a.size() == 1) {
            m_settings.quality = a[0] == "low" ? Quality::Low : a[0] == "medium" ? Quality::Medium : Quality::High;
            applySettings(app);
            return app::ScenarioExec::Done;
        }
        if (command.op == "debug_ui" && a.size() == 1) {
            m_debugUi = a[0] == "on";
            return app::ScenarioExec::Done;
        }
        return app::ScenarioExec::Unknown;
    }

    std::string perfDetails(App& app) override {
        (void)app;
        const render::RenderStats& s = m_renderer->stats();
        return std::format("draws={} instances={} culled={} shadow_draws={} render={}x{}", s.draws, s.instances,
                           s.culled, s.shadowDraws, s.renderWidth, s.renderHeight);
    }

private:
    // The closing card over the last view: the title, the spirits brought home, thanks.
    void endingCard() {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("##ending", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        const float fade = smoothstep(6.0f, 9.0f, m_ending);
        const float scale = vp->Size.y / 360.0f / ImGui::GetStyle().FontScaleDpi;
        auto line = [&](const char* text, float size, float y, float alpha) {
            ImGui::PushFont(m_font, 13.0f * size * scale);
            const ImVec2 extent = ImGui::CalcTextSize(text);
            ImGui::SetCursorPos(ImVec2((vp->Size.x - extent.x) * 0.5f, vp->Size.y * y));
            // A soft shadow under the letters.
            ImGui::SetCursorPos(ImVec2((vp->Size.x - extent.x) * 0.5f + 1.5f * scale, vp->Size.y * y + 1.5f * scale));
            ImGui::TextColored(ImVec4(0.25f, 0.18f, 0.12f, alpha * 0.35f), "%s", text);
            ImGui::SetCursorPos(ImVec2((vp->Size.x - extent.x) * 0.5f, vp->Size.y * y));
            ImGui::TextColored(ImVec4(1.0f, 0.97f, 0.9f, alpha), "%s", text);
            ImGui::PopFont();
        };
        line("Cloudsong", 3.0f, 0.2f, fade);
        const int home = m_spirits.awake();
        const std::string spirits = home == int(m_spirits.count())
                                        ? "Every spirit is home."
                                        : std::format("{} of {} spirits are home.", home, m_spirits.count());
        line(spirits.c_str(), 1.1f, 0.34f, fade * 0.9f);
        line("Thank you for playing.", 0.9f, 0.82f, smoothstep(10.0f, 13.0f, m_ending) * 0.85f);
        ImGui::End();
    }

    void candleLit(int candle) {
        m_world.setCandleLit(*m_scene, candle, true);
        m_spirits.wake(candle);
        const glm::vec3 at = m_world.landmarks().candles[std::size_t(candle)] + glm::vec3(0.0f, kAltitude, 0.0f);
        m_sound.candleLit(at);
        m_sound.spiritWakes(at);
    }
    // The golden hour: a low warm sun, a pastel sky, a soft haze that pales the far isles.
    void applyMood() {
        const Mood& m = m_mood;
        m_towardSun = render::skyDirection(m.sunElevation, m.sunAzimuth);
        const render::Daylight day = render::daylightFor(m_towardSun);
        m_scene->setLight(m_sun, {.type = render::LightType::Directional,
                                  .color = day.sunColor * m.sunTint * m.sunScale,
                                  .direction = -m_towardSun});
        render::Environment env = day.environment;
        env.atmosphere = m.atmosphere;
        env.skyZenith *= m.zenith;
        env.skyHorizon *= m.horizon;
        env.sunGlow *= m.glow;
        env.sunDisc *= m.glow;
        env.skyAmbient = glm::mix(env.skyAmbient, m.ambientTint, 0.5f) * m.ambient;
        env.groundAmbient = glm::vec3(0.55f, 0.48f, 0.42f);
        env.skyGround = glm::vec3(1.05f, 0.95f, 0.92f);
        env.fogDensity = m.fogDensity;
        env.fogFalloff = 0.0009f;
        env.fogBaseHeight = kAltitude;
        env.fogMaxAmount = m.fogMax;
        env.fogSunScatter = 0.8f;
        env.fogColor = m.fogColor;
        env.exposure = m.exposure;
        m_scene->setEnvironment(env);
    }
    bool updraftOpen() const { return m_journey.candlesLit() == m_world.landmarks().candles.size(); }
    // The title's view: low behind the figure on the meadow, past it toward the temple's beam, drifting slowly.
    std::pair<glm::vec3, glm::vec3> titleView() const {
        const Landmarks& l = m_world.landmarks();
        const glm::vec3 feet = l.start + glm::vec3(0.0f, kAltitude, 0.0f);
        const glm::vec3 ahead(std::sin(l.startYaw), 0.0f, std::cos(l.startYaw));
        const glm::vec3 side(ahead.z, 0.0f, -ahead.x);
        const float drift = std::sin(m_time * 0.05f);
        return {feet - ahead * 4.4f + side * (1.7f + 0.9f * drift) + glm::vec3(0.0f, 0.9f + 0.15f * drift, 0.0f),
                feet + ahead * 40.0f + glm::vec3(0.0f, 8.0f, 0.0f)};
    }
    std::pair<glm::vec3, glm::vec3> currentView() const {
        if (m_scriptCamera) return *m_scriptCamera;
        if (m_endView) return *m_endView;
        if (m_screen == Screen::Title) return titleView();
        const std::pair<glm::vec3, glm::vec3> rig(m_camera.eye(), m_camera.pivot());
        if (m_beginFade >= 1.0f) return rig;
        const std::pair<glm::vec3, glm::vec3> title = titleView();
        const float k = smoothstep(0.0f, 1.0f, m_beginFade);
        return {glm::mix(title.first, rig.first, k), glm::mix(title.second, rig.second, k)};
    }
    glm::vec3 eye() const { return currentView().first; }
    glm::mat4 viewMatrix() const {
        const std::pair<glm::vec3, glm::vec3> v = currentView();
        return glm::lookAt(v.first, v.second, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void begin() {
        if (m_screen != Screen::Title) return;
        m_screen = Screen::Playing;
        m_menus.openSettings(false);
        m_begunAt = m_time;
        m_beginFade = 0.0f;
        m_jumpWasDown = true; // the Space that began is not a jump
    }

    // Pause and resume, the touch controls, the title's fade.
    void updateScreen(App& app, float dt) {
        const app::Input& input = app.input();
        if (m_touchUi && m_screen == Screen::Playing && m_ending < 0.0f) {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            const app::WindowRect safe = app.safeArea();
            m_touchState =
                m_touch.update(input, glm::vec2(size.x, size.y),
                               glm::vec4(float(safe.x), float(safe.y), float(safe.width), float(safe.height)));
            if (m_touchState.call) m_touchCall = true;
        } else {
            m_touchState = TouchState{};
            m_touch.reset();
        }
        const bool pause = input.pressed(m_pause) || m_touchState.pause;
        if (m_screen == Screen::Playing && pause) {
            m_screen = Screen::Paused;
            m_menus.openSettings(false);
            m_touchState = TouchState{};
            m_touch.reset();
        } else if (m_screen == Screen::Paused && pause) {
            if (m_menus.settingsOpen())
                m_menus.openSettings(false);
            else
                m_screen = Screen::Playing;
        } else if (m_screen == Screen::Title && !m_menus.settingsOpen() && input.pressed(m_confirm)) {
            begin();
        }
        if (m_screen != Screen::Title) m_beginFade = std::min(1.0f, m_beginFade + dt / 1.6f);
        // Phones grow hot: the thermal governor's limits go to the renderer, never into the player's settings.
        if (app.mobile()) {
            render::RenderSettings rs = m_playerRender;
            const app::ThermalGovernor& heat = app.thermalGovernor();
            rs.renderScale = std::max(heat.renderScale(rs.renderScale), 0.25f);
            if (heat.reduceEffects()) rs.clouds = render::Clouds::Layer;
            const render::RenderSettings& now = m_renderer->settings();
            if (rs.renderScale != now.renderScale || rs.clouds != now.clouds) m_renderer->setSettings(rs);
        }
    }

    const char* controlsHint(const App& app) const {
        const float since = m_time - m_begunAt;
        if (since > 12.0f) return "Gather the drifting lights, and call beside the cold candles.";
        if (m_touchUi)
            return "Left thumb to walk, right thumb to look, the arrow to jump (hold to glide), the rings to call";
        if (app.input().gamepadConnected())
            return "Left stick to walk, A to jump (hold to glide), X to call, right stick to look";
        return "WASD to walk, Space to jump (hold to glide), E to call, drag the mouse to look, Esc to rest";
    }

    // The quality's render settings and the volumes.
    void applySettings(App& app) {
        (void)app;
        render::RenderSettings rs = m_renderer->settings();
        rs.cloudShadows = false;
        rs.bloom = true;
        rs.bloomStrength = 0.09f;
        rs.bloomThreshold = 1.3f;
        rs.toneCurve = render::ToneCurve::Agx;
        rs.vignette = 0.22f;
        switch (m_settings.quality) {
        case Quality::High:
            rs.renderScale = 1.0f;
            rs.clouds = render::Clouds::Volumetric;
            rs.cloudResolution = 0.5f;
            rs.cloudSteps = 64;
            rs.shadowCascades = 3;
            rs.shadowMapSize = 2048;
            rs.shadowDistance = 90.0f;
            rs.shadowFilter = 2;
            rs.reflections = render::Reflections::ScreenSpace;
            break;
        case Quality::Medium:
            rs.renderScale = 0.85f;
            rs.clouds = render::Clouds::Volumetric;
            rs.cloudResolution = 0.35f;
            rs.cloudSteps = 40;
            rs.shadowCascades = 3;
            rs.shadowMapSize = 1536;
            rs.shadowDistance = 70.0f;
            rs.shadowFilter = 1;
            rs.reflections = render::Reflections::Sky;
            break;
        case Quality::Low:
            rs.renderScale = 0.7f;
            rs.clouds = render::Clouds::Layer;
            rs.shadowCascades = 2;
            rs.shadowMapSize = 1024;
            rs.shadowDistance = 50.0f;
            rs.shadowFilter = 1;
            rs.reflections = render::Reflections::Sky;
            break;
        }
        m_playerRender = rs;
        m_renderer->setSettings(rs);
        m_sound.setVolumes(m_settings.music, m_settings.effects);
    }

    struct Mood {
        float sunElevation = 10.0f, sunAzimuth = 135.0f; // degrees: ahead and to the right on the way to the temple
        glm::vec3 sunTint{1.3f, 1.0f, 0.75f};
        float sunScale = 1.15f;
        float atmosphere = 1.0f;
        glm::vec3 zenith{0.9f, 0.92f, 1.0f}, horizon{1.2f, 0.95f, 0.85f}, glow{1.6f, 1.25f, 0.9f}; // daylight tints
        glm::vec3 ambientTint{0.6f, 0.62f, 0.82f}; // a cool fill against the warm sun
        float ambient = 1.4f;
        float fogDensity = 0.0007f, fogMax = 0.85f;
        glm::vec3 fogColor{0.92f, 0.8f, 0.78f};
        float exposure = 1.08f;
    };

    std::optional<render::Renderer> m_renderer;
    std::optional<render::Scene> m_scene;
    std::optional<PhysicsWorld> m_physics;
    World m_world;
    Glow m_glow;
    weather::Weather m_weather;
    render::SurfaceId m_worldSurface;
    render::LightId m_sun;
    glm::vec3 m_towardSun{0.0f, 1.0f, 0.0f};
    Mood m_mood;

    Player m_player;
    glm::vec3 m_previous{0.0f}, m_current{0.0f};
    glm::vec3 m_safe{0.0f};
    float m_safeYaw = 0.0f;
    float m_safeTimer = 0.0f;
    Character m_figure;
    render::MaterialId m_figureMaterial;
    render::InstanceId m_figureInstance;
    float m_flash = 0.0f;
    CameraRig m_camera;
    std::optional<std::pair<glm::vec3, glm::vec3>> m_scriptCamera;
    bool m_cut = true;
    float m_time = 0.0f;
    bool m_debugUi = false;

    app::ActionId m_left = 0, m_right = 0, m_forward = 0, m_back = 0;
    app::ActionId m_lookLeft = 0, m_lookRight = 0, m_lookUp = 0, m_lookDown = 0;
    app::ActionId m_jump = 0, m_walk = 0, m_call = 0, m_pause = 0, m_confirm = 0;
    enum class Screen { Title, Playing, Paused };
    Screen m_screen = Screen::Playing;
    Settings m_settings;
    render::RenderSettings m_playerRender;
    Menus m_menus;
    TouchControls m_touch;
    TouchState m_touchState;
    bool m_touchUi = false, m_touchCall = false;
    float m_begunAt = -100.0f; // m_time when the player began
    float m_beginFade = 1.0f;  // 0..1 from the title's view to the player's
    bool m_jumpWasDown = false, m_callWasDown = false, m_scriptCall = false;
    float m_callAge = 100.0f;
    glm::vec3 m_callCentre{0.0f};
    Journey m_journey;
    Spirits m_spirits;
    Sound m_sound;
    float m_ending = -1.0f; // seconds since the end began
    ImFont* m_font = nullptr;
    float m_endYaw = 0.0f;
    std::optional<std::pair<glm::vec3, glm::vec3>> m_endView;
    glm::vec2 m_scriptMove{0.0f};
    bool m_scriptJump = false;
};

} // namespace

Result<std::unique_ptr<quench::app::Game>> quench::app::createGame(AppDesc& desc,
                                                                   std::span<const std::string_view> args) {
    desc.name = QUENCH_GAME_NAME;
    desc.organization = "Quench";
    desc.version = QUENCH_GAME_VERSION;
    desc.audio.buses = {
        {.name = "music"},
        {.name = "effects"},
        {.name = "ui"},
        {.name = "reverb", .volume = 0.8f, .reverb = quench::audio::ReverbDesc{.roomSize = 0.86f, .damping = 0.45f}}};
    for (const std::string_view arg : args) {
        if (arg == "--help") {
            std::printf("%s: no options of its own.\n", QUENCH_GAME_NAME);
            continue;
        }
        return Error{std::format("unknown argument '{}' (--help lists the options)", arg)};
    }
    return std::unique_ptr<Game>(std::make_unique<Cloudsong>());
}
