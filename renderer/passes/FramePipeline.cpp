#include "passes/FramePipeline.hpp"
#include "core/DrawRecorder.hpp"
#include "render/RenderData.hpp"
#include <stdexcept>

namespace rubia::render
{
rhi::RenderPassLayoutDesc scenePassLayout(rhi::Format color, rhi::Format depth)
{
    using namespace rhi;
    AttachmentDesc colorMsaa;
    colorMsaa.format = color; colorMsaa.samples = sceneSamples;
    colorMsaa.store = StoreOp::Discard; colorMsaa.after = {AccessUsage::ColorWrite};
    AttachmentDesc depthMsaa = colorMsaa;
    depthMsaa.format = depth; depthMsaa.after = {AccessUsage::DepthReadWrite};
    AttachmentDesc resolve = colorMsaa;
    resolve.samples = 1; resolve.load = LoadOp::Discard; resolve.store = StoreOp::Store;
    return {{colorMsaa, depthMsaa, resolve}, {{sceneColorAttachment, sceneResolveAttachment}}, sceneDepthAttachment};
}
rhi::RenderPassLayoutDesc editorOutputPassLayout(rhi::Format color)
{
    rhi::AttachmentDesc output;
    output.format = color;
    output.after = {rhi::AccessUsage::ShaderRead, rhi::shaderStageMask(rhi::ShaderStage::Fragment)};
    return {{output}, {{0, -1}}, -1};
}
uint32_t outputTransferFunction(rhi::Format format, bool srgbNonlinear)
{
    if (format == rhi::Format::RGBA8Srgb || format == rhi::Format::BGRA8Srgb) return 0;
    if (srgbNonlinear) return 1;
    throw std::runtime_error("output shader does not support this color space");
}
namespace
{
rhi::RenderPassDesc pass(const rhi::PassTarget* target, bool depth)
{
    if (!target) throw std::invalid_argument("frame is missing a render target");
    rhi::RenderPassDesc desc{target, {rhi::ClearValue{}}};
    if (depth)
    {
        rhi::ClearValue clear;
        clear.depthStencil = true;
        desc.clears.push_back(clear);
    }
    return desc;
}
void output(rhi::RenderEncoder& commands, const FramePasses& frame,
    const rhi::PassTarget& target, uint32_t transfer)
{
    commands.bindPipeline(frame.outputPipeline);
    commands.setViewport({0, 0, static_cast<float>(target.width()), static_cast<float>(target.height()), 0, 1});
    commands.setScissor({0, 0, target.width(), target.height()});
    commands.bindResources(0, frame.outputBindings);
    PresentPushConstants constants{};
    constants.outputTransferFunction = transfer;
    commands.setConstants(rhi::shaderStageMask(rhi::ShaderStage::Fragment), 0, sizeof(constants), &constants);
    commands.draw(3, 1, 0, 0);
}
}
void recordFramePipeline(rhi::CommandEncoder& commands, OutputMode mode, const FramePasses& frame,
    const PreparedDrawList* scene, const GuiOverlay* gui, GuiCommandContext& guiContext)
{
    if (scene)
    {
        auto& encoder = commands.beginPass(pass(frame.scene, true));
        recordDrawList(encoder, frame.frameBindings, frame.scene->width(), frame.scene->height(), *scene);
        commands.endPass();
        commands.transition(frame.sceneResolve, {}, {rhi::AccessUsage::ColorWrite},
            {rhi::AccessUsage::ShaderRead, rhi::shaderStageMask(rhi::ShaderStage::Fragment)});
        if (mode == OutputMode::Editor)
        {
            auto& editor = commands.beginPass(pass(frame.viewport, false));
            // Store display-encoded SDR in the UNORM viewport for stock GUI sampling.
            output(editor, frame, *frame.viewport, 1);
            commands.endPass();
        }
    }
    auto& present = commands.beginPass(pass(frame.presentation, true));
    if (scene && mode == OutputMode::Runtime) output(present, frame, *frame.presentation, frame.outputTransfer);
    if (gui) gui->record(guiContext);
    // UI is terminal in this pass. No cached state is reused after native GUI draws.
    commands.endPass();
}
} // namespace rubia::render
