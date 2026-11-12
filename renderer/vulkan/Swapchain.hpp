#pragma once

#include <vulkan/vulkan.h>
#include "rhi/Presentation.hpp"
#include "vulkan/ImageView.hpp"

#include <cstddef>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Owns a presentation swapchain and views of its images.
class Swapchain final
{
public:
    /// Presentation capabilities reported for a surface.
    struct SupportDetails
    {
        /// Surface size, image-count, and transform limits.
        VkSurfaceCapabilitiesKHR capabilities{};
        /// Supported surface color formats and color spaces.
        std::vector<VkSurfaceFormatKHR> formats;
        /// Supported presentation modes.
        std::vector<VkPresentModeKHR> presentModes;
    };

    /// Creates an empty swapchain wrapper.
    Swapchain() = default;
    /// Creates a swapchain and views for all of its images.
    Swapchain(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkSurfaceKHR surface,
        uint32_t graphicsQueueFamily,
        uint32_t presentQueueFamily,
        VkExtent2D framebufferExtent,
        VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE,
        const PresentationConfig& presentation = {});
    /// Destroys the swapchain and its image views.
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    /// Transfers swapchain ownership from another wrapper.
    Swapchain(Swapchain&& other) noexcept;
    /// Replaces this swapchain by taking ownership from another wrapper.
    Swapchain& operator=(Swapchain&& other) noexcept;

    /// Creates or replaces the swapchain and its image views.
    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkSurfaceKHR surface,
        uint32_t graphicsQueueFamily,
        uint32_t presentQueueFamily,
        VkExtent2D framebufferExtent,
        VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE,
        const PresentationConfig& presentation = {});
    /// Destroys the swapchain and clears its cached image state.
    void reset() noexcept;

    /// Returns the owned Vulkan swapchain handle.
    [[nodiscard]] VkSwapchainKHR get() const noexcept { return swapchain_; }
    /// Returns the selected swapchain color format.
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    /// Returns the selected presentation color space.
    [[nodiscard]] VkColorSpaceKHR colorSpace() const noexcept
    {
        return colorSpace_;
    }
    /// Returns the selected swapchain image extent.
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    PresentMode presentMode() const noexcept { return presentMode_; }
    /// Returns the number of images owned by the swapchain.
    [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }
    /// Returns the non-owning swapchain image handles.
    [[nodiscard]] const std::vector<VkImage>& images() const noexcept { return images_; }
    /// Returns the owned views for all swapchain images.
    [[nodiscard]] const std::vector<ImageView>& imageViews() const noexcept { return imageViews_; }
    /// Returns whether a swapchain is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept { return swapchain_ != VK_NULL_HANDLE; }

    /// Queries presentation support for a physical device and surface.
    [[nodiscard]] static SupportDetails querySupport(
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface);

private:
    /// Selects the preferred surface format from those available.
    [[nodiscard]] static VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats, Format preferred);
    /// Selects the preferred presentation mode from those available.
    [[nodiscard]] static VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes, PresentMode preferred);
    /// Selects a supported image extent for the current framebuffer size.
    [[nodiscard]] static VkExtent2D chooseExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        VkExtent2D framebufferExtent);

    /// Logical device that owns the swapchain and image views.
    VkDevice device_ = VK_NULL_HANDLE;
    /// Owned Vulkan swapchain handle.
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    /// Non-owning images provided by the swapchain.
    std::vector<VkImage> images_;
    /// Owned views for the swapchain images.
    std::vector<ImageView> imageViews_;
    /// Color format shared by all swapchain images.
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    /// Presentation color space paired with the image format.
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    /// Drawable extent shared by all swapchain images.
    VkExtent2D extent_{};
    PresentMode presentMode_ = PresentMode::Fifo;
};

} // namespace rubia::rhi::vulkan
