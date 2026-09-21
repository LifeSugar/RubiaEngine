#pragma once

#include "vulkan/VulkanMemoryConfig.hpp"

#include <vulkan/vulkan.h>
#if VK_RENDERER_USE_VMA
#include <vk_mem_alloc.h>
#endif

#include <optional>
#include <string>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Owns the logical device and caches its physical device and queues.
class Device final
{
public:
    /// Creates an empty device wrapper.
    Device() = default;
    /// Selects a physical device and creates its logical device and queues.
    Device(
        VkInstance instance,
        VkSurfaceKHR surface,
        bool preferIntegratedGpu = false,
        uint32_t vulkanApiVersion = VK_API_VERSION_1_0);
    /// Destroys the owned logical device.
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    /// Transfers logical-device state from another wrapper.
    Device(Device&& other) noexcept;
    /// Replaces this device by taking ownership from another wrapper.
    Device& operator=(Device&& other) noexcept;

    /// Selects a physical device and creates or replaces the logical device.
    void create(
        VkInstance instance,
        VkSurfaceKHR surface,
        bool preferIntegratedGpu = false,
        uint32_t vulkanApiVersion = VK_API_VERSION_1_0);
    /// Destroys the logical device and clears all cached handles.
    void reset() noexcept;
    /// Waits until all queues on the logical device are idle.
    void waitIdle() const;

    /// Selects the first candidate supporting all required format features.
    [[nodiscard]] VkFormat findSupportedFormat(
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags requiredFeatures) const;
    /// Selects a depth-stencil format supported as an optimal attachment.
    [[nodiscard]] VkFormat findDepthStencilFormat() const;

    /// Returns the selected physical-device handle.
    [[nodiscard]] VkPhysicalDevice physical() const noexcept { return physicalDevice_; }
    /// Returns the owned logical-device handle.
    [[nodiscard]] VkDevice get() const noexcept { return device_; }
    /// Features actually enabled on this logical device, not merely supported.
    [[nodiscard]] const VkPhysicalDeviceFeatures& enabledFeatures() const noexcept
    {
        return enabledFeatures_;
    }
#if VK_RENDERER_USE_VMA
    /// Returns the allocator associated with the logical device.
    [[nodiscard]] VmaAllocator allocator() const noexcept { return allocator_; }
#endif
    /// Returns the graphics queue handle.
    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
    /// Returns the presentation queue handle.
    [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
    /// Returns the graphics queue-family index.
    [[nodiscard]] uint32_t graphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }
    /// Returns the presentation queue-family index.
    [[nodiscard]] uint32_t presentQueueFamily() const noexcept { return presentQueueFamily_; }
    /// Returns whether a logical device is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept
    {
#if VK_RENDERER_USE_VMA
        return device_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE;
#else
        return device_ != VK_NULL_HANDLE;
#endif
    }

private:
    /// Graphics and presentation queue families found for a physical device.
    struct QueueFamilyIndices
    {
        /// Queue family that supports graphics commands.
        std::optional<uint32_t> graphics;
        /// Queue family that supports presentation to the target surface.
        std::optional<uint32_t> present;

        /// Returns whether both required queue families were found.
        [[nodiscard]] bool complete() const noexcept
        {
            return graphics.has_value() && present.has_value();
        }
    };

    /// Finds graphics and presentation queue families for a physical device.
    [[nodiscard]] static QueueFamilyIndices findQueueFamilies(
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface);
    /// Returns whether a physical device supports all required extensions.
    [[nodiscard]] static bool supportsRequiredExtensions(VkPhysicalDevice physicalDevice);
    /// Returns whether a physical device can render and present to the surface.
    [[nodiscard]] static bool isSuitable(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
    /// Returns a readable name for a Vulkan physical-device type.
    [[nodiscard]] static const char* physicalDeviceTypeName(VkPhysicalDeviceType type) noexcept;
    /// Returns a readable list of capabilities for queue flags.
    [[nodiscard]] static std::string queueFlagsName(VkQueueFlags flags);

    /// Selected physical-device handle.
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    /// Owned logical-device handle.
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures enabledFeatures_{};
#if VK_RENDERER_USE_VMA
    /// VMA allocator bound to the physical and logical device.
    VmaAllocator allocator_ = VK_NULL_HANDLE;
#endif
    /// Queue used to submit graphics work.
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    /// Queue used to present swapchain images.
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    /// Family index of the graphics queue.
    uint32_t graphicsQueueFamily_ = 0;
    /// Family index of the presentation queue.
    uint32_t presentQueueFamily_ = 0;
};

} // namespace rubia::rhi::vulkan
