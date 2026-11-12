#include "ImGuiLayer.hpp"
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>

namespace
{
using namespace rubia;
using namespace rubia::integration::imgui;
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
struct State
{
    std::vector<std::string> events;
    ImGuiContext* context = nullptr;
    bool failInitialization = false;
};
struct Platform final : GuiPlatformBackend
{
    State& state;
    explicit Platform(State& state) : state(state) {}
    void initialize(ImGuiContext* context) override
    { state.context = context; state.events.push_back("platform init"); }
    void shutdown() noexcept override { state.events.push_back("platform shutdown"); }
    void newFrame() override
    {
        require(ImGui::GetCurrentContext() == state.context, "wrong GUI context");
        auto& io = ImGui::GetIO();
        io.DisplaySize = {640, 480};
        io.DeltaTime = 1.0f / 60;
        state.events.push_back("platform frame");
    }
    float contentScale() const override { return 1; }
};
struct Renderer final : GuiRendererBackend
{
    State& state;
    bool initialized = false;
    explicit Renderer(State& state) : state(state) {}
    void initialize(ImGuiContext* context) override
    {
        require(context == state.context, "platform and renderer used different GUI contexts");
        state.events.push_back("renderer init");
        if (state.failInitialization) throw std::runtime_error("injected renderer init failure");
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        initialized = true;
    }
    void shutdown() noexcept override
    {
        state.events.push_back("renderer shutdown");
        if (initialized) ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
        initialized = false;
    }
    bool ready() const noexcept override { return initialized; }
    void newFrame() override { state.events.push_back("renderer frame"); }
    void prepare(ImDrawData* data) override
    { require(data && data->Valid, "GUI draw data was not finalized"); state.events.push_back("prepare"); }
    void reconfigure() override { state.events.push_back("reconfigure"); }
    GuiTextureRef registerTexture(const rhi::TextureViewRef&) override { return {}; }
    void record(render::GuiCommandContext&) const override { state.events.push_back("record"); }
};
editor::ImGuiLayer::CreateInfo config(State& state)
{
    editor::ImGuiLayer::CreateInfo result;
    result.platform = std::make_unique<Platform>(state);
    result.renderer = std::make_unique<Renderer>(state);
    return result;
}
}
int main()
{
    try
    {
        State state;
        editor::ImGuiLayer layer(config(state));
        layer.beginFrame();
        ImGui::Begin("Independent GUI");
        ImGui::TextUnformatted("No graphics API required");
        ImGui::End();
        auto* overlay = layer.endFrame();
        require(overlay == &layer.rendererBackend(), "GUI overlay bypassed the injected renderer");
        render::GuiCommandContext commands;
        overlay->record(commands);
        layer.reconfigureRenderer();
        layer.reset();
        require(!layer && ImGui::GetCurrentContext() == nullptr, "GUI reset leaked its context");
        require(state.events == std::vector<std::string>{"platform init", "renderer init", "renderer frame",
            "platform frame", "prepare", "record", "reconfigure", "renderer shutdown", "platform shutdown"},
            "GUI lifecycle order is incorrect");
        state.events.clear();
        state.failInitialization = true;
        bool rejected = false;
        try { layer.create(config(state)); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && !layer && ImGui::GetCurrentContext() == nullptr,
            "failed backend initialization leaked the GUI context");
        require(state.events == std::vector<std::string>{"platform init", "renderer init",
            "renderer shutdown", "platform shutdown"}, "failed initialization cleanup order is incorrect");
        std::cout << "Injected GUI lifecycle tests passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
