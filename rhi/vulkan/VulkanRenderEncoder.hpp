#pragma once
#include "rhi/RenderEncoder.hpp"
#include "vulkan/RenderPass.hpp"
#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{
// Borrows an active command buffer and its device; contains no drawing policy.
class VulkanRenderEncoder final : public rhi::RenderEncoder
{
public:
    VulkanRenderEncoder(DeviceId device, VkCommandBuffer commands,
                        std::shared_ptr<const RenderPassState> pass = {});
    void bindPipeline(const PipelineRef&) override;
    void bindResources(uint32_t group, const BindingSetRef&) override;
    void bindVertexBuffer(uint32_t binding, const BufferRef&, uint64_t offset = 0) override;
    void bindIndexBuffer(const BufferRef&, IndexType, uint64_t offset = 0) override;
    void setViewport(const Viewport&) override;
    void setScissor(const Rect&) override;
    void setConstants(ShaderStageMask, uint32_t offset, uint32_t size, const void*) override;
    void draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indices, uint32_t instances, uint32_t firstIndex,
                     int32_t vertexOffset, uint32_t firstInstance) override;
private:
    void requirePipeline() const;
    void requireDevice(const Resource&) const;
    DeviceId device_;
    VkCommandBuffer commands_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::shared_ptr<const RenderPassState> pass_;
};
} // namespace rubia::rhi::vulkan
