#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanResourceDevice.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

Swapchain::Swapchain(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    uint32_t graphicsQueueFamily,
    uint32_t presentQueueFamily,
    VkExtent2D framebufferExtent,
    VkSwapchainKHR oldSwapchain,
    const PresentationConfig& presentation)
{
    create(
        physicalDevice,
        device,
        surface,
        graphicsQueueFamily,
        presentQueueFamily,
        framebufferExtent,
        oldSwapchain, presentation);
}

Swapchain::~Swapchain()
{
    reset();
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      swapchain_(std::exchange(other.swapchain_, VK_NULL_HANDLE)),
      images_(std::move(other.images_)),
      imageViews_(std::move(other.imageViews_)),
      format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED)),
      colorSpace_(std::exchange(
          other.colorSpace_,
          VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)),
      extent_(std::exchange(other.extent_, VkExtent2D{})),
      presentMode_(other.presentMode_)
{
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept
{
    if (this != &other)
    {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        colorSpace_ = std::exchange(
            other.colorSpace_,
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
        presentMode_ = other.presentMode_;
    }
    return *this;
}

void Swapchain::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    uint32_t graphicsQueueFamily,
    uint32_t presentQueueFamily,
    VkExtent2D framebufferExtent,
    VkSwapchainKHR oldSwapchain,
    const PresentationConfig& presentation)
{
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
    {
        throw std::invalid_argument("cannot create a Swapchain with an invalid device or surface");
    }

    const SupportDetails support = querySupport(physicalDevice, surface);
    if (support.formats.empty() || support.presentModes.empty())
    {
        throw std::runtime_error("swapchain support is no longer adequate!");
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats, presentation.preferredFormat);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes, presentation.preferredPresentMode);
    const VkExtent2D extent = chooseExtent(support.capabilities, framebufferExtent);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    const std::array<uint32_t, 2> queueFamilyIndices = {
        graphicsQueueFamily,
        presentQueueFamily
    };

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (graphicsQueueFamily != presentQueueFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    std::vector<VkImage> newImages;
    std::vector<ImageView> newImageViews;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &newSwapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create swapchain!");
    }

    try
    {
        uint32_t actualImageCount = 0;
        if (vkGetSwapchainImagesKHR(device, newSwapchain, &actualImageCount, nullptr) != VK_SUCCESS ||
            actualImageCount == 0)
        {
            throw std::runtime_error("failed to query swapchain images!");
        }

        newImages.resize(actualImageCount);
        if (vkGetSwapchainImagesKHR(device, newSwapchain, &actualImageCount, newImages.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to get swapchain images!");
        }
        newImages.resize(actualImageCount);

        newImageViews.reserve(newImages.size());
        for (VkImage image : newImages)
        {
            ImageView::CreateInfo viewInfo{};
            viewInfo.image = image;
            viewInfo.format = surfaceFormat.format;
            viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            newImageViews.emplace_back(device, viewInfo);
        }
    }
    catch (...)
    {
        // Image views must be destroyed before their owning swapchain.
        newImageViews.clear();
        vkDestroySwapchainKHR(device, newSwapchain, nullptr);
        throw;
    }

    reset();
    device_ = device;
    swapchain_ = newSwapchain;
    images_ = std::move(newImages);
    imageViews_ = std::move(newImageViews);
    format_ = surfaceFormat.format;
    colorSpace_ = surfaceFormat.colorSpace;
    extent_ = extent;
    presentMode_ = presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? PresentMode::Mailbox
        : presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? PresentMode::Immediate : PresentMode::Fifo;
}

void Swapchain::reset() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        imageViews_.clear();
        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
    }

    device_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    images_.clear();
    imageViews_.clear();
    format_ = VK_FORMAT_UNDEFINED;
    colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    extent_ = {};
}

Swapchain::SupportDetails Swapchain::querySupport(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface)
{
    SupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount > 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice,
            surface,
            &formatCount,
            details.formats.data());
        details.formats.resize(formatCount);
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice,
            surface,
            &presentModeCount,
            details.presentModes.data());
        details.presentModes.resize(presentModeCount);
    }

    return details;
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats, Format preferred)
{
    if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
    {
        return {toVulkan(preferred), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    for (const VkSurfaceFormatKHR& availableFormat : availableFormats)
    {
        // The default preference is UNorm for display-encoded GUI colors.
        if (availableFormat.format == toVulkan(preferred) &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }
    return availableFormats.front();
}

VkPresentModeKHR Swapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes, PresentMode preferred)
{
    const auto requested = preferred == PresentMode::Mailbox ? VK_PRESENT_MODE_MAILBOX_KHR
        : preferred == PresentMode::Immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == requested)
        {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    VkExtent2D framebufferExtent)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    framebufferExtent.width = std::clamp(
        framebufferExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    framebufferExtent.height = std::clamp(
        framebufferExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
    return framebufferExtent;
}

} // namespace rubia::rhi::vulkan
