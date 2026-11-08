#pragma once

// The game's glow pass: light added to the HDR image after the transparent surfaces (shafts in the cave, the
// temple's beam, the call's ring, drifting motes, soft halos), collected each frame and drawn by the renderer's
// PassStage::Transparent callback.

#include "shaders/GlowUniforms.h"

#include <quench/gpu/Device.h>
#include <quench/render/Renderer.h>

#include <glm/glm.hpp>

#include <map>
#include <vector>

namespace cloudsong {

class Glow {
public:
    void attach(quench::render::Renderer& renderer, quench::gpu::Device& device);
    void detach();

    // Starts a frame's list: the camera's right and up (world) and the time for drifting things.
    void begin(glm::vec3 right, glm::vec3 up, float time);
    void beam(glm::vec3 a, float radiusA, glm::vec3 b, float radiusB, glm::vec3 color, float fadeIn = 0.15f,
              float fadeOut = 0.7f, float seed = 0.0f);
    void ring(glm::vec3 centre, float radius, float height, float strength, glm::vec3 color);
    void motes(glm::vec3 centre, glm::vec3 half, int count, float size, float drift, glm::vec3 color, float seed);
    void sprite(glm::vec3 at, float radius, glm::vec3 color, float core = 0.0f);

    int draws() const { return m_drawn; }

private:
    void draw(quench::render::PassContext& context);
    const quench::gpu::GraphicsPipeline* pipeline(const quench::render::PassContext& context);

    quench::render::Renderer* m_renderer = nullptr;
    quench::gpu::Device* m_device = nullptr;
    quench::render::PassId m_pass{};
    std::map<std::pair<quench::gpu::TextureFormat, quench::gpu::TextureFormat>, quench::gpu::GraphicsPipeline>
        m_pipelines;
    std::vector<quench::shader::GlowDraw> m_draws;
    std::vector<std::uint32_t> m_vertices;
    glm::vec3 m_right{1.0f, 0.0f, 0.0f}, m_up{0.0f, 1.0f, 0.0f};
    float m_time = 0.0f;
    int m_drawn = 0;
};

} // namespace cloudsong
