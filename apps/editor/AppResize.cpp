#include "App.hpp"

#include "ApplicationGui.hpp"

namespace rubia::editor
{

void App::requestSwapChainRecreation()
{
    swapChainRecreationRequested = true;
    lastFramebufferResizeTime = rhi::vulkan::Window::time();
}

bool App::isSwapChainRecreationDue() const
{
    const VkExtent2D extent = window.framebufferExtent();
    return extent.width > 0 &&
        extent.height > 0 &&
        rhi::vulkan::Window::time() - lastFramebufferResizeTime >=
            kSwapChainResizeDebounceSeconds;
}

void App::recreateSwapChain(ApplicationGui&)
{
    VkExtent2D extent = window.framebufferExtent();
    while (extent.width == 0 || extent.height == 0)
    {
        if (window.shouldClose())
        {
            return;
        }
        window.waitEvents();
        extent = window.framebufferExtent();
    }

    const bool recreateImGui = static_cast<bool>(imguiLayer);
    renderer.waitIdle();
    // Renderer recreation preserves editor commands, CPU workers and preparation tickets.
    guiRenderBridge.detach();
    renderer.resize(extent);

    if (recreateImGui)
    {
        imguiLayer.recreateRendererPipeline(
            renderer.presentRenderPass());
    }

    guiRenderBridge.attach(renderer, renderAssets);

    const VkExtent2D renderExtent = renderer.extent();
    camera.setAspect(
        static_cast<float>(renderExtent.width) /
        static_cast<float>(renderExtent.height));

    swapChainRecreationRequested = false;
}

} // namespace rubia::editor
