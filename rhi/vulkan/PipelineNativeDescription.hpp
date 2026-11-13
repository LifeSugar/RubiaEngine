#pragma once
#include "rhi/GraphicsPipeline.hpp"
#include "vulkan/GraphicsPipeline.hpp"
#include "VulkanResourceDevice.hpp"

namespace rubia::rhi::vulkan
{
/// Converts pure GPU state; caller supplies the compatible native render-pass owner.
GraphicsPipeline::CreateInfo nativeGraphicsPipelineDescription(const rhi::GraphicsPipelineDesc&);
/// Legacy bridge. Existing shader refs/layout owners are retained. Raw legacy
/// SPIR-V creates shader objects, so normal frame paths supply prepared shaders.
rhi::GraphicsPipelineDesc describeGraphicsPipeline(const GraphicsPipeline::CreateInfo&,
                                                   const VulkanResourceDevice&);
void applySpecializations(GraphicsPipeline::CreateInfo&, const std::vector<rhi::SpecializationConstant>&);
std::shared_ptr<const RenderPassState> nativePipelineTarget(const rhi::PipelineTargetRef&);
} // namespace rubia::rhi::vulkan
