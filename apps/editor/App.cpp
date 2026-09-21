#include "App.hpp"

#include "ApplicationGui.hpp"
#include "content/DemoContent.hpp"
#include "render/RenderFrameBuilder.hpp"
#include "RuntimeGui.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rubia::editor
{

App::~App()
{
    resourcePreparation_.cancelAll();
    discardContentLoading();
    discardTextureReimport();
    // Destruction after an exception must not release resources still in use by
    // the GPU. Member destruction then proceeds in reverse dependency order.
    vulkanContext.waitIdle();
}

void App::setPreferIntegratedGPU(bool enabled)
{
    preferIntegratedGpu = enabled;
}

void App::run()
{
    RuntimeGui gui;
    run(RunConfig{}, gui);
}

void App::run(const RunConfig& config, ApplicationGui& gui)
{
    bool guiAttached = false;
    try
    {
        initWindow(config, true);
        initVulkan(config);
        if (config.initializeEmptyScene) startEmptyScene(config);
        setupCamera();
        initImGui(config);
        guiRenderBridge.attach(renderer, renderAssets);
        ApplicationGuiContext guiContext{
            assetManager, scene, guiRenderBridge, &textureImports, &contentLoadStatus_,
            &resourcePreparation_, demoContent.defaultMaterial};
        gui.attach(guiContext);
        guiAttached = true;
        mainLoop(gui);
    }
    catch (...)
    {
        discardContentLoading();
        renderer.waitIdle();
        if (guiAttached) gui.detach();
        cleanup();
        throw;
    }
    discardContentLoading();
    renderer.waitIdle();
    gui.detach();
    cleanup();
}

void App::initWindow(const RunConfig& config, bool visible)
{
    if (config.windowWidth == 0 || config.windowHeight == 0 ||
        config.windowTitle.empty())
    {
        throw std::invalid_argument("App run config contains an invalid window");
    }

    rhi::vulkan::Window::CreateInfo createInfo{};
    createInfo.width = config.windowWidth;
    createInfo.height = config.windowHeight;
    createInfo.title = config.windowTitle;
    createInfo.visible = visible;
    window.create(createInfo);
}

void App::initVulkan(const RunConfig& config)
{
    rhi::vulkan::VulkanContext::CreateInfo contextCreateInfo{};
    contextCreateInfo.enableValidationLayers = kEnableValidationLayers;
    contextCreateInfo.preferIntegratedGpu = preferIntegratedGpu;
    vulkanContext.create(window, contextCreateInfo);

    rhi::vulkan::VulkanRenderer::CreateInfo rendererCreateInfo{};
    rendererCreateInfo.context = &vulkanContext;
    rendererCreateInfo.framebufferExtent = window.framebufferExtent();
    rendererCreateInfo.framesInFlight = kMaxFramesInFlight;
    rendererCreateInfo.outputMode = config.outputMode;
    rendererCreateInfo.resourcePreparation = config.resourcePreparation;
    renderer.createPresentation(rendererCreateInfo);

}

void App::cleanup()
{
    resourcePreparation_.cancelAll();
    discardContentLoading();
    contentLoadStatus_ = {};
    discardTextureReimport();
    renderer.waitIdle();
    guiRenderBridge.detach();
    imguiLayer.reset();
    renderer.reset();
    renderAssets.reset();
    scene.reset();
    textureImports.reset();
    assetManager.reset();
    demoContent = {};
    vulkanContext.reset();
    window.reset();
}

void App::initImGui(const RunConfig& config)
{
    ImGuiLayer::CreateInfo createInfo{};
    createInfo.window = &window;
    createInfo.context = &vulkanContext;
    createInfo.renderPass = renderer.presentRenderPass();
    createInfo.minImageCount = 2;
    createInfo.imageCount = renderer.swapchainImageCount();
    createInfo.enableDocking = config.enableDocking;
    createInfo.iniFilename = config.imguiIniFilename;
    imguiLayer.create(createInfo);
}

void App::setupCamera()
{
    camera.setPosition(glm::vec3(0.0f, 1.0f, 0.5f));
    camera.setRotation(glm::vec3(-60.0f, 0.0f, 0.0f));
    const VkExtent2D extent = renderer.extent();
    camera.setAspect(
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height));
}

void App::mainLoop(ApplicationGui& gui)
{
    while (!window.shouldClose())
    {
        window.pollEvents();

        if (window.consumeFramebufferResize())
        {
            requestSwapChainRecreation();
        }

        if (swapChainRecreationRequested)
        {
            if (!isSwapChainRecreationDue())
            {
                // During an interactive resize, avoid repeatedly destroying and
                // recreating GPU resources.
                window.waitEventsTimeout(0.016);
                continue;
            }

            recreateSwapChain(gui);
        }

        resourcePreparation_.advance();
        updateContentLoading();
        processPendingTextureReimport();
        gui.update({assetManager, scene, guiRenderBridge, &textureImports,
                    &contentLoadStatus_, &resourcePreparation_, demoContent.defaultMaterial});

        imguiLayer.beginFrame();
        drawGui(gui);
        ImDrawData* uiDrawData = imguiLayer.endFrame();

        const rhi::vulkan::VulkanRenderer::RenderResult renderResult =
            renderer.sceneReady()
                ? renderer.render(makeRenderFrame(), renderAssets, uiDrawData)
                : renderer.renderGui(uiDrawData);
        if (renderResult == rhi::vulkan::VulkanRenderer::RenderResult::NeedsResize)
        {
            requestSwapChainRecreation();
        }
    }
}

void App::drawGui(ApplicationGui& gui)
{
    ApplicationGuiContext context{
        assetManager,
        scene,
        guiRenderBridge,
        &textureImports,
        &contentLoadStatus_,
        &resourcePreparation_, demoContent.defaultMaterial};
    const ApplicationGuiFrameOutput output = gui.draw(context);
    for (const importer::texture::TextureReimportRequest& request : output.textureReimports)
    {
        const bool alreadyQueued = std::any_of(
            pendingTextureReimports_.begin(),
            pendingTextureReimports_.end(),
            [&](const importer::texture::TextureReimportRequest& queued)
            {
                return queued.texture == request.texture;
            });
        if (!alreadyQueued && activeTextureReimport_ != request.texture)
        {
            pendingTextureReimports_.push_back(request);
        }
    }
    if (output.sceneAspectRatio)
    {
        const float aspect = *output.sceneAspectRatio;
        if (!std::isfinite(aspect) || aspect <= 0.0f)
        {
            throw std::invalid_argument(
                "Application GUI returned an invalid scene aspect ratio");
        }
        camera.setAspect(aspect);
    }
    if (output.sceneFocus && output.sceneFocus->valid())
    {
        const auto& bounds = *output.sceneFocus;
        const float radius = std::max(0.01f, glm::length(bounds.extents()));
        auto config = camera.getConfig();
        const float halfVertical = glm::radians(config.fov * 0.5f);
        const float halfHorizontal = std::atan(std::tan(halfVertical) * config.aspectRatio);
        const float distance = radius * 1.2f / std::sin(std::min(halfVertical, halfHorizontal));
        config.nearPlane = std::max(0.001f, radius * 0.001f);
        config.farPlane = std::max(100.0f, distance + radius * 4.0f);
        camera.setConfig(config);
        camera.setRotation(glm::vec3(-20.0f, 30.0f, 0.0f));
        camera.setPosition(bounds.center() - camera.getForwardVector() * distance);
    }
}

render::RenderFrame App::makeRenderFrame()
{
    camera.Update();
    return buildRenderFrame(
        scene,
        assetManager,
        camera.makeRenderView());
}

} // namespace rubia::editor
