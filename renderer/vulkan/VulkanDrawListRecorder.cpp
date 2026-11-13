#include "vulkan/VulkanDrawListRecorder.hpp"
#include "core/DrawRecorder.hpp"
#include "vulkan/VulkanRenderEncoder.hpp"
#include <stdexcept>
namespace rubia::rhi::vulkan
{
void recordVulkanDrawList(VkCommandBuffer commands, const rhi::BindingSetRef& frame,
    VkExtent2D extent, const VulkanDrawList& draws)
{
    if (!frame) throw std::invalid_argument("draw recorder requires frame bindings");
    VulkanRenderEncoder encoder(frame->deviceId(), commands);
    render::recordDrawList(encoder, frame, extent.width, extent.height, draws);
}
}
