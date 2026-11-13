#include "App.hpp"

#include "ApplicationGui.hpp"

namespace rubia::editor
{

void App::requestPresentationReconfigure()
{
    presentationReconfigureRequested_ = true;
    lastFramebufferResizeTime = platform::glfw::Window::time();
}

bool App::isPresentationReconfigureDue() const
{
    const auto extent = window.framebufferExtent();
    return extent.width > 0 &&
        extent.height > 0 &&
        platform::glfw::Window::time() - lastFramebufferResizeTime >=
            kPresentationResizeDebounceSeconds;
}

void App::reconfigurePresentation(ApplicationGui&)
{
    auto extent = window.framebufferExtent();
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
    backend_->waitIdle();
    // Renderer recreation preserves editor commands, CPU workers and preparation tickets.
    guiRenderBridge.detach();
    backend_->resize(extent);

    if (recreateImGui)
    {
        imguiLayer.reconfigureRenderer();
    }

    if (recreateImGui)
        guiRenderBridge.attach(backend_->guiHost(), renderAssets, imguiLayer.rendererBackend());

    const auto renderExtent = backend_->extent();
    camera.setAspect(
        static_cast<float>(renderExtent.width) /
        static_cast<float>(renderExtent.height));

    presentationReconfigureRequested_ = false;
}

} // namespace rubia::editor
