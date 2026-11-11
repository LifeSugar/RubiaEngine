#pragma once
#include "rhi/GraphicsPipeline.hpp"
#include "VulkanResourceDevice.hpp"
#include "PipelineNativeDescription.hpp"

namespace rubia::rhi::vulkan
{
class VulkanPipelineDevice final : public rhi::PipelineDevice
{
public:
    explicit VulkanPipelineDevice(const VulkanResourceDevice& resources) : resources_(resources) {}
    DeviceId deviceId() const noexcept override { return resources_.id(); }
    void validateGraphicsPipeline(const rhi::GraphicsPipelineDesc&) const override;
    rhi::PipelineRef createGraphicsPipeline(const rhi::GraphicsPipelineDesc&) const override;
private:
    const VulkanResourceDevice& resources_;
};
VkPipeline nativePipeline(const rhi::PipelineRef&);
VkPipelineLayout nativePipelineLayout(const rhi::PipelineRef&);
const RenderPassState& nativePipelinePass(const rhi::PipelineRef&);
} // namespace rubia::rhi::vulkan
