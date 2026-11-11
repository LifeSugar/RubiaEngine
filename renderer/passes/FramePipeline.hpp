#pragma once
#include "rhi/CommandEncoder.hpp"
#include "core/PreparedDrawList.hpp"
#include "render/GuiOverlay.hpp"

namespace rubia::render
{
enum class OutputMode { Runtime, Editor };
inline constexpr uint32_t sceneSamples = 4;
inline constexpr uint32_t sceneColorAttachment = 0, sceneDepthAttachment = 1, sceneResolveAttachment = 2;

rhi::RenderPassLayoutDesc scenePassLayout(rhi::Format color, rhi::Format depth);
rhi::RenderPassLayoutDesc editorOutputPassLayout(rhi::Format color);
uint32_t outputTransferFunction(rhi::Format targetFormat, bool srgbNonlinearColorSpace);

struct FramePasses
{
    const rhi::PassTarget* scene = nullptr;
    const rhi::PassTarget* viewport = nullptr;
    const rhi::PassTarget* presentation = nullptr;
    rhi::TextureRef sceneResolve;
    rhi::BindingSetRef frameBindings, outputBindings;
    rhi::PipelineRef outputPipeline;
    uint32_t outputTransfer = 0;
};

/// The three supported frame paths and their GPU commands. Targets and bindings
/// are immutable for the recording interval. No pipeline/resource creation here.
void recordFramePipeline(rhi::CommandEncoder&, OutputMode, const FramePasses&,
    const PreparedDrawList* scene, const GuiOverlay*, GuiCommandContext&);
} // namespace rubia::render
