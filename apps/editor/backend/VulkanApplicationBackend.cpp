#include "backend/VulkanApplicationBackend.hpp"
#include "imgui/GlfwGuiPlatform.hpp"
#include "imgui/VulkanGuiRenderer.hpp"

namespace rubia::editor
{
VulkanApplicationBackend::VulkanApplicationBackend(platform::glfw::Window& window,
    const BackendConfig& config) : window_(window)
{
    rhi::vulkan::VulkanContext::CreateInfo contextInfo;
    contextInfo.enableValidationLayers = config.validation;
    contextInfo.preferIntegratedGpu = config.preferIntegratedGpu;
    context_.create(window_, contextInfo);
    const auto extent = window_.framebufferExtent();
    rhi::vulkan::VulkanRenderer::CreateInfo info;
    info.context = &context_;
    info.framebufferExtent = {extent.width, extent.height};
    info.framesInFlight = config.framesInFlight;
    info.outputMode = config.outputMode;
    info.resourcePreparation = config.resourcePreparation;
    renderer_.createPresentation(info);
}
VulkanApplicationBackend::~VulkanApplicationBackend()
{
    // Includes partial startup/exception unwinding; renderer dies before device.
    try { context_.waitIdle(); } catch (...) {}
}
platform::Extent2D VulkanApplicationBackend::extent() const noexcept
{
    const auto extent = renderer_.extent();
    return {extent.width, extent.height};
}
void VulkanApplicationBackend::resize(platform::Extent2D extent)
{
    renderer_.resize({extent.width, extent.height});
}
render::RenderResult VulkanApplicationBackend::render(const render::RenderFrame& frame,
    const render::RenderAssetStore& assets, const render::GuiOverlay* gui)
{ return renderer_.render(frame, assets, gui); }
render::RenderResult VulkanApplicationBackend::renderGui(const render::GuiOverlay* gui)
{ return renderer_.renderGui(gui); }
void VulkanApplicationBackend::beginScenePreparation(render::RenderAssetStore& assets,
    render::SceneResourceRequest request)
{ renderer_.beginScenePreparation(assets, std::move(request)); }
std::unique_ptr<integration::imgui::GuiRendererBackend> VulkanApplicationBackend::createGuiRenderer()
{ return std::make_unique<integration::imgui::VulkanGuiRenderer>(context_, renderer_); }
std::unique_ptr<integration::imgui::GuiPlatformBackend> VulkanApplicationBackend::createGuiPlatform()
{
    return integration::imgui::makeGlfwGuiPlatform(window_.nativeHandle(),
        integration::imgui::GlfwGuiClientApi::Vulkan);
}
}
