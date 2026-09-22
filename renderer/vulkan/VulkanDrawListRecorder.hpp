#pragma once
#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{
struct VulkanDrawList;

/// Records compiled draws inside their compatible render pass/subpass.
/// Sets viewport/scissor on every pipeline switch, binds frame set 0 and material
/// set 1, then pushes DrawPushConstants. Preserves opaque/transparent list order.
/// Caller retains all referenced GPU resources until submission completion.
void recordVulkanDrawList(VkCommandBuffer commandBuffer, VkDescriptorSet frameDescriptorSet,
                          VkExtent2D extent, const VulkanDrawList &draws);
} // namespace rubia::rhi::vulkan
