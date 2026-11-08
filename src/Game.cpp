// Cloudsong: a small caped figure wanders floating isles of grass above a sea of clouds at the golden hour, glides
// between them, gathers drifting lights, lights candles, wakes the spirits and brings them to the temple of light.

#include "Character.h"
#include "Glow.h"
#include "Journey.h"
#include "Physics.h"
#include "Player.h"
#include "World.h"

#include <quench/app/App.h>
#include <quench/app/Input.h>
#include <quench/app/Scenario.h>
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

        const float grass = app.mobile() ? 0.45f : 1.0f;
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

        // The golden hour: a low warm sun behind the way to the temple, a soft haze over the clouds.
        m_towardSun = render::skyDirection(16.0f, 70.0f);
        const render::Daylight day = render::daylightFor(m_towardSun);
        m_sun = m_scene->addLight({.type = render::LightType::Directional,
                                   .color = day.sunColor * glm::vec3(1.25f, 1.05f, 0.85f) * 1.15f,
                                   .direction = -m_towardSun});
        render::Environment env = day.environment;
        env.skyAmbient = glm::mix(env.skyAmbient, glm::vec3(0.55f, 0.6f, 0.75f), 0.5f) * 1.25f;
        env.groundAmbient = glm::vec3(0.55f, 0.48f, 0.42f);
        env.skyGround = glm::vec3(1.05f, 0.95f, 0.92f);
        env.fogDensity = 0.00035f;
        env.fogFalloff = 0.0009f;
        env.fogBaseHeight = kAltitude;
        env.fogMaxAmount = 0.75f;
        env.fogSunScatter = 0.8f;
        env.fogColor = glm::vec3(0.74f, 0.76f, 0.86f);
        m_scene->setEnvironment(env);

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

        render::RenderSettings rs = m_renderer->settings();
        rs.clouds = render::Clouds::Volumetric;
        rs.cloudShadows = false;
        rs.cloudResolution = 0.5f;
        rs.shadowDistance = 90.0f;
        rs.shadowCascades = 3;
        rs.bloom = true;
        rs.bloomStrength = 0.09f;
        rs.bloomThreshold = 1.3f;
        rs.toneCurve = render::ToneCurve::Agx;
        rs.vignette = 0.22f;
        rs.reflections = render::Reflections::ScreenSpace;
        m_renderer->setSettings(rs);

        m_glow.attach(*m_renderer, app.device());

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
        return {};
    }

    void fixedStep(App& app, double dt) override {
        const app::Input& input = app.input();
        PlayerInput in;
        in.move = glm::vec2(input.axis(m_left, m_right), input.axis(m_back, m_forward)) + m_scriptMove;
        if (const float l = glm::length(in.move); l > 1.0f) in.move /= l;
        in.walk = input.down(m_walk);
        in.jumpHeld = input.down(m_jump) || m_scriptJump;
        in.jumpPressed = in.jumpHeld && !m_jumpWasDown;
        m_jumpWasDown = in.jumpHeld;
        in.cameraYaw = m_camera.yaw;
        m_previous = m_current;
        m_player.step(float(dt), in, *m_physics);
        m_current = m_player.position();
        const bool called = (input.down(m_call) || m_scriptCall) && !m_callWasDown;
        m_callWasDown = input.down(m_call) || m_scriptCall;
        m_scriptCall = false;
        if (called) m_callAge = 0.0f;
        const JourneyEvents events =
            m_journey.step(float(dt), m_player, m_current - glm::vec3(0.0f, kAltitude, 0.0f), called);
        if (events.lightTaken >= 0) m_flash = 1.6f;
        if (events.candleLit >= 0) m_world.setCandleLit(*m_scene, events.candleLit, true);
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
        // Camera input: drag with a mouse button, the right stick, the look keys.
        glm::vec2 look(input.axis(m_lookLeft, m_lookRight), -input.axis(m_lookDown, m_lookUp));
        look *= 2.2f * dt;
        if (!input.uiCapturesMouse() && !input.mouseFromTouch() &&
            (input.mouseDown(app::MouseButton::Left) || input.mouseDown(app::MouseButton::Right)))
            look += input.mouseDelta() * 0.005f;
        look.x = -look.x;
        if (m_scriptCamera) look = glm::vec2(0.0f);

        const glm::vec3 feet = glm::mix(m_previous, m_current, float(app.alpha()));
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
        const float pitch = m_player.glide() * 1.1f;
        m_figure.setPlacement(feet, m_player.yaw(), pitch);
        m_figure.update(dt, pose, [this](glm::vec3 from) { return groundBelow(*m_physics, from, 1.2f); });
        m_scene->setTransform(m_figureInstance, m_figure.transform());
        m_scene->setJointPalette(m_figureInstance, m_figure.palette());
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
        m_glow.beam(l.beamBase + alt, 1.4f, l.beamBase + alt + glm::vec3(0.0f, 900.0f, 0.0f), 3.0f,
                    glm::vec3(3.2f, 2.6f, 1.8f), 0.02f, 0.4f);
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
        (void)app;
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
        return std::nullopt;
    }

    // teleport X Y Z [YAW]: the player to a local point. camera YAW PITCH DISTANCE: the orbit (orbit: its yaw from
    // behind the player). view EX EY EZ TX TY TZ:
    // a fixed camera (local points); view off: the orbit again. move X Y: hold the stick; jump down|up; call.
    app::ScenarioExec scenarioCommand(App& app, const app::ScenarioCommand& command) override {
        (void)app;
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
    glm::vec3 eye() const { return m_scriptCamera ? m_scriptCamera->first : m_camera.eye(); }
    glm::mat4 viewMatrix() const {
        if (m_scriptCamera)
            return glm::lookAt(m_scriptCamera->first, m_scriptCamera->second, glm::vec3(0.0f, 1.0f, 0.0f));
        return m_camera.view();
    }

    std::optional<render::Renderer> m_renderer;
    std::optional<render::Scene> m_scene;
    std::optional<PhysicsWorld> m_physics;
    World m_world;
    Glow m_glow;
    weather::Weather m_weather;
    render::SurfaceId m_worldSurface;
    render::LightId m_sun;
    glm::vec3 m_towardSun{0.0f, 1.0f, 0.0f};

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
    app::ActionId m_jump = 0, m_walk = 0, m_call = 0;
    bool m_jumpWasDown = false, m_callWasDown = false, m_scriptCall = false;
    float m_callAge = 100.0f;
    glm::vec3 m_callCentre{0.0f};
    Journey m_journey;
    glm::vec2 m_scriptMove{0.0f};
    bool m_scriptJump = false;
};

} // namespace

Result<std::unique_ptr<quench::app::Game>> quench::app::createGame(AppDesc& desc,
                                                                   std::span<const std::string_view> args) {
    desc.name = QUENCH_GAME_NAME;
    desc.organization = "Quench";
    desc.version = QUENCH_GAME_VERSION;
    for (const std::string_view arg : args) {
        if (arg == "--help") {
            std::printf("%s: no options of its own.\n", QUENCH_GAME_NAME);
            continue;
        }
        return Error{std::format("unknown argument '{}' (--help lists the options)", arg)};
    }
    return std::unique_ptr<Game>(std::make_unique<Cloudsong>());
}
