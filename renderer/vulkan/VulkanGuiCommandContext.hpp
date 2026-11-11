#pragma once

#include "render/GuiOverlay.hpp"

#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{

/// Temporary native UI interop boundary; ordinary Renderer clients do not use it.
struct VulkanGuiCommandContext final : render::GuiCommandContext
{
    explicit VulkanGuiCommandContext(VkCommandBuffer buffer) : commandBuffer(buffer) {}

    VkCommandBuffer commandBuffer;
};

} // namespace rubia::rhi::vulkan
