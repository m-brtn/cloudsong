#include "Picture.h"
#include <algorithm>
#include <picture_shaders.h>
#include <quench/core/Log.h>

namespace cloudsong {
namespace gpu = quench::gpu;
namespace render = quench::render;

void Picture::attach(render::Renderer& renderer, gpu::Device& device) {
    m_renderer = &renderer;
    m_device = &device;
    m_pass = renderer.addPass(render::PassStage::PostHdr, "picture depth",
                              [this](render::PassContext& context) { capture(context); });
    auto linear =
        device.createSampler({.addressU = gpu::AddressMode::ClampToEdge, .addressV = gpu::AddressMode::ClampToEdge});
    auto point = device.createSampler({.minFilter = gpu::Filter::Nearest,
                                       .magFilter = gpu::Filter::Nearest,
                                       .addressU = gpu::AddressMode::ClampToEdge,
                                       .addressV = gpu::AddressMode::ClampToEdge});
    if (linear) m_linear = std::move(*linear);
    if (point) m_point = std::move(*point);
}

void Picture::detach() {
    if (m_renderer) m_renderer->removePass(m_pass);
    m_renderer = nullptr;
    m_pipeline = {};
    m_image = {};
    m_linear = {};
    m_point = {};
}

gpu::TextureRef Picture::begin(gpu::TextureRef target, const gpu::DisplayOutput& output, float blur, float colour,
                               float dt, bool movingFrame) {
    m_enabled = m_blurActive = false;
    m_depth = {};
    // PQ/BT.2020 needs its own conversion. Apple SDR and extended-linear HDR are supported here.
    if (!m_linear || !m_point || output.composition == gpu::SwapchainComposition::Hdr10 || (blur <= 0 && colour <= 0))
        return target;
    if (!m_image || m_image.desc().width != target.desc.width || m_image.desc().height != target.desc.height ||
        m_image.desc().format != target.desc.format) {
        auto desc = target.desc;
        desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::ColorTarget;
        desc.name = "picture source";
        auto image = m_device->createTexture(desc);
        if (!image) {
            quench::log::warning("game", "picture target: {}", image.error());
            return target;
        }
        const gpu::ColorTargetDesc color{.format = desc.format};
        auto pipeline = m_device->createPipeline({.vertexShader = quench::shaders::picture.find("picture_vs"),
                                                  .fragmentShader = quench::shaders::picture.find("picture_fs"),
                                                  .rasterizer = {.cull = gpu::CullMode::None},
                                                  .colorTargets = {&color, 1},
                                                  .name = "picture"});
        if (!pipeline) {
            quench::log::warning("game", "picture pipeline: {}", pipeline.error());
            return target;
        }
        m_image = std::move(*image);
        m_pipeline = std::move(*pipeline);
    }
    m_uniforms = {};
    m_uniforms.previousClip = glm::mat4(1);
    m_uniforms.size = {1.f / target.desc.width, 1.f / target.desc.height, target.desc.width, target.desc.height};
    // A fixed exposure duration prevents a stronger blur at low frame rates. Skip stalls/cuts.
    const float exposure = movingFrame && dt > .0001f && dt < .1f ? blur / (60.f * dt) : 0;
    const bool srgb = target.desc.format == gpu::TextureFormat::Bgra8UnormSrgb ||
                      target.desc.format == gpu::TextureFormat::Rgba8UnormSrgb;
    m_uniforms.effect = {exposure, 8.f * float(target.desc.height) / 1080.f, colour,
                         !output.hdr() && !srgb ? 1.f : 0.f};
    m_uniforms.depth.w = output.hdr() ? output.headroom * output.paperWhite : 1.f;
    m_enabled = true;
    return m_image;
}

void Picture::capture(render::PassContext& context) {
    if (!m_enabled) return;
    m_depth = context.depth;
    const auto& f = context.frame;
    // Multiplying in double avoids subpixel camera shake at the world's 2200 m altitude.
    const auto inverse = glm::inverse(glm::dmat4(f.viewProjStable));
    m_uniforms.previousClip = glm::mat4(glm::dmat4(f.prevViewProj) * inverse);
    const glm::dvec4 offset = glm::dmat4(f.viewProj) * inverse * glm::dvec4(0, 0, .5, 1);
    m_uniforms.depth.x = float(offset.x / offset.w) * .5f;
    m_uniforms.depth.y = float(offset.y / offset.w) * -.5f;
    m_uniforms.depth.z = f.cameraFwd.w;
    if (!m_depth) m_uniforms.effect.x = 0;
    m_blurActive = m_uniforms.effect.x > 0;
}

void Picture::finish(gpu::CommandBuffer& commands, gpu::TextureRef target) {
    if (!m_enabled) return;
    commands.beginTiming("picture");
    auto pass = commands.beginRenderPass({.texture = target, .load = gpu::LoadOp::DontCare});
    if (pass) {
        const gpu::TextureSamplerBinding bindings[] = {{m_image, m_linear},
                                                       {m_depth ? m_depth : gpu::TextureRef(m_image), m_point}};
        pass.bindPipeline(m_pipeline);
        pass.bindSamplers(quench::ShaderStage::Fragment, 0, bindings);
        pass.pushUniforms(quench::ShaderStage::Fragment, 0, m_uniforms);
        pass.draw(3);
        pass.end();
    }
    commands.endTiming();
}
} // namespace cloudsong
