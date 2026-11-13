#pragma once
#include "backend/ApplicationBackend.hpp"
#include "glfw/Window.hpp"
#include "imgui/VulkanGuiRenderHost.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/VulkanRenderer.hpp"

namespace rubia::editor
{
// Selected only at the application composition boundary. Native accessors are
// for backend-specific diagnostics/tests, never daily application operations.
class VulkanApplicationBackend final : public ApplicationBackend
{
public:
    VulkanApplicationBackend(platform::glfw::Window&, const BackendConfig&);
    ~VulkanApplicationBackend() override;
    ShaderTarget shaderTarget() const override { return {ShaderBackend::Vulkan, "spirv_1_3"}; }
    bool ready() const noexcept override { return static_cast<bool>(renderer_); }
    void waitIdle() const override { renderer_.waitIdle(); }
    void shutdown() noexcept override { renderer_.reset(); }
    platform::Extent2D extent() const noexcept override;
    void resize(platform::Extent2D) override;
    render::RenderResult render(const render::RenderFrame&, const render::RenderAssetStore&,
        const render::GuiOverlay*) override;
    render::RenderResult renderGui(const render::GuiOverlay*) override;
    bool sceneReady() const noexcept override { return renderer_.sceneReady(); }
    void beginScenePreparation(render::RenderAssetStore&, render::SceneResourceRequest) override;
    render::ScenePreparationStatus scenePreparationStatus() const override { return renderer_.scenePreparationStatus(); }
    void activatePreparedScene() override { renderer_.activatePreparedScene(); }
    void cancelScenePreparation() noexcept override { renderer_.cancelScenePreparation(); }
    render::ResourcePreparationHost& preparationHost() noexcept override { return renderer_; }
    render::GuiRenderHost& guiHost() noexcept override { return guiHost_; }
    std::unique_ptr<integration::imgui::GuiRendererBackend> createGuiRenderer() override;
    std::unique_ptr<integration::imgui::GuiPlatformBackend> createGuiPlatform() override;

    rhi::vulkan::VulkanContext& context() noexcept { return context_; }
    rhi::vulkan::VulkanRenderer& renderer() noexcept { return renderer_; }
private:
    platform::glfw::Window& window_;
    rhi::vulkan::VulkanContext context_;
    rhi::vulkan::VulkanRenderer renderer_;
    integration::imgui::VulkanGuiRenderHost guiHost_{renderer_};
};
}
