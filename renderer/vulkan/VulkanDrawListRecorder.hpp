#pragma once
#include "vulkan/VulkanDrawList.hpp"
#include <vulkan/vulkan.h>
namespace rubia::rhi::vulkan
{
void recordVulkanDrawList(VkCommandBuffer commandBuffer, const rhi::BindingSetRef& frameBindings,
    VkExtent2D extent, const VulkanDrawList& draws);
}
