#include "vulkan/Device.hpp"

#include "vulkan/Swapchain.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{
namespace
{

std::vector<const char*> requiredDeviceExtensions()
{
    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
#if defined(__APPLE__)
    // MoltenVK exposes VK_KHR_portability_subset; the spec requires enabling it
    // whenever the physical device advertises support for it.
    extensions.push_back("VK_KHR_portability_subset");
#endif
    return extensions;
}

} // namespace

Device::Device(
    VkInstance instance,
    VkSurfaceKHR surface,
    bool preferIntegratedGpu,
    uint32_t vulkanApiVersion)
{
    create(instance, surface, preferIntegratedGpu, vulkanApiVersion);
}

Device::~Device()
{
    reset();
}

Device::Device(Device&& other) noexcept
    : physicalDevice_(std::exchange(other.physicalDevice_, VK_NULL_HANDLE)),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      enabledFeatures_(std::exchange(other.enabledFeatures_, VkPhysicalDeviceFeatures{})),
#if VK_RENDERER_USE_VMA
      allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
#endif
      graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
      presentQueue_(std::exchange(other.presentQueue_, VK_NULL_HANDLE)),
      graphicsQueueFamily_(std::exchange(other.graphicsQueueFamily_, 0)),
      presentQueueFamily_(std::exchange(other.presentQueueFamily_, 0))
{
}

Device& Device::operator=(Device&& other) noexcept
{
    if (this != &other)
    {
        reset();
        physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        enabledFeatures_ = std::exchange(other.enabledFeatures_, VkPhysicalDeviceFeatures{});
#if VK_RENDERER_USE_VMA
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
#endif
        graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
        presentQueue_ = std::exchange(other.presentQueue_, VK_NULL_HANDLE);
        graphicsQueueFamily_ = std::exchange(other.graphicsQueueFamily_, 0);
        presentQueueFamily_ = std::exchange(other.presentQueueFamily_, 0);
    }
    return *this;
}

void Device::create(
    VkInstance instance,
    VkSurfaceKHR surface,
    bool preferIntegratedGpu,
    uint32_t vulkanApiVersion)
{
    if (instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
    {
        throw std::invalid_argument("cannot create a Device with an invalid instance or surface");
    }

    uint32_t physicalDeviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr) != VK_SUCCESS ||
        physicalDeviceCount == 0)
    {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    if (vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to enumerate Vulkan physical devices!");
    }

    VkPhysicalDevice selectedPhysicalDevice = VK_NULL_HANDLE;
    QueueFamilyIndices selectedQueueFamilies;
    int bestScore = -1;

    for (VkPhysicalDevice candidate : physicalDevices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        std::cout << "Checking device: " << properties.deviceName
                  << " (" << physicalDeviceTypeName(properties.deviceType) << ")\n";

        if (!isSuitable(candidate, surface))
        {
            std::cout << " -> Device is not suitable.\n";
            continue;
        }

        int score = 0;
        switch (properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score = preferIntegratedGpu ? 1000 : 500;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score = preferIntegratedGpu ? 500 : 1000;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score = 100;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            score = 50;
            break;
        default:
            break;
        }

        if (score > bestScore)
        {
            bestScore = score;
            selectedPhysicalDevice = candidate;
            selectedQueueFamilies = findQueueFamilies(candidate, surface);
        }
    }

    if (selectedPhysicalDevice == VK_NULL_HANDLE || !selectedQueueFamilies.complete())
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    const std::array<uint32_t, 2> queueFamilyCandidates = {
        *selectedQueueFamilies.graphics,
        *selectedQueueFamilies.present
    };
    std::set<uint32_t> uniqueQueueFamilies(
        queueFamilyCandidates.begin(),
        queueFamilyCandidates.end());

    constexpr float kQueuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &kQueuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures features{};
    const std::vector<const char*> deviceExtensions = requiredDeviceExtensions();
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &features;

    VkDevice newDevice = VK_NULL_HANDLE;
    if (vkCreateDevice(selectedPhysicalDevice, &createInfo, nullptr, &newDevice) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create logical device!");
    }

    VkQueue newGraphicsQueue = VK_NULL_HANDLE;
    VkQueue newPresentQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(newDevice, *selectedQueueFamilies.graphics, 0, &newGraphicsQueue);
    vkGetDeviceQueue(newDevice, *selectedQueueFamilies.present, 0, &newPresentQueue);

#if VK_RENDERER_USE_VMA
    VmaAllocatorCreateInfo allocatorCreateInfo{};
    allocatorCreateInfo.instance = instance;
    allocatorCreateInfo.physicalDevice = selectedPhysicalDevice;
    allocatorCreateInfo.device = newDevice;
    VkPhysicalDeviceProperties selectedDeviceProperties{};
    vkGetPhysicalDeviceProperties(
        selectedPhysicalDevice,
        &selectedDeviceProperties);
    allocatorCreateInfo.vulkanApiVersion =
        std::min(vulkanApiVersion, selectedDeviceProperties.apiVersion);

    VmaAllocator newAllocator = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&allocatorCreateInfo, &newAllocator) != VK_SUCCESS)
    {
        vkDestroyDevice(newDevice, nullptr);
        throw std::runtime_error("failed to create Vulkan memory allocator!");
    }
