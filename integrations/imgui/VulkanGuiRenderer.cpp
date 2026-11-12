#include "imgui/VulkanGuiRenderer.hpp"

#include "vulkan/VulkanGuiCommandContext.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/VulkanRenderer.hpp"
#include "vulkan/VulkanResourceDevice.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace rubia::integration::imgui
{
namespace
{

void reportVulkanResult(VkResult result)
{
    if (result != VK_SUCCESS)
        std::cerr << "[ImGui][Vulkan] VkResult = " << static_cast<int>(result) << '\n';
}

bool needsTextureWork(const ImTextureData& texture, uint32_t imageCount)
{
    return texture.Status == ImTextureStatus_WantCreate ||
        texture.Status == ImTextureStatus_WantUpdates ||
        (texture.Status == ImTextureStatus_WantDestroy &&
         texture.UnusedFrames >= static_cast<int>(imageCount));
}

} // namespace

struct VulkanGuiRenderer::TextureSession
{
    ImGuiContext* context = nullptr;
    bool active = false;
};
struct VulkanGuiRenderer::TextureRegistration final : GuiTexture
{
    TextureRegistration(std::shared_ptr<TextureSession> session, rhi::TextureViewRef view)
        : session(std::move(session)), view(std::move(view))
    {
        set = ImGui_ImplVulkan_AddTexture(rhi::vulkan::nativeTextureView(this->view),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!set) throw std::runtime_error("failed to register GUI texture");
    }
    ~TextureRegistration() override
    {
        if (session->active)
        {
            auto* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(session->context);
            ImGui_ImplVulkan_RemoveTexture(set);
            ImGui::SetCurrentContext(previous);
        }
    }
    std::uintptr_t token() const noexcept override
    { return session->active ? reinterpret_cast<std::uintptr_t>(set) : 0; }
    std::shared_ptr<TextureSession> session;
    rhi::TextureViewRef view;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

VulkanGuiRenderer::VulkanGuiRenderer(const rhi::vulkan::VulkanContext& context,
    const rhi::vulkan::VulkanRenderer& renderer) : context_(context), renderer_(renderer) {}

void VulkanGuiRenderer::initialize(ImGuiContext* imguiContext)
{
    const auto& device = context_.device();
    CreateInfo info;
    info.imguiContext = imguiContext;
    info.instance = context_.instance();
    info.physicalDevice = device.physical();
    info.device = device.get();
    info.graphicsQueue = device.graphicsQueue();
    info.graphicsQueueFamily = device.graphicsQueueFamily();
    info.renderPass = renderer_.presentRenderPass();
    info.imageCount = renderer_.swapchainImageCount();
    initialize(info);
}

void VulkanGuiRenderer::reconfigure()
{
    if (!initialized_) throw std::logic_error("cannot reconfigure an uninitialized GUI backend");
    if (textures_.use_count() != 1)
        throw std::logic_error("GUI registrations must be drained before reconfiguration");
    // The official backend's draw-buffer ring also depends on image count.
    if (imageCount_ != renderer_.swapchainImageCount())
    {
        auto* context = imguiContext_;
        shutdown();
        initialize(context);
    }
    else recreatePipeline(renderer_.presentRenderPass());
}

GuiTextureRef VulkanGuiRenderer::registerTexture(const rhi::TextureViewRef& view)
{
    if (!initialized_ || !view || !view->texture())
        throw std::invalid_argument("GUI texture registration requires an active backend and view");
    if (rhi::vulkan::nativeImage(view->texture()).ownerDevice() != device_)
        throw std::invalid_argument("GUI texture belongs to a different Vulkan device");
    makeContextCurrent();
    return std::make_shared<TextureRegistration>(textures_, view);
}

VulkanGuiRenderer::~VulkanGuiRenderer()
{
    shutdown();
}

void VulkanGuiRenderer::initialize(const CreateInfo& createInfo)
{
    if (initialized_ || createInfo.imguiContext == nullptr ||
        createInfo.instance == VK_NULL_HANDLE ||
        createInfo.physicalDevice == VK_NULL_HANDLE ||
        createInfo.device == VK_NULL_HANDLE ||
        createInfo.graphicsQueue == VK_NULL_HANDLE ||
        createInfo.renderPass == VK_NULL_HANDLE || createInfo.minImageCount < 2 ||
        createInfo.imageCount < createInfo.minImageCount)
    {
        throw std::invalid_argument("ImGui Vulkan adapter create info is incomplete or already initialized");
    }

    auto textureSession = std::make_shared<TextureSession>();
    imguiContext_ = createInfo.imguiContext;
    device_ = createInfo.device;
    imageCount_ = createInfo.imageCount;
    makeContextCurrent();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = createInfo.apiVersion;
    initInfo.Instance = createInfo.instance;
    initInfo.PhysicalDevice = createInfo.physicalDevice;
    initInfo.Device = createInfo.device;
    initInfo.QueueFamily = createInfo.graphicsQueueFamily;
    initInfo.Queue = createInfo.graphicsQueue;
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = createInfo.minImageCount;
    initInfo.ImageCount = createInfo.imageCount;
    initInfo.PipelineInfoMain.RenderPass = createInfo.renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = reportVulkanResult;
    initInfo.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        imguiContext_ = nullptr;
        device_ = VK_NULL_HANDLE;
        imageCount_ = 0;
        throw std::runtime_error("failed to initialize Dear ImGui Vulkan backend");
    }
    textureSession->context = imguiContext_;
    textureSession->active = true;
    textures_ = std::move(textureSession);
    initialized_ = true;
}

void VulkanGuiRenderer::shutdown() noexcept
{
    // Invalidates escaped CPU tokens; callers still must drain GPU use first.
    if (textures_) textures_->active = false;
    textures_.reset();
    if (initialized_)
    {
        makeContextCurrent();
        ImGui_ImplVulkan_Shutdown();
    }
    initialized_ = false;
    drawData_ = nullptr;
    imguiContext_ = nullptr;
    device_ = VK_NULL_HANDLE;
    imageCount_ = 0;
}

void VulkanGuiRenderer::newFrame()
{
    if (!initialized_)
        throw std::logic_error("cannot begin an uninitialized ImGui Vulkan frame");
    drawData_ = nullptr;
    makeContextCurrent();
    ImGui_ImplVulkan_NewFrame();
}

void VulkanGuiRenderer::prepare(ImDrawData* drawData)
{
    if (!initialized_ || drawData == nullptr || !drawData->Valid)
        throw std::logic_error("cannot prepare invalid ImGui Vulkan draw data");
    drawData_ = nullptr;
    makeContextCurrent();

    if (drawData->Textures != nullptr)
    {
        bool completedGpuUse = false;
        for (ImTextureData* texture : *drawData->Textures)
        {
            if (!needsTextureWork(*texture, imageCount_))
                continue;

            if (!completedGpuUse)
            {
                // ImGui's age counter is a CPU frame count, not a GPU fence.
                // Its own upload path also submits/waits on the graphics queue.
                // Keep this compatibility work outside the active UI pass and
                // ensure old atlas images cannot be destroyed while in flight.
                if (vkDeviceWaitIdle(device_) != VK_SUCCESS)
                    throw std::runtime_error("failed to wait for ImGui texture maintenance");
                completedGpuUse = true;
            }
            ImGui_ImplVulkan_UpdateTexture(texture);
        }
    }
    drawData_ = drawData;
}

void VulkanGuiRenderer::recreatePipeline(VkRenderPass renderPass)
{
    if (!initialized_ || renderPass == VK_NULL_HANDLE)
        throw std::logic_error("cannot recreate an uninitialized ImGui Vulkan pipeline");
    makeContextCurrent();
    ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
    pipelineInfo.RenderPass = renderPass;
    pipelineInfo.Subpass = 0;
    pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
}

void VulkanGuiRenderer::record(render::GuiCommandContext& context) const
{
    const auto* vulkan = dynamic_cast<const rhi::vulkan::VulkanGuiCommandContext*>(&context);
    if (!initialized_ || drawData_ == nullptr || !drawData_->Valid ||
        vulkan == nullptr || vulkan->commandBuffer == VK_NULL_HANDLE)
    {
        throw std::logic_error("ImGui Vulkan overlay requires prepared data and a Vulkan UI pass");
    }
    makeContextCurrent();

    // The official backend otherwise submits texture uploads from RenderDrawData.
    // prepare() already handled them at the explicit pre-recording boundary.
    struct RestoreTextures
    {
        ImDrawData& drawData;
        ImVector<ImTextureData*>* textures;
        ~RestoreTextures() { drawData.Textures = textures; }
    } restore{*drawData_, std::exchange(drawData_->Textures, nullptr)};
    ImGui_ImplVulkan_RenderDrawData(drawData_, vulkan->commandBuffer);
}

void VulkanGuiRenderer::makeContextCurrent() const noexcept
{
    if (imguiContext_ != nullptr)
        ImGui::SetCurrentContext(imguiContext_);
}

} // namespace rubia::integration::imgui
