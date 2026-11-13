#pragma once

#include "imgui/GuiBackend.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

struct ImDrawData;
struct ImGuiContext;

namespace rubia::rhi::vulkan { class VulkanContext; class VulkanRenderer; }

namespace rubia::integration::imgui
{

/// Owns the ImGui Vulkan backend; the ImGui context and device are borrowed.
class VulkanGuiRenderer final : public GuiRendererBackend
{
public:
    struct CreateInfo
    {
        ImGuiContext* imguiContext = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsQueueFamily = 0;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t minImageCount = 2;
        uint32_t imageCount = 0;
        uint32_t apiVersion = VK_API_VERSION_1_3;
    };

    VulkanGuiRenderer(const rhi::vulkan::VulkanContext&, const rhi::vulkan::VulkanRenderer&);
    ~VulkanGuiRenderer() override;
    VulkanGuiRenderer(const VulkanGuiRenderer&) = delete;
    VulkanGuiRenderer& operator=(const VulkanGuiRenderer&) = delete;
    VulkanGuiRenderer(VulkanGuiRenderer&&) = delete;
    VulkanGuiRenderer& operator=(VulkanGuiRenderer&&) = delete;

    void initialize(ImGuiContext*) override;
    void reconfigure() override;
    GuiTextureRef registerTexture(const rhi::TextureViewRef&) override;
    bool ready() const noexcept override { return initialized_; }
    /// All GPU use and external GUI registrations must have been drained first.
    void shutdown() noexcept override;
    void newFrame() override;
    /// Called after ImGui::Render, outside command recording. Dynamic texture
    /// work uses an explicit blocking compatibility path before exposing commands.
    void prepare(ImDrawData* drawData) override;
    void record(render::GuiCommandContext& context) const override;

    [[nodiscard]] explicit operator bool() const noexcept { return initialized_; }

private:
    struct TextureSession;
    struct TextureRegistration;
    void initialize(const CreateInfo&);
    void recreatePipeline(VkRenderPass);
    const rhi::vulkan::VulkanContext& context_;
    const rhi::vulkan::VulkanRenderer& renderer_;
    std::shared_ptr<TextureSession> textures_;
    void makeContextCurrent() const noexcept;

    ImGuiContext* imguiContext_ = nullptr;
    ImDrawData* drawData_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t imageCount_ = 0;
    bool initialized_ = false;
};

} // namespace rubia::integration::imgui
