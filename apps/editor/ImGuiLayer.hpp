#pragma once

#include "EditorTheme.hpp"
#include "imgui/GuiBackend.hpp"


#include <cstdint>
#include <string>

struct ImGuiContext;

namespace rubia::editor
{

/// Owns the Dear ImGui context and injected platform/rendering backends.
class ImGuiLayer final
{
public:
    struct CreateInfo
    {
        std::unique_ptr<integration::imgui::GuiPlatformBackend> platform;
        std::unique_ptr<integration::imgui::GuiRendererBackend> renderer;
        bool enableDocking = true;
        std::string iniFilename;
    };

    ImGuiLayer() = default;
    explicit ImGuiLayer(CreateInfo createInfo);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;
    ImGuiLayer(ImGuiLayer&&) = delete;
    ImGuiLayer& operator=(ImGuiLayer&&) = delete;

    void create(CreateInfo createInfo);
    void reset() noexcept;

    /// Starts one Dear ImGui frame after window events have been processed.
    void beginFrame();
    /// Finalizes and prepares the UI; the borrowed overlay lasts until next frame.
    [[nodiscard]] const render::GuiOverlay* endFrame();

    /// Caller has completed GPU work and drained GUI texture registrations.
    void reconfigureRenderer();
    integration::imgui::GuiRendererBackend& rendererBackend();

    [[nodiscard]] explicit operator bool() const noexcept;

private:
    void makeContextCurrent() const noexcept;

    ImGuiContext* imguiContext_ = nullptr;
    EditorThemeSettings themeSettings_;
    std::string iniFilename_;
    bool platformBackendInitialized_ = false;
    std::unique_ptr<integration::imgui::GuiPlatformBackend> platformBackend_;
    std::unique_ptr<integration::imgui::GuiRendererBackend> rendererBackend_;
};

} // namespace rubia::editor
