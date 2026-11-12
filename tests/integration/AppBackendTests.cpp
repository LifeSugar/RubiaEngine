#include "App.hpp"
#include "ApplicationGui.hpp"
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rubia::test
{
namespace
{
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
struct State
{
    std::vector<std::string> events;
    GLFWwindow* window = nullptr;
    bool scene = false;
    int frames = 0, sceneFrames = 0, guiFrames = 0, resizes = 0;
};
struct GuiRenderer final : integration::imgui::GuiRendererBackend
{
    State& state;
    bool initialized = false;
    explicit GuiRenderer(State& state) : state(state) {}
    void initialize(ImGuiContext*) override
    { ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures; initialized = true; }
    void shutdown() noexcept override
    {
        if (initialized)
        {
            state.events.push_back("GUI shutdown");
            ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
            initialized = false;
        }
    }
    bool ready() const noexcept override { return initialized; }
    void newFrame() override {}
    void prepare(ImDrawData* data) override { require(data && data->Valid, "invalid draw data"); }
    void reconfigure() override { state.events.push_back("GUI reconfigure"); }
    void record(render::GuiCommandContext&) const override { state.events.push_back("GUI record"); }
    integration::imgui::GuiTextureRef registerTexture(const rhi::TextureViewRef&) override
    { throw std::logic_error("test has no sampled viewport"); }
};
struct GuiPlatform final : integration::imgui::GuiPlatformBackend
{
    void initialize(ImGuiContext*) override {}
    void shutdown() noexcept override {}
    void newFrame() override
    { ImGui::GetIO().DisplaySize = {320, 240}; ImGui::GetIO().DeltaTime = 1.0f / 60; }
    float contentScale() const override { return 1; }
};
// A non-Vulkan implementation exercises the actual App loop/resize/cleanup.
struct Backend final : editor::ApplicationBackend, render::ResourcePreparationHost, render::GuiRenderHost
{
    State& state;
    bool initialized = true;
    platform::Extent2D size{320, 240};
    explicit Backend(State& state) : state(state) {}
    ~Backend() override { state.events.push_back("backend destroyed"); }
    bool ready() const noexcept override { return initialized; }
    void waitIdle() const override { state.events.push_back("idle"); }
    void shutdown() noexcept override { initialized = false; state.events.push_back("backend shutdown"); }
    platform::Extent2D extent() const noexcept override { return size; }
    void resize(platform::Extent2D extent) override
    {
        require(extent.width && extent.height, "zero extent reached backend resize");
        require(!state.events.empty() && state.events.back() == "drain", "resize preceded GUI drain");
        size = extent; ++state.resizes; state.events.push_back("resize");
    }
    render::RenderResult draw(const render::GuiOverlay* gui)
    {
        render::GuiCommandContext commands;
        if (gui) gui->record(commands);
        ++state.frames;
        if (state.frames == 4) glfwSetWindowShouldClose(state.window, GLFW_TRUE);
        const render::RenderResult results[] = {render::RenderResult::Skipped,
            render::RenderResult::NeedsResize, render::RenderResult::RenderedNeedsResize,
            render::RenderResult::Rendered};
        return results[state.frames - 1];
    }
    render::RenderResult render(const render::RenderFrame&, const render::RenderAssetStore&,
        const render::GuiOverlay* gui) override { ++state.sceneFrames; return draw(gui); }
    render::RenderResult renderGui(const render::GuiOverlay* gui) override
    { ++state.guiFrames; return draw(gui); }
    bool sceneReady() const noexcept override { return state.scene; }
    void beginScenePreparation(render::RenderAssetStore&, render::SceneResourceRequest) override {}
    render::ScenePreparationStatus scenePreparationStatus() const override { return {}; }
    void activatePreparedScene() override {}
    void cancelScenePreparation() noexcept override {}
    render::ResourcePreparationHost& preparationHost() noexcept override { return *this; }
    render::GuiRenderHost& guiHost() noexcept override { return *this; }
    std::unique_ptr<integration::imgui::GuiRendererBackend> createGuiRenderer() override
    { return std::make_unique<GuiRenderer>(state); }
    std::unique_ptr<integration::imgui::GuiPlatformBackend> createGuiPlatform() override
    { return std::make_unique<GuiPlatform>(); }
    bool canPrepareResources() const noexcept override { return false; }
    render::ResourcePreparationService& resourcePreparationService() override
    { throw std::logic_error("unused preparation service"); }
    void advanceResourcePreparation() override {}
    void createPreparedSceneResources(std::shared_ptr<const render::PreparedMaterialTemplate>,
        std::shared_ptr<const render::PreparedShaderProgram>, uint32_t) override {}
    render::PipelinePreparationResult prewarmScenePipelines(const render::RenderAssetStore&,
        const std::vector<asset::MaterialAssetHandle>&) override { return {}; }
    render::PipelinePreparationStatus pipelinePreparationStatus(render::PipelinePreparationTicket) const override { return {}; }
    void cancelPipelinePreparation(render::PipelinePreparationTicket) override {}
    void releasePipelinePreparation(render::PipelinePreparationTicket) override {}
    void releasePreparedSceneResources() noexcept override {}
    bool hasViewport() const noexcept override { return false; }
    render::TextureOutput::Extent outputExtent() const noexcept override { return {size.width, size.height}; }
    uint32_t currentFrameIndex() const noexcept override { return 0; }
    uint32_t frameCount() const noexcept override { return 1; }
    render::TextureOutput viewportOutput(uint32_t) const override { return {}; }
    void resizeViewport(uint32_t, uint32_t) override {}
    void retireGuiResource(std::shared_ptr<const void>) override {}
    void drainGuiResources() override { state.events.push_back("drain"); }
};
struct Gui final : editor::ApplicationGui
{
    editor::ApplicationGuiFrameOutput draw(const editor::ApplicationGuiContext&) override
    { ImGui::TextUnformatted("Backend-independent App loop"); return {}; }
};
}
class AppBackendTests
{
public:
    static void run()
    {
        for (bool scene : {false, true})
        {
            State state;
            state.scene = scene;
            editor::App app;
            editor::App::RunConfig config;
            config.windowWidth = 320; config.windowHeight = 240;
            app.initWindow(config, false);
            state.window = app.window.nativeHandle();
            app.backend_ = std::make_unique<Backend>(state);
            app.resourcePreparation_ = std::make_unique<render::ResourcePreparationSession>(
                app.backend_->preparationHost(), app.renderAssets);
            app.scene.create({"Test scene", {}});
            app.setupCamera();
            app.initImGui(config);
            app.guiRenderBridge.attach(app.backend_->guiHost(), app.renderAssets, app.imguiLayer.rendererBackend());
            Gui gui;
            app.mainLoop(gui);
            require(state.frames == 4 && state.resizes == 2, "frame outcomes did not drive presentation reconfiguration");
            require(state.sceneFrames == (scene ? 4 : 0) && state.guiFrames == (scene ? 0 : 4),
                "App chose the wrong scene/GUI render path");
            app.cleanup();
            const auto guiEnd = std::find(state.events.begin(), state.events.end(), "GUI shutdown");
            const auto backendEnd = std::find(state.events.begin(), state.events.end(), "backend shutdown");
            const auto deviceEnd = std::find(state.events.begin(), state.events.end(), "backend destroyed");
            require(guiEnd < backendEnd && backendEnd < deviceEnd, "GUI/renderer/device shutdown order is invalid");
            require(!app.backend_ && !app.resourcePreparation_ && !app.window, "cleanup retained the backend session");
        }
        for (auto api : {editor::GraphicsApi::Direct3D12, editor::GraphicsApi::Metal, editor::GraphicsApi::OpenGL})
        {
            editor::App app;
            editor::App::RunConfig config;
            config.graphicsApi = api;
            bool rejected = false;
            try { app.initBackend(config); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected && !app.backend_, "unimplemented API silently selected a backend");
        }
    }
};
}
int main()
{
    try { rubia::test::AppBackendTests::run(); std::cout << "Application backend boundary tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
