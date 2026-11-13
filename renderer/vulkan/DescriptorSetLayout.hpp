#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{
/// Immutable owning handle and value key, captured directly at creation.
class DescriptorSetLayoutState final
{
  public:
    ~DescriptorSetLayoutState();
    DescriptorSetLayoutState(const DescriptorSetLayoutState &) = delete;
    DescriptorSetLayoutState &operator=(const DescriptorSetLayoutState &) = delete;
    VkDescriptorSetLayout get() const noexcept
    {
        return handle_;
    }
    VkDevice device() const noexcept
    {
        return device_;
    }
    const std::vector<VkDescriptorSetLayoutBinding>& bindings() const noexcept { return bindings_; }
    const std::vector<uint32_t> &key() const noexcept
    {
        return key_;
    }

  private:
    friend class DescriptorSetLayout;
    DescriptorSetLayoutState() = default;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout handle_ = VK_NULL_HANDLE;
    std::vector<uint32_t> key_;
    std::vector<VkDescriptorSetLayoutBinding> bindings_;
};

/// Owner facade; reference() keeps an immutable version alive across reset/move.
/// The VkDevice must outlive the facade and every outstanding reference.
class DescriptorSetLayout final
{
  public:
    DescriptorSetLayout() = default;
    DescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings,
                        VkDescriptorSetLayoutCreateFlags flags = 0);
    ~DescriptorSetLayout();
    DescriptorSetLayout(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout(DescriptorSetLayout &&other) noexcept;
    DescriptorSetLayout &operator=(DescriptorSetLayout &&other) noexcept;
    void create(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings,
                VkDescriptorSetLayoutCreateFlags flags = 0);
    void reset() noexcept;
    VkDescriptorSetLayout get() const noexcept
    {
        return state_ ? state_->get() : VK_NULL_HANDLE;
    }
    std::shared_ptr<const DescriptorSetLayoutState> reference() const noexcept
    {
        return state_;
    }
    explicit operator bool() const noexcept
    {
        return bool(state_);
    }

  private:
    std::shared_ptr<const DescriptorSetLayoutState> state_;
};
} // namespace rubia::rhi::vulkan
