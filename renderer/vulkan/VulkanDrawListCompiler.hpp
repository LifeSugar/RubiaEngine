#pragma once

#include "render/RenderList.hpp"
#include "vulkan/GraphicsPipeline.hpp"
#include "vulkan/VulkanDrawList.hpp"

namespace rubia::rhi::vulkan
{

class RenderAssetCache;
class GraphicsPipelineCache;
class GpuMaterialTemplate;

/// Resolves resident draws and their actual pipelines before command recording.
/// passPipeline supplies pass/subpass and fixed-function defaults. Material state
/// overrides blend/cull/depth. Shader, layouts, vertex input and draw push constants
/// come from the resident material and the scene drawing ABI (asset::Vertex,
/// frame set 0, material set 1, DrawPushConstants). No resource uploads occur here.
class VulkanDrawListCompiler final
{
  public:
    /// Shared immutable description path for prewarming and draw compilation.
    static GraphicsPipeline::CreateInfo describeMaterialPipeline(
        const GpuMaterialTemplate& materialTemplate, const render::PipelineVariantKey& key,
        const GraphicsPipeline::CreateInfo& passPipeline);
    [[nodiscard]] VulkanDrawList compile(const render::RenderList &source,
                                         const RenderAssetCache &resources,
                                         const GraphicsPipeline::CreateInfo &passPipeline,
                                         GraphicsPipelineCache &pipelines) const;
};

} // namespace rubia::rhi::vulkan
