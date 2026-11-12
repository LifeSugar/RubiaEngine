#pragma once
#include "Extent2D.hpp"
#include "core/RenderResult.hpp"
#include "core/GuiRenderHost.hpp"
#include "imgui/GuiBackend.hpp"
#include "passes/FramePipeline.hpp"
#include "render/RenderFrame.hpp"
#include "render/ResourcePreparationOptions.hpp"
#include "resources/ResourcePreparationSession.hpp"

namespace rubia::editor
{
enum class GraphicsApi { Vulkan, Direct3D12, Metal, OpenGL };

struct BackendConfig
{
    render::OutputMode outputMode = render::OutputMode::Runtime;
    render::ResourcePreparationOptions resourcePreparation;
    uint32_t framesInFlight = 2;
    bool validation = false;
    bool preferIntegratedGpu = false;
};

// One application rendering session. Owns the API context/device and renderer;
// the window and resident asset store must outlive their submitted uses. GUI
// adapters and preparation sessions must be destroyed before this session.
class ApplicationBackend
{
public:
    virtual ~ApplicationBackend() = default;
    virtual bool ready() const noexcept = 0;
    virtual void waitIdle() const = 0;
    // Stop renderer/jobs while keeping the device alive for resident asset release.
    virtual void shutdown() noexcept = 0;
    virtual platform::Extent2D extent() const noexcept = 0;
    virtual void resize(platform::Extent2D) = 0;
    virtual render::RenderResult render(const render::RenderFrame&, const render::RenderAssetStore&,
        const render::GuiOverlay* = nullptr) = 0;
    virtual render::RenderResult renderGui(const render::GuiOverlay*) = 0;
    virtual bool sceneReady() const noexcept = 0;
    virtual void beginScenePreparation(render::RenderAssetStore&, render::SceneResourceRequest) = 0;
    virtual render::ScenePreparationStatus scenePreparationStatus() const = 0;
    virtual void activatePreparedScene() = 0;
    virtual void cancelScenePreparation() noexcept = 0;
    virtual render::ResourcePreparationHost& preparationHost() noexcept = 0;
    virtual render::GuiRenderHost& guiHost() noexcept = 0;
    virtual std::unique_ptr<integration::imgui::GuiRendererBackend> createGuiRenderer() = 0;
    virtual std::unique_ptr<integration::imgui::GuiPlatformBackend> createGuiPlatform() = 0;
};
}
