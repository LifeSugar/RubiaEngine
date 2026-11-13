#pragma once
#include "core/GuiRenderHost.hpp"
#include "vulkan/VulkanRenderer.hpp"

namespace rubia::integration::imgui
{
class VulkanGuiRenderHost final : public render::GuiRenderHost
{
public:
    explicit VulkanGuiRenderHost(rhi::vulkan::VulkanRenderer& renderer) : renderer_(renderer) {}
    bool hasViewport() const noexcept override
    { return renderer_.sceneReady() && renderer_.outputMode() == render::OutputMode::Editor; }
    render::TextureOutput::Extent outputExtent() const noexcept override
    { const auto extent = renderer_.extent(); return {extent.width, extent.height}; }
    uint32_t currentFrameIndex() const noexcept override { return renderer_.currentFrameIndex(); }
    uint32_t frameCount() const noexcept override { return renderer_.frameCount(); }
    render::TextureOutput viewportOutput(uint32_t slot) const override { return renderer_.editorViewportOutput(slot); }
    void resizeViewport(uint32_t width, uint32_t height) override { renderer_.resizeEditorViewport({width, height}); }
    void waitIdle() const override { renderer_.waitIdle(); }
    void retireGuiResource(std::shared_ptr<const void> owner) override { renderer_.retireExternalResource(std::move(owner)); }
    void drainGuiResources() override { renderer_.drainRetiredResources(); }
private:
    rhi::vulkan::VulkanRenderer& renderer_;
};
}