#else
    (void)vulkanApiVersion;
#endif

    reset();
    physicalDevice_ = selectedPhysicalDevice;
    device_ = newDevice;
    enabledFeatures_ = features;
#if VK_RENDERER_USE_VMA
    allocator_ = newAllocator;
#endif
    graphicsQueue_ = newGraphicsQueue;
    presentQueue_ = newPresentQueue;
    graphicsQueueFamily_ = *selectedQueueFamilies.graphics;
    presentQueueFamily_ = *selectedQueueFamilies.present;
}

void Device::reset() noexcept
{
#if VK_RENDERER_USE_VMA
    if (allocator_ != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator_);
    }
#endif
    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
    }

    physicalDevice_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    enabledFeatures_ = {};
#if VK_RENDERER_USE_VMA
    allocator_ = VK_NULL_HANDLE;
#endif
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    graphicsQueueFamily_ = 0;
    presentQueueFamily_ = 0;
}

void Device::waitIdle() const
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
    }
}

VkFormat Device::findSupportedFormat(
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags requiredFeatures) const
{
    if (physicalDevice_ == VK_NULL_HANDLE ||
        candidates.empty() ||
        requiredFeatures == 0)
    {
        throw std::invalid_argument(
            "format query requires a physical device, candidates, and features");
    }

    for (VkFormat format : candidates)
    {
        if (format == VK_FORMAT_UNDEFINED)
        {
            continue;
        }

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice_,
            format,
            &properties);

        const VkFormatFeatureFlags availableFeatures =
            tiling == VK_IMAGE_TILING_LINEAR
                ? properties.linearTilingFeatures
                : properties.optimalTilingFeatures;
        if ((availableFeatures & requiredFeatures) == requiredFeatures)
        {
            return format;
        }
    }

    throw std::runtime_error("failed to find a supported image format");
}

VkFormat Device::findDepthStencilFormat() const
{
    return findSupportedFormat(
        {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT
        },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

Device::QueueFamilyIndices Device::findQueueFamilies(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice,
        &queueFamilyCount,
        queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (presentSupport == VK_TRUE)
        {
            indices.present = i;
        }

        if (indices.complete())
        {
            break;
        }
    }

    return indices;
}

bool Device::supportsRequiredExtensions(VkPhysicalDevice physicalDevice)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(
        physicalDevice,
        nullptr,
        &extensionCount,
        availableExtensions.data());

    std::set<std::string> requiredExtensions;
    for (const char* extensionName : requiredDeviceExtensions())
    {
        requiredExtensions.insert(extensionName);
    }
    for (const VkExtensionProperties& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

bool Device::isSuitable(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    const QueueFamilyIndices queueFamilies = findQueueFamilies(physicalDevice, surface);
    if (!queueFamilies.complete() || !supportsRequiredExtensions(physicalDevice))
    {
        return false;
    }

    const Swapchain::SupportDetails swapchainSupport =
        Swapchain::querySupport(physicalDevice, surface);
    return !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
}

const char* Device::physicalDeviceTypeName(VkPhysicalDeviceType type) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "Other";
    }
}

std::string Device::queueFlagsName(VkQueueFlags flags)
{
    std::string result;
    if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
    {
        result += "GRAPHICS|";
    }
    if ((flags & VK_QUEUE_COMPUTE_BIT) != 0)
    {
        result += "COMPUTE|";
    }
    if ((flags & VK_QUEUE_TRANSFER_BIT) != 0)
    {
        result += "TRANSFER|";
    }
    if ((flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0)
    {
        result += "SPARSE_BINDING|";
    }
    if (result.empty())
    {
        return "NONE";
    }
    result.pop_back();
    return result;
}

} // namespace rubia::rhi::vulkan
