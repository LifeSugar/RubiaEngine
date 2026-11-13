#include "VulkanRenderEncoder.hpp"
#include "VulkanPipelineDevice.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
VulkanRenderEncoder::VulkanRenderEncoder(DeviceId device, VkCommandBuffer commands,
    std::shared_ptr<const RenderPassState> pass)
    : device_(device), commands_(commands), pass_(std::move(pass))
{
    if (!device || !commands) throw std::invalid_argument("render encoder requires a device and command buffer");
}
void VulkanRenderEncoder::requireDevice(const Resource& resource) const
{
    if (resource.deviceId() != device_) throw std::invalid_argument("render resource belongs to another device");
}
void VulkanRenderEncoder::requirePipeline() const
{
    if (!layout_) throw std::logic_error("bind a pipeline before recording a draw or its bindings");
}
void VulkanRenderEncoder::bindPipeline(const PipelineRef& pipeline)
{
    if (!pipeline) throw std::invalid_argument("null pipeline");
    requireDevice(*pipeline);
    if (pass_ && pass_->compatibilityKey() != nativePipelinePass(pipeline).compatibilityKey())
        throw std::invalid_argument("pipeline is incompatible with the active render pass");
    layout_ = nativePipelineLayout(pipeline);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, nativePipeline(pipeline));
}
void VulkanRenderEncoder::bindResources(uint32_t group, const BindingSetRef& resources)
{
    requirePipeline();
    if (!resources) throw std::invalid_argument("null binding set");
    requireDevice(*resources);
    const auto set = nativeBindingSet(resources);
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, group, 1, &set, 0, nullptr);
}
void VulkanRenderEncoder::bindVertexBuffer(uint32_t binding, const BufferRef& buffer, uint64_t offset)
{
    if (!buffer) throw std::invalid_argument("null vertex buffer");
    requireDevice(*buffer);
    const auto native = nativeBuffer(buffer).get();
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(commands_, binding, 1, &native, &nativeOffset);
}
void VulkanRenderEncoder::bindIndexBuffer(const BufferRef& buffer, IndexType type, uint64_t offset)
{
    if (!buffer) throw std::invalid_argument("null index buffer");
    requireDevice(*buffer);
    vkCmdBindIndexBuffer(commands_, nativeBuffer(buffer).get(), offset,
        type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}
void VulkanRenderEncoder::setViewport(const Viewport& v)
{
    const VkViewport viewport{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth};
    vkCmdSetViewport(commands_, 0, 1, &viewport);
}
void VulkanRenderEncoder::setScissor(const Rect& r)
{
    const VkRect2D scissor{{r.x, r.y}, {r.width, r.height}};
    vkCmdSetScissor(commands_, 0, 1, &scissor);
}
void VulkanRenderEncoder::setConstants(ShaderStageMask stages, uint32_t offset, uint32_t size, const void* data)
{
    requirePipeline();
    vkCmdPushConstants(commands_, layout_, toVulkanStages(stages), offset, size, data);
}
void VulkanRenderEncoder::draw(uint32_t count, uint32_t instances, uint32_t first, uint32_t firstInstance)
{
    requirePipeline();
    vkCmdDraw(commands_, count, instances, first, firstInstance);
}
void VulkanRenderEncoder::drawIndexed(uint32_t count, uint32_t instances, uint32_t first,
                                     int32_t vertexOffset, uint32_t firstInstance)
{
    requirePipeline();
    vkCmdDrawIndexed(commands_, count, instances, first, vertexOffset, firstInstance);
}
} // namespace rubia::rhi::vulkan
