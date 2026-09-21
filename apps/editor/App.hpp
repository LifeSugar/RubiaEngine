#pragma once

#include "asset/AssetManager.hpp"
#include "render/Camera.hpp"
#include "content/DemoContent.hpp"
#include "content/ContentLoadStatus.hpp"
#include "ImGuiLayer.hpp"
#include "texture/TextureImportRegistry.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "scene/Scene.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/VulkanApplicationGuiRenderBridge.hpp"
#include "vulkan/VulkanRenderer.hpp"
#include "vulkan/VulkanResourcePreparationBridge.hpp"
#include "vulkan/Window.hpp"

#include <cstdint>
#include <atomic>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace rubia::test
{
class AppSmokeTests;
}

namespace rubia::editor
{

class ApplicationGui;

class App
{
public:
    struct RunConfig
    {
        uint32_t windowWidth = 1280;
        uint32_t windowHeight = 720;
        std::string windowTitle = "RubiaEngine";
        bool enableDocking = true;
        render::ResourcePreparationOptions resourcePreparation;
        std::string imguiIniFilename;
        rhi::vulkan::VulkanRenderer::OutputMode outputMode =
            rhi::vulkan::VulkanRenderer::OutputMode::Runtime;
        DemoContentLoader::CreateInfo demoContent{
            "ABeautifulGame_extracted/ABeautifulGame.gltf",
            "shaders/triangle.vert.spv",
            "shaders/triangle.frag.spv",
            "shaders/present.vert.spv",
            "shaders/present.frag.spv",
            "assets",
            DemoContentLoader::TextureImportPolicy{
                importer::texture::KtxPayloadEncoding::Uastc,
                true,
                asset::TextureFormat::BC7UNorm,
                asset::TextureFormat::BC7UNorm,
                asset::TextureFormat::BC5UNorm,
                true}};
    };

    ~App();

    void setPreferIntegratedGPU(bool enabled);

    void run();
    void run(const RunConfig& config, ApplicationGui& gui);
    // Frontend callers submit CPU handles/snapshots without a Vulkan cache or device.
    [[nodiscard]] render::ResourcePreparation& resourcePreparation() noexcept
    {
        return resourcePreparation_;
    }
private:
    struct PreparedContent
    {
        asset::AssetManager assets;
        scene::Scene scene;
        importer::texture::TextureImportRegistry textureImports;
        DemoContent content;
    };

    struct PreparedTextureReimport
    {
        importer::texture::TextureReimportRequest request;
        importer::texture::TextureImportRecord record;
        std::filesystem::path stagedPath;
        asset::TextureAsset replacementAsset;
        std::string error;
    };

#ifdef NDEBUG
    static constexpr bool kEnableValidationLayers = false;
#else
    static constexpr bool kEnableValidationLayers = true;
#endif

    rhi::vulkan::Window window;
    rhi::vulkan::VulkanContext vulkanContext;
    asset::AssetManager assetManager;
    importer::texture::TextureImportRegistry textureImports;
    DemoContent demoContent;
    scene::Scene scene;
    rhi::vulkan::RenderAssetCache renderAssets;
    rhi::vulkan::VulkanRenderer renderer;
    rhi::vulkan::VulkanResourcePreparationBridge resourcePreparation_{renderer, renderAssets};
    // Must be destroyed before the renderer, device, and GLFW window.
    ImGuiLayer imguiLayer;
    // Must release ImGui descriptors before ImGuiLayer is destroyed.
    rhi::vulkan::VulkanApplicationGuiRenderBridge guiRenderBridge;

    render::Camera camera;
    static constexpr uint32_t kMaxFramesInFlight = 2;
    ContentLoadStatus contentLoadStatus_;
    DemoContentLoader::CreateInfo contentLoadConfig_;
    std::shared_ptr<std::atomic<bool>> contentLoadCancelled_;
    std::future<std::unique_ptr<PreparedContent>> contentLoadFuture_;
    std::shared_ptr<PreparedContent> preparedContent_;
    bool preferIntegratedGpu = false;
    bool swapChainRecreationRequested = false;
    std::deque<importer::texture::TextureReimportRequest> pendingTextureReimports_;
    std::future<PreparedTextureReimport> textureReimportFuture_;
    asset::TextureAssetHandle activeTextureReimport_;
    std::filesystem::path activeTextureReimportStagedPath_;
    double lastFramebufferResizeTime = 0.0;
    static constexpr double kSwapChainResizeDebounceSeconds = 0.15;

private:
    void initWindow(const RunConfig& config, bool visible = true);
    void initVulkan(const RunConfig& config);
    void startContentLoading();
    void updateContentLoading();
    void discardContentLoading() noexcept;
    void initImGui(const RunConfig& config);
    void mainLoop(ApplicationGui& gui);
    void cleanup();
    void drawGui(ApplicationGui& gui);
    void processPendingTextureReimport();
    void discardTextureReimport() noexcept;

private:
    void setupCamera();

    void recreateSwapChain(ApplicationGui& gui);
    void requestSwapChainRecreation();
    bool isSwapChainRecreationDue() const;

    [[nodiscard]] render::RenderFrame makeRenderFrame();

    friend class rubia::test::AppSmokeTests;
};

} // namespace rubia::editor
