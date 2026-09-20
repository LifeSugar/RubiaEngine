#include "ImGuiLayer.hpp"
#include "EditorTheme.hpp"

#include "vulkan/VulkanContext.hpp"
#include "vulkan/Window.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace rubia::editor
{
namespace
{

constexpr uint32_t kImGuiDescriptorCapacity = 64;

void loadEditorFont(ImGuiIO& io)
{
    constexpr float fontSize = 16.0f;
    const std::filesystem::path relativePath =
        "assets/fonts/FiraCode/FiraCode-Regular.ttf";
    const std::filesystem::path candidates[] = {
        relativePath,
        std::filesystem::path(PROJECT_SOURCE_DIR) / relativePath};

    ImFontConfig fontConfig;
    fontConfig.Flags |= ImFontFlags_NoLoadError;
    for (const auto& path : candidates)
    {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(
                path.u8string().c_str(), fontSize, &fontConfig))
        {
            io.FontDefault = font;
            return;
        }
    }

    std::cerr << "[ImGui] Could not load Fira Code; using the default font\n";
    fontConfig.SizePixels = fontSize;
    io.FontDefault = io.Fonts->AddFontDefault(&fontConfig);
}

void reportVulkanResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        std::cerr
            << "[ImGui][Vulkan] VkResult = "
            << static_cast<int>(result)
            << '\n';
    }
}

} // namespace

ImGuiLayer::ImGuiLayer(const CreateInfo& createInfo)
{
    create(createInfo);
}

ImGuiLayer::~ImGuiLayer()
{
    reset();
}

void ImGuiLayer::create(const CreateInfo& createInfo)
{
    if (createInfo.window == nullptr || !*createInfo.window ||
        createInfo.context == nullptr || !*createInfo.context ||
        createInfo.renderPass == VK_NULL_HANDLE ||
        createInfo.minImageCount < 2 ||
        createInfo.imageCount < createInfo.minImageCount)
    {
        throw std::invalid_argument("ImGuiLayer create info is incomplete");
    }

    reset();
    context_ = createInfo.context;
    minImageCount_ = createInfo.minImageCount;
    iniFilename_ = createInfo.iniFilename;

    try
    {
        IMGUI_CHECKVERSION();
        imguiContext_ = ImGui::CreateContext();
        makeContextCurrent();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (createInfo.enableDocking)
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        }
        io.IniFilename = iniFilename_.empty()
            ? nullptr
            : iniFilename_.c_str();
        applyEditorTheme(themeSettings_);
        loadEditorFont(io);

        if (!ImGui_ImplGlfw_InitForVulkan(
                createInfo.window->nativeHandle(),
                true))
        {
            throw std::runtime_error(
                "failed to initialize Dear ImGui GLFW backend");
        }
        platformBackendInitialized_ = true;

        const float contentScale =
            ImGui_ImplGlfw_GetContentScaleForWindow(
                createInfo.window->nativeHandle());
        if (contentScale > 0.0f && contentScale != 1.0f)
        {
            ImGui::GetStyle().ScaleAllSizes(contentScale);
            ImGui::GetStyle().FontScaleDpi = contentScale;
        }

        initializeRendererBackend(
            createInfo.renderPass,
            createInfo.imageCount);
    }
    catch (...)
    {
        reset();
        throw;
    }
}

void ImGuiLayer::reset() noexcept
{
    makeContextCurrent();
    shutdownRendererBackend();

    if (platformBackendInitialized_)
    {
        ImGui_ImplGlfw_Shutdown();
        platformBackendInitialized_ = false;
    }
    if (imguiContext_ != nullptr)
    {
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;
    }

    context_ = nullptr;
    iniFilename_.clear();
    minImageCount_ = 2;
}

void ImGuiLayer::beginFrame()
{
    if (!*this)
    {
        throw std::logic_error("cannot begin an uninitialized ImGui frame");
    }

    makeContextCurrent();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

ImDrawData* ImGuiLayer::endFrame()
{
    if (!*this)
    {
        throw std::logic_error("cannot end an uninitialized ImGui frame");
    }

    makeContextCurrent();
    ImGui::Render();
    return ImGui::GetDrawData();
}

void ImGuiLayer::shutdownRendererBackend() noexcept
{
    makeContextCurrent();
    if (rendererBackendInitialized_)
    {
        ImGui_ImplVulkan_Shutdown();
        rendererBackendInitialized_ = false;
    }
}

void ImGuiLayer::initializeRendererBackend(
    VkRenderPass renderPass,
    uint32_t imageCount)
{
    if (imguiContext_ == nullptr || context_ == nullptr || !*context_ ||
        !platformBackendInitialized_ || rendererBackendInitialized_ ||
        renderPass == VK_NULL_HANDLE || imageCount < minImageCount_)
    {
        throw std::logic_error(
            "cannot initialize Dear ImGui Vulkan backend");
    }

    makeContextCurrent();
    const rhi::vulkan::Device& device = context_->device();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = context_->instance();
    initInfo.PhysicalDevice = device.physical();
    initInfo.Device = device.get();
    initInfo.QueueFamily = device.graphicsQueueFamily();
    initInfo.Queue = device.graphicsQueue();
    initInfo.DescriptorPoolSize = kImGuiDescriptorCapacity;
    initInfo.MinImageCount = minImageCount_;
    initInfo.ImageCount = imageCount;
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = reportVulkanResult;
    initInfo.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error(
            "failed to initialize Dear ImGui Vulkan backend");
    }
    rendererBackendInitialized_ = true;
}

void ImGuiLayer::recreateRendererPipeline(VkRenderPass renderPass)
{
    if (!*this || renderPass == VK_NULL_HANDLE)
    {
        throw std::logic_error(
            "cannot recreate Dear ImGui Vulkan pipeline");
    }

    makeContextCurrent();
    ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
    pipelineInfo.RenderPass = renderPass;
    pipelineInfo.Subpass = 0;
    pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
}

ImGuiLayer::operator bool() const noexcept
{
    return context_ != nullptr && imguiContext_ != nullptr &&
        platformBackendInitialized_ && rendererBackendInitialized_;
}

void ImGuiLayer::makeContextCurrent() const noexcept
{
    if (imguiContext_ != nullptr)
    {
        ImGui::SetCurrentContext(imguiContext_);
    }
}

} // namespace rubia::editor
