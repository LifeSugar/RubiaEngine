#pragma once

#include "vulkan/VulkanMemoryConfig.hpp"

#include <vulkan/vulkan.h>
#if VK_RENDERER_USE_VMA
#include <vk_mem_alloc.h>
#endif

namespace rubia::rhi::vulkan
{

class Device;

/// RAII wrapper for a Vulkan buffer and its bound memory.
class Buffer final
{
public:
    /// Creates an empty buffer wrapper.
    Buffer() = default;
    /// Creates a buffer and allocates matching device memory.
    Buffer(
        const Device& device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties);
    /// Unmaps and destroys the owned buffer and memory.
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /// Transfers buffer and memory ownership from another wrapper.
    Buffer(Buffer&& other) noexcept;
    /// Replaces this buffer by taking ownership from another wrapper.
    Buffer& operator=(Buffer&& other) noexcept;

    /// Creates or replaces the buffer and its backing memory.
    void create(
        const Device& device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties);
    /// Unmaps and releases all owned Vulkan resources.
    void reset() noexcept;

    /// Returns the owned Vulkan buffer handle.
    [[nodiscard]] VkBuffer get() const noexcept { return buffer_; }
    /// Returns the memory bound to the buffer.
    [[nodiscard]] VkDeviceMemory memory() const noexcept;
    /// Returns the allocated buffer size in bytes.
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
    /// Returns whether a buffer is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept { return buffer_ != VK_NULL_HANDLE; }

    /// Maps a range of the buffer's memory into host address space.
    void* map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    /// Unmaps the buffer memory if it is currently mapped.
    void unmap() noexcept;

    [[nodiscard]] VkDevice ownerDevice() const noexcept { return ownerDevice_; }
    [[nodiscard]] VkBufferUsageFlags usage() const noexcept { return usage_; }

private:
    VkDevice ownerDevice_ = VK_NULL_HANDLE;
    VkBufferUsageFlags usage_ = 0;
#if VK_RENDERER_USE_VMA
    /// Allocator that owns the buffer allocation.
    VmaAllocator allocator_ = VK_NULL_HANDLE;
#else
    /// Logical device that owns the buffer and memory.
    VkDevice device_ = VK_NULL_HANDLE;
#endif
    /// Owned Vulkan buffer handle.
    VkBuffer buffer_ = VK_NULL_HANDLE;
#if VK_RENDERER_USE_VMA
    /// Device memory bound to the buffer.
    VmaAllocation allocation_ = VK_NULL_HANDLE;
#else
    /// Original Vulkan allocation bound to the buffer.
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
#endif
    /// Allocated buffer size in bytes.
    VkDeviceSize size_ = 0;
    /// Current host mapping, or null when unmapped.
    void* mappedData_ = nullptr;
};

} // namespace rubia::rhi::vulkan
