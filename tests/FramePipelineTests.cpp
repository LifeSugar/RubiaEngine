#include "passes/FramePipeline.hpp"
#include "render/RenderData.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace rubia;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Target final : rhi::PassTarget
{
    explicit Target(std::string name, uint32_t w = 640, uint32_t h = 480) : name(std::move(name)), w(w), h(h) {}
    uint32_t w, h;
    std::string name;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    uint32_t width() const noexcept override { return w; }
    uint32_t height() const noexcept override { return h; }
};
struct Commands final : rhi::CommandEncoder, rhi::RenderEncoder
{
    std::vector<std::string> events;
    std::string active;
    uint32_t width = 0, height = 0;
    rhi::RenderEncoder& beginPass(const rhi::RenderPassDesc& pass) override
    {
        require(active.empty(), "nested pass");
        const auto* target = dynamic_cast<const Target*>(pass.target);
        require(target != nullptr, "missing prepared framebuffer");
        active = target->name;
        width = target->width(); height = target->height();
        const bool depth = active != "viewport";
        require(pass.clears.size() == (depth ? 2 : 1), "pass clears do not match its attachments");
        if (depth) require(pass.clears[1].depthStencil && pass.clears[1].depth == 1,
                           "scene/present depth was not cleared");
        events.push_back("begin " + active);
        return *this;
    }
    void endPass() override
    {
        require(!active.empty(), "ending inactive pass");
        events.push_back("end " + active);
        active.clear();
    }
    void transition(const rhi::TextureRef&, const rhi::SubresourceRange& range,
                    rhi::ResourceAccess before, rhi::ResourceAccess after) override
    {
        require(active.empty(), "resolve barrier is inside the scene pass");
        require(before.usage == rhi::AccessUsage::ColorWrite &&
            after.usage == rhi::AccessUsage::ShaderRead &&
            after.stages == rhi::shaderStageMask(rhi::ShaderStage::Fragment) &&
            range.aspects == rhi::flag(rhi::TextureAspect::Color), "resolve barrier lost access scope");
        events.push_back("resolve readable");
    }
    void bindPipeline(const rhi::PipelineRef&) override { require(!active.empty(), "pipeline outside pass"); }
    void bindResources(uint32_t group, const rhi::BindingSetRef&) override
    { require(group == 0, "fullscreen output bound the wrong group"); }
    void bindVertexBuffer(uint32_t, const rhi::BufferRef&, uint64_t) override
    { throw std::runtime_error("fullscreen output unexpectedly binds a mesh"); }
    void bindIndexBuffer(const rhi::BufferRef&, rhi::IndexType, uint64_t) override
    { throw std::runtime_error("fullscreen output unexpectedly binds indices"); }
    void setViewport(const rhi::Viewport& value) override
    { require(value.width == width && value.height == height, "output viewport ignored target size"); }
    void setScissor(const rhi::Rect& value) override
    { require(value.width == width && value.height == height, "output scissor ignored target size"); }
    void setConstants(rhi::ShaderStageMask stages, uint32_t offset, uint32_t size, const void* data) override
    {
        require(stages == rhi::shaderStageMask(rhi::ShaderStage::Fragment) && offset == 0 &&
            size == sizeof(render::PresentPushConstants), "output constants lost their ABI");
        render::PresentPushConstants constants{};
        std::memcpy(&constants, data, size);
        events.push_back("transfer " + std::to_string(constants.outputTransferFunction));
    }
    void draw(uint32_t count, uint32_t instances, uint32_t first, uint32_t firstInstance) override
    {
        require(count == 3 && instances == 1 && first == 0 && firstInstance == 0,
            "fullscreen output is not one generated triangle");
        events.push_back("output " + active);
    }
    void drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override
    { throw std::runtime_error("fullscreen output unexpectedly draws indices"); }
};
struct GuiContext final : render::GuiCommandContext {};
struct Gui final : render::GuiOverlay
{
    explicit Gui(Commands& commands) : commands(commands) {}
    Commands& commands;
    void record(render::GuiCommandContext&) const override
    {
        require(commands.active == "presentation", "GUI recorded outside terminal presentation pass");
        commands.events.push_back("gui");
    }
};
void testFramePaths()
{
    Target scene("scene", 320, 200), viewport("viewport", 320, 200), present("presentation", 800, 600);
    render::FramePasses frame;
    frame.scene = &scene; frame.viewport = &viewport; frame.presentation = &present;
    frame.outputTransfer = 0;
    render::PreparedDrawList emptyScene;
    GuiContext context;
    Commands runtime; Gui runtimeGui(runtime);
    render::recordFramePipeline(runtime, render::OutputMode::Runtime, frame, &emptyScene, &runtimeGui, context);
    require(runtime.events == std::vector<std::string>{"begin scene", "end scene", "resolve readable",
        "begin presentation", "transfer 0", "output presentation", "gui", "end presentation"},
        "runtime frame reordered scene/output/GUI or lost its barrier");
    Commands editor; Gui editorGui(editor);
    render::recordFramePipeline(editor, render::OutputMode::Editor, frame, &emptyScene, &editorGui, context);
    require(editor.events == std::vector<std::string>{"begin scene", "end scene", "resolve readable",
        "begin viewport", "transfer 1", "output viewport", "end viewport",
        "begin presentation", "gui", "end presentation"},
        "editor did not store display-encoded viewport before GUI presentation");
    Commands guiOnly; Gui overlay(guiOnly);
    render::FramePasses onlyPresent; onlyPresent.presentation = &present;
    render::recordFramePipeline(guiOnly, render::OutputMode::Editor, onlyPresent, nullptr, &overlay, context);
    require(guiOnly.events == std::vector<std::string>{"begin presentation", "gui", "end presentation"},
        "GUI-only frame depends on scene resources");
}
void testPassAccessPolicy()
{
    const auto scene = render::scenePassLayout(rhi::Format::RGBA16Float, rhi::Format::D32Float);
    const auto& resolve = scene.attachments.at(scene.colors.at(0).resolve);
    require(resolve.samples == 1 && resolve.store == rhi::StoreOp::Store &&
        resolve.after.usage == rhi::AccessUsage::ColorWrite,
        "scene resolve no longer feeds the explicit color-to-sampled transition");
    const auto output = render::editorOutputPassLayout(rhi::Format::RGBA8UNorm);
    require(output.attachments[0].store == rhi::StoreOp::Store &&
        output.attachments[0].after.usage == rhi::AccessUsage::ShaderRead &&
        output.attachments[0].after.stages == rhi::shaderStageMask(rhi::ShaderStage::Fragment),
        "viewport pass does not make stored color available to GUI sampling");
    require(render::outputTransferFunction(rhi::Format::BGRA8Srgb, true) == 0 &&
        render::outputTransferFunction(rhi::Format::BGRA8UNorm, true) == 1,
        "output transfer double-encodes sRGB or skips UNORM encoding");
}
}
int main()
{
    try { testFramePaths(); testPassAccessPolicy(); std::cout << "Frame pipeline tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
