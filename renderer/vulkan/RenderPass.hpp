#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{
/// Immutable owning handle and value key, captured directly at creation.
class RenderPassState final
{
  public:
    ~RenderPassState();
    RenderPassState(const RenderPassState &) = delete;
    RenderPassState &operator=(const RenderPassState &) = delete;
    VkRenderPass get() const noexcept
    {
        return handle_;
    }
    VkDevice device() const noexcept
    {
        return device_;
    }
    const std::vector<uint32_t> &compatibilityKey() const noexcept
    {
        return key_;
    }
    uint32_t subpassCount() const noexcept
    {
        return static_cast<uint32_t>(colorCounts_.size());
    }
    uint32_t colorAttachmentCount(uint32_t index) const
    {
        return colorCounts_.at(index);
    }

  private:
    friend class RenderPass;
    RenderPassState() = default;
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass handle_ = VK_NULL_HANDLE;
    std::vector<uint32_t> key_;
    std::vector<uint32_t> colorCounts_;
};

/// Owner facade; reference() keeps an immutable version alive across reset/move.
/// The VkDevice must outlive the facade and every outstanding reference.
class RenderPass final
{
  public:
    RenderPass() = default;
    RenderPass(VkDevice device, const VkRenderPassCreateInfo &createInfo);
    ~RenderPass();
    RenderPass(const RenderPass &) = delete;
    RenderPass &operator=(const RenderPass &) = delete;
    RenderPass(RenderPass &&other) noexcept;
    RenderPass &operator=(RenderPass &&other) noexcept;
    void create(VkDevice device, const VkRenderPassCreateInfo &createInfo);
    void reset() noexcept;
    VkRenderPass get() const noexcept
    {
        return state_ ? state_->get() : VK_NULL_HANDLE;
    }
    std::shared_ptr<const RenderPassState> reference() const noexcept
    {
        return state_;
    }
    explicit operator bool() const noexcept
    {
        return bool(state_);
    }

  private:
    std::shared_ptr<const RenderPassState> state_;
};
} // namespace rubia::rhi::vulkan
