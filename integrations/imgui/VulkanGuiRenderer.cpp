#include "imgui/VulkanGuiRenderer.hpp"

#include "vulkan/VulkanGuiCommandContext.hpp"

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
    initialized_ = true;
}

void VulkanGuiRenderer::shutdown() noexcept
{
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
