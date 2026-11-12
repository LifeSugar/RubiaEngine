#pragma once
// Compatibility name. New callers use the backend-neutral bridge and inject
// GuiRenderHost plus GuiRendererBackend at attach().
#include "imgui/ApplicationGuiRenderBridge.hpp"
namespace rubia::rhi::vulkan
{
using VulkanApplicationGuiRenderBridge = integration::imgui::ApplicationGuiRenderBridge;
}
