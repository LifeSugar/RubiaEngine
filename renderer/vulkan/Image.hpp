#pragma once

#include "vulkan/VulkanMemoryConfig.hpp"

#include <vulkan/vulkan.h>
#if VK_RENDERER_USE_VMA
#include <vk_mem_alloc.h>
#endif

namespace rubia::rhi::vulkan
{

class Device;

/// RAII wrapper for a Vulkan image and its bound memory.
class Image final
{
public:
    /// Vulkan image properties and allocation requirements.
    struct CreateInfo
    {
        VkImageType type = VK_IMAGE_TYPE_2D;
        VkExtent3D extent{};
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
        VkImageUsageFlags usage = 0;
        VkMemoryPropertyFlags memoryProperties = 0;
        VkImageCreateFlags flags = 0;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    /// Creates an empty image wrapper.
    Image() = default;
    /// Creates an image and allocates matching device memory.
    Image(const Device& device, const CreateInfo& createInfo);
    /// Destroys the owned image and memory.
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    /// Transfers image and memory ownership from another wrapper.
    Image(Image&& other) noexcept;
    /// Replaces this image by taking ownership from another wrapper.
    Image& operator=(Image&& other) noexcept;

    /// Creates or replaces the image and its backing memory.
    void create(const Device& device, const CreateInfo& createInfo);
    /// Releases the owned image and memory.
    void reset() noexcept;

    /// Returns the owned Vulkan image handle.
    [[nodiscard]] VkImage get() const noexcept { return image_; }
    /// Returns the memory bound to the image.
    [[nodiscard]] VkDeviceMemory memory() const noexcept;
    /// Returns whether an image is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept { return image_ != VK_NULL_HANDLE; }

    [[nodiscard]] VkDevice ownerDevice() const noexcept { return ownerDevice_; }
    [[nodiscard]] const CreateInfo& description() const noexcept { return description_; }

private:
    VkDevice ownerDevice_ = VK_NULL_HANDLE;
    CreateInfo description_{};
#if VK_RENDERER_USE_VMA
    /// Allocator that owns the image allocation.
    VmaAllocator allocator_ = VK_NULL_HANDLE;
#else
    /// Logical device that owns the image and memory.
    VkDevice device_ = VK_NULL_HANDLE;
#endif
    /// Owned Vulkan image handle.
    VkImage image_ = VK_NULL_HANDLE;
#if VK_RENDERER_USE_VMA
    /// Device memory bound to the image.
    VmaAllocation allocation_ = VK_NULL_HANDLE;
#else
    /// Original Vulkan allocation bound to the image.
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
#endif
};

} // namespace rubia::rhi::vulkan
