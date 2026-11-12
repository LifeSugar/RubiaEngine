#include "imgui/ApplicationGuiRenderBridge.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
using namespace rubia;
using namespace rubia::integration::imgui;
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
struct View final : rhi::TextureViewResource
{
    rhi::TextureRef image;
    rhi::TextureViewDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::TextureViewDesc& description() const noexcept override { return desc; }
    const rhi::TextureRef& texture() const noexcept override { return image; }
};
struct Registration final : GuiTexture
{
    Registration(uintptr_t id, rhi::TextureViewRef view, int& live)
        : id(id), view(std::move(view)), live(live) { ++live; }
    ~Registration() override { --live; }
    uintptr_t token() const noexcept override { return id; }
    uintptr_t id;
    rhi::TextureViewRef view;
    int& live;
};
struct Backend final : GuiRendererBackend
{
    uintptr_t next = 1;
    int live = 0, failAfter = -1;
    void initialize(ImGuiContext*) override {}
    void shutdown() noexcept override {}
    bool ready() const noexcept override { return true; }
    void newFrame() override {}
    void prepare(ImDrawData*) override {}
    void reconfigure() override {}
    void record(render::GuiCommandContext&) const override {}
    GuiTextureRef registerTexture(const rhi::TextureViewRef& view) override
    {
        if (failAfter == 0) throw std::runtime_error("injected registration failure");
        if (failAfter > 0) --failAfter;
        return std::make_shared<Registration>(next++, view, live);
    }
};
struct Host final : render::GuiRenderHost
{
    bool viewport = false;
    uint32_t slot = 0, width = 640, height = 480;
    uint64_t revision = 1;
    int waits = 0;
    std::vector<rhi::TextureViewRef> views{std::make_shared<View>(), std::make_shared<View>()};
    std::vector<std::shared_ptr<const void>> pending;
    bool hasViewport() const noexcept override { return viewport; }
    render::TextureOutput::Extent outputExtent() const noexcept override { return {width, height}; }
    uint32_t currentFrameIndex() const noexcept override { return slot; }
    uint32_t frameCount() const noexcept override { return 2; }
    render::TextureOutput viewportOutput(uint32_t index) const override
    { return {views.at(index), {width, height}, revision}; }
    void resizeViewport(uint32_t w, uint32_t h) override
    { width = w; height = h; replace(); }
    void replace()
    { views = {std::make_shared<View>(), std::make_shared<View>()}; ++revision; }
    void waitIdle() const override { ++const_cast<Host*>(this)->waits; }
    void retireGuiResource(std::shared_ptr<const void> owner) override { pending.push_back(std::move(owner)); }
    void drainGuiResources() override { waitIdle(); pending.clear(); }
};
void testBridge()
{
    Host host;
    Backend backend;
    render::RenderAssetStore assets;
    integration::imgui::ApplicationGuiRenderBridge bridge;
    bridge.attach(host, assets, backend);
    require(!bridge.currentFrame().sceneViewport && backend.live == 0,
        "runtime output registered scene textures");
    host.viewport = true;
    const auto first = bridge.currentFrame();
    require(first.sceneViewport && backend.live == 2, "viewport registrations missing");
    host.slot = 1;
    require(bridge.currentFrame().sceneViewport.textureId != first.sceneViewport.textureId,
        "GUI ignored the CPU frame slot");
    std::weak_ptr<const rhi::TextureViewResource> oldView = host.views[0];
    host.replace();
    backend.failAfter = 1;
    bool rejected = false;
    try { static_cast<void>(bridge.currentFrame()); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && backend.live == 2 && host.pending.empty() && !oldView.expired(),
        "partial registration failure destroyed the previous GUI output");
    backend.failAfter = -1;
    static_cast<void>(bridge.currentFrame());
    require(backend.live == 4 && host.pending.size() == 2 && !oldView.expired(),
        "output replacement released registrations before GPU completion");
    host.drainGuiResources();
    require(backend.live == 2 && oldView.expired(), "retirement did not release old views");
    bridge.resizeSceneViewport(320, 200);
    require(host.waits == 2 && bridge.currentFrame().width == 320 && backend.live == 2,
        "GUI resize did not synchronize and replace registrations");
    bridge.detach();
    require(backend.live == 0 && host.pending.empty() && !bridge.currentFrame().sceneViewport,
        "detach leaked GUI registrations");
}
}
int main()
{
    try { testBridge(); std::cout << "Backend-neutral GUI registration tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
