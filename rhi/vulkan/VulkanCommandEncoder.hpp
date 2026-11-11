#pragma once
#include "rhi/CommandEncoder.hpp"
#include "VulkanRenderEncoder.hpp"
#include "vulkan/RenderPass.hpp"

namespace rubia::rhi::vulkan
{
class VulkanPassTarget final : public rhi::PassTarget
{
public:
    VulkanPassTarget(DeviceId device, std::shared_ptr<const RenderPassState> pass,
        VkFramebuffer framebuffer, VkExtent2D extent)
        : device_(device), pass_(std::move(pass)), framebuffer_(framebuffer), extent_(extent) {}
    DeviceId deviceId() const noexcept override { return device_; }
    uint32_t width() const noexcept override { return extent_.width; }
    uint32_t height() const noexcept override { return extent_.height; }
    VkRenderPass pass() const noexcept { return pass_ ? pass_->get() : VK_NULL_HANDLE; }
    const std::shared_ptr<const RenderPassState>& passReference() const noexcept { return pass_; }
    VkFramebuffer framebuffer() const noexcept { return framebuffer_; }
private:
    DeviceId device_;
    std::shared_ptr<const RenderPassState> pass_;
    VkFramebuffer framebuffer_;
    VkExtent2D extent_;
};
class VulkanCommandEncoder final : public rhi::CommandEncoder
{
public:
    VulkanCommandEncoder(DeviceId device, VkCommandBuffer commands)
        : device_(device), commands_(commands), render_(device, commands) {}
    RenderEncoder& beginPass(const RenderPassDesc&) override;
    void endPass() override;
    void transition(const TextureRef&, const SubresourceRange&, ResourceAccess, ResourceAccess) override;
private:
    DeviceId device_;
    VkCommandBuffer commands_;
    VulkanRenderEncoder render_;
    bool activePass_ = false;
};
} // namespace rubia::rhi::vulkan
