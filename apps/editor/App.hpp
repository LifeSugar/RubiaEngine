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
#include "imgui/ApplicationGuiRenderBridge.hpp"
#include "imgui/VulkanGuiRenderHost.hpp"
#include "vulkan/VulkanRenderer.hpp"
#include "resources/ResourcePreparationSession.hpp"
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
        bool initializeEmptyScene = false;
        render::ResourcePreparationOptions resourcePreparation;
        std::string imguiIniFilename;
        rhi::vulkan::VulkanRenderer::OutputMode outputMode =
            rhi::vulkan::VulkanRenderer::OutputMode::Runtime;
        DemoContentLoader::CreateInfo demoContent{
            "ABeautifulGame_extracted/ABeautifulGame.gltf",
            "shaders/triangle.vert.hlsl",
            "shaders/triangle.frag.hlsl",
            "shaders/present.vert.hlsl",
            "shaders/present.frag.hlsl",
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
        asset::AssetVersion<asset::TextureAsset> baseVersion;
        asset::AssetDomainId domain;
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
    integration::imgui::VulkanGuiRenderHost guiRenderHost{renderer};
    render::ResourcePreparationSession resourcePreparation_{renderer, renderAssets};
    // Must be destroyed before the renderer, device, and GLFW window.
    ImGuiLayer imguiLayer;
    // Must release ImGui descriptors before ImGuiLayer is destroyed.
    integration::imgui::ApplicationGuiRenderBridge guiRenderBridge;

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
    struct TextureReimportTransaction;
    std::shared_ptr<TextureReimportTransaction> textureReimportTransaction_;
    std::future<PreparedTextureReimport> textureReimportFuture_;
    asset::TextureAssetHandle activeTextureReimport_;
    std::filesystem::path activeTextureReimportStagedPath_;
    double lastFramebufferResizeTime = 0.0;
    static constexpr double kSwapChainResizeDebounceSeconds = 0.15;

private:
    void initWindow(const RunConfig& config, bool visible = true);
    void initVulkan(const RunConfig& config);
    void startEmptyScene(const RunConfig& config);
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
