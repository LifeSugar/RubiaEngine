#include "App.hpp"
#include "backend/VulkanApplicationBackend.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "EditorIconData.hpp"
#include "texture/StbImageDecoder.hpp"

#include "ApplicationGui.hpp"
#include "content/DemoContent.hpp"
#include "render/RenderFrameBuilder.hpp"
#include "RuntimeGui.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace rubia::editor
{

App::~App()
{
    cleanup();
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
        initBackend(config);
        if (config.initializeEmptyScene) startEmptyScene(config);
        setupCamera();
        initImGui(config);
        guiRenderBridge.attach(backend_->guiHost(), renderAssets, imguiLayer.rendererBackend());
        ApplicationGuiContext guiContext{
            assetManager, scene, guiRenderBridge, &textureImports, &contentLoadStatus_,
            resourcePreparation_.get(), demoContent.defaultMaterial};
        gui.attach(guiContext);
        guiAttached = true;
        mainLoop(gui);
    }
    catch (...)
    {
        discardContentLoading();
        if (backend_) backend_->waitIdle();
        if (guiAttached) gui.detach();
        cleanup();
        throw;
    }
    discardContentLoading();
    if (backend_) backend_->waitIdle();
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

    platform::glfw::Window::CreateInfo createInfo{};
    createInfo.width = config.windowWidth;
    createInfo.height = config.windowHeight;
    createInfo.title = config.windowTitle;
    createInfo.visible = visible;
    window.create(createInfo);
    auto icon = importer::texture::StbImageDecoder{}.decodeMemory(
        {std::begin(kEditorIconPng), std::end(kEditorIconPng)}, "Rubia icon");
    GLFWimage windowIcon{static_cast<int>(icon.width), static_cast<int>(icon.height),
        reinterpret_cast<unsigned char*>(icon.payload.data())};
    glfwSetWindowIcon(window.nativeHandle(), 1, &windowIcon);
}

void App::initBackend(const RunConfig& config)
{
    if (backend_) throw std::logic_error("application backend is already initialized");
    BackendConfig backendConfig;
    backendConfig.outputMode = config.outputMode;
    backendConfig.resourcePreparation = config.resourcePreparation;
    backendConfig.framesInFlight = kMaxFramesInFlight;
    backendConfig.validation = kEnableValidationLayers;
    backendConfig.preferIntegratedGpu = preferIntegratedGpu;
    // The application composition root is the only place selecting an API.
    switch (config.graphicsApi)
    {
    case GraphicsApi::Vulkan:
        backend_ = std::make_unique<VulkanApplicationBackend>(window, backendConfig);
        break;
    case GraphicsApi::Direct3D12:
        throw std::runtime_error("Direct3D12 application backend is not implemented");
    case GraphicsApi::Metal:
        throw std::runtime_error("Metal application backend is not implemented");
    case GraphicsApi::OpenGL:
        throw std::runtime_error("OpenGL application backend is not implemented");
    default:
        throw std::invalid_argument("unknown graphics API");
    }
    resourcePreparation_ = std::make_unique<render::ResourcePreparationSession>(
        backend_->preparationHost(), renderAssets);
}

void App::cleanup()
{
    if (resourcePreparation_) resourcePreparation_->cancelAll();
    discardContentLoading();
    contentLoadStatus_ = {};
    discardTextureReimport();
    if (backend_) backend_->waitIdle();
    guiRenderBridge.detach();
    imguiLayer.reset();
    resourcePreparation_.reset();
    if (backend_) backend_->shutdown();
    renderAssets.reset();
    scene.reset();
    textureImports.reset();
    assetManager.reset();
    demoContent = {};
    backend_.reset();
    window.reset();
    presentationReconfigureRequested_ = false;
    lastFramebufferResizeTime = 0;
}

void App::initImGui(const RunConfig& config)
{
    ImGuiLayer::CreateInfo createInfo{};
    if (!backend_) throw std::logic_error("GUI creation requires an application backend");
    createInfo.platform = backend_->createGuiPlatform();
    createInfo.renderer = backend_->createGuiRenderer();
    createInfo.enableDocking = config.enableDocking;
    createInfo.iniFilename = config.imguiIniFilename;
    imguiLayer.create(std::move(createInfo));
}

void App::setupCamera()
{
    camera.setPosition(glm::vec3(0.0f, 1.0f, 0.5f));
    camera.setRotation(glm::vec3(-60.0f, 0.0f, 0.0f));
    const auto extent = backend_->extent();
    camera.setAspect(
        static_cast<float>(extent.width) /
        static_cast<float>(extent.height));
}

void App::drawGui(ApplicationGui& gui)
{
    ApplicationGuiContext context{
        assetManager,
        scene,
        guiRenderBridge,
        &textureImports,
        &contentLoadStatus_,
        resourcePreparation_.get(), demoContent.defaultMaterial};
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
