#include "Glow.h"

#include <quench/core/Log.h>

#include <glow_shaders.h>

namespace cloudsong {

namespace gpu = quench::gpu;
namespace render = quench::render;
using quench::ShaderStage;

void Glow::attach(render::Renderer& renderer, gpu::Device& device) {
    m_renderer = &renderer;
    m_device = &device;
    m_pass = renderer.addPass(render::PassStage::Transparent, "glow",
                              [this](render::PassContext& context) { draw(context); });
}

void Glow::detach() {
    if (m_renderer) m_renderer->removePass(m_pass);
    m_renderer = nullptr;
    m_pipelines.clear();
}

void Glow::begin(glm::vec3 right, glm::vec3 up, float time) {
    m_draws.clear();
    m_vertices.clear();
    m_right = right;
    m_up = up;
    m_time = time;
}

void Glow::beam(glm::vec3 a, float radiusA, glm::vec3 b, float radiusB, glm::vec3 color, float fadeIn, float fadeOut,
                float seed) {
    quench::shader::GlowDraw d{};
    d.a = glm::vec4(a, radiusA);
    d.b = glm::vec4(b, radiusB);
    d.color = glm::vec4(color, float(CS_GLOW_BEAM));
    d.params = glm::vec4(seed, m_time, fadeIn, fadeOut);
    m_draws.push_back(d);
    m_vertices.push_back(CS_BEAM_SEGMENTS * 6);
}

void Glow::ring(glm::vec3 centre, float radius, float height, float strength, glm::vec3 color) {
    quench::shader::GlowDraw d{};
    d.a = glm::vec4(centre, radius);
    d.b = glm::vec4(height, strength, 0.0f, 0.0f);
    d.color = glm::vec4(color, float(CS_GLOW_RING));
    d.params = glm::vec4(0.0f, m_time, 0.0f, 0.0f);
    m_draws.push_back(d);
    m_vertices.push_back(CS_RING_SEGMENTS * 6);
}

void Glow::motes(glm::vec3 centre, glm::vec3 half, int count, float size, float drift, glm::vec3 color, float seed) {
    if (count <= 0) return;
    quench::shader::GlowDraw d{};
    d.a = glm::vec4(centre, float(count));
    d.b = glm::vec4(half, size);
    d.color = glm::vec4(color, float(CS_GLOW_MOTES));
    d.params = glm::vec4(seed, m_time, drift, 0.0f);
    m_draws.push_back(d);
    m_vertices.push_back(std::uint32_t(count) * 6u);
}

void Glow::sprite(glm::vec3 at, float radius, glm::vec3 color, float core) {
    // Sprites share a draw until it is full.
    if (m_draws.empty() || m_draws.back().color.w != float(CS_GLOW_SPRITES) ||
        m_vertices.back() >= CS_MAX_SPRITES * 6) {
        quench::shader::GlowDraw d{};
        d.color = glm::vec4(1.0f, 1.0f, 1.0f, float(CS_GLOW_SPRITES));
        d.params = glm::vec4(0.0f, m_time, 0.0f, 0.0f);
        m_draws.push_back(d);
        m_vertices.push_back(0);
    }
    quench::shader::GlowDraw& d = m_draws.back();
    const std::uint32_t i = m_vertices.back() / 6u;
    d.sprite[i] = glm::vec4(at, radius);
    d.spriteColor[i] = glm::vec4(color, core);
    m_vertices.back() += 6;
}

const gpu::GraphicsPipeline* Glow::pipeline(const render::PassContext& context) {
    const auto key = std::pair(context.colorFormat, context.depthFormat);
    if (const auto it = m_pipelines.find(key); it != m_pipelines.end()) return it->second ? &it->second : nullptr;
    const gpu::ColorTargetDesc color{.format = context.colorFormat, .blend = gpu::BlendState::additive()};
    quench::Result<gpu::GraphicsPipeline> p = m_device->createPipeline({
        .vertexShader = quench::shaders::glow.find("glow_vs"),
        .fragmentShader = quench::shaders::glow.find("glow_fs"),
        .rasterizer = {.cull = gpu::CullMode::None},
        .depth = {.test = true, .write = false},
        .colorTargets = {&color, 1},
        .depthFormat = context.depthFormat,
        .name = "glow",
    });
    if (!p) quench::log::error("game", "glow pipeline: {}", p.error());
    gpu::GraphicsPipeline& slot = m_pipelines[key];
    if (p) slot = std::move(*p);
    return slot ? &slot : nullptr;
}

void Glow::draw(render::PassContext& context) {
    m_drawn = 0;
    if (m_draws.empty() || !context.pass) return;
    const gpu::GraphicsPipeline* p = pipeline(context);
    if (!p) return;
    context.pass->bindPipeline(*p);
    for (std::size_t i = 0; i < m_draws.size(); ++i) {
        quench::shader::GlowDraw& d = m_draws[i];
        d.right = glm::vec4(m_right, 0.0f);
        d.up = glm::vec4(m_up, 0.0f);
        context.pass->pushUniforms(ShaderStage::Vertex, 1, d);
        context.pass->pushUniforms(ShaderStage::Fragment, 1, d);
        context.pass->draw(m_vertices[i]);
        ++m_drawn;
    }
}

} // namespace cloudsong
