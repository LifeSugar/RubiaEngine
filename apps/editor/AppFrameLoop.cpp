#include "App.hpp"
#include "ApplicationGui.hpp"

namespace rubia::editor
{
void App::mainLoop(ApplicationGui& gui)
{
    while (!window.shouldClose())
    {
        window.pollEvents();
        if (window.shouldClose()) break;

        if (window.consumeFramebufferResize())
        {
            requestPresentationReconfigure();
        }

        if (presentationReconfigureRequested_)
        {
            if (!isPresentationReconfigureDue())
            {
                // During an interactive resize, avoid repeatedly destroying and
                // recreating GPU resources.
                window.waitEventsTimeout(0.016);
                continue;
            }

            reconfigurePresentation(gui);
            if (window.shouldClose()) break;
        }

        resourcePreparation_->advance();
        updateContentLoading();
        processPendingTextureReimport();
        gui.update({assetManager, scene, guiRenderBridge, &textureImports,
                    &contentLoadStatus_, resourcePreparation_.get(), demoContent.defaultMaterial});

        imguiLayer.beginFrame();
        drawGui(gui);
        const render::GuiOverlay* uiDrawData = imguiLayer.endFrame();

        const render::RenderResult renderResult =
            backend_->sceneReady()
                ? backend_->render(makeRenderFrame(), renderAssets, uiDrawData)
                : backend_->renderGui(uiDrawData);
        if (render::needsResize(renderResult))
        {
            requestPresentationReconfigure();
        }
    }
}

}
