#pragma once

#include "shaders/PictureUniforms.h"
#include <quench/gpu/Device.h>
#include <quench/render/Renderer.h>

namespace cloudsong {

// A final camera-motion/colour pass before the UI, never written into TAA history.
class Picture {
public:
    void attach(quench::render::Renderer& renderer, quench::gpu::Device& device);
    void detach();
    quench::gpu::TextureRef begin(quench::gpu::TextureRef target, const quench::gpu::DisplayOutput& output, float blur,
                                  float colour, float dt, bool movingFrame);
    void finish(quench::gpu::CommandBuffer& commands, quench::gpu::TextureRef target);
    bool blurActive() const { return m_blurActive; }

private:
    void capture(quench::render::PassContext& context);
    quench::render::Renderer* m_renderer = nullptr;
    quench::gpu::Device* m_device = nullptr;
    quench::render::PassId m_pass{};
    quench::gpu::Texture m_image;
    quench::gpu::Sampler m_linear, m_point;
    quench::gpu::GraphicsPipeline m_pipeline;
    quench::gpu::TextureRef m_depth;
    quench::shader::PictureUniforms m_uniforms{};
    bool m_enabled = false, m_blurActive = false;
};
} // namespace cloudsong
