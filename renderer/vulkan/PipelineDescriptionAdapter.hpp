#pragma once

#include "pipeline/MaterialPipelineCompiler.hpp"
#include "vulkan/GraphicsPipeline.hpp"

namespace rubia::rhi::vulkan
{

/// Transitional bridge: reflects only binding metadata needed by CPU policy.
[[nodiscard]] rhi::BindingLayoutDesc describeBindingLayout(const DescriptorSetLayoutState& layout);

/// Replaces shader-local ABI state and applies explicitly supplied policy fields.
/// All other state (render pass, owners, topology, front face, stencil, sample
/// count/masks, write masks, viewport/dynamics, etc.) remains with the caller.
void applyPipelinePolicy(GraphicsPipeline::CreateInfo& destination,
                         const render::PipelinePolicyDesc& policy);

} // namespace rubia::rhi::vulkan
