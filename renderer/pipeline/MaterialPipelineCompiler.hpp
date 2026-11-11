#pragma once

#include "asset/ShaderProgramAsset.hpp"
#include "render/PipelineVariantKey.hpp"
#include "rhi/PipelineTypes.hpp"

#include <optional>
#include <vector>

namespace rubia::render
{

/// Renderer-owned policy, not a complete GPU pipeline description. Optional
/// fields override pass defaults; unrelated fixed-function state stays intact.
/// Vertex layout, constant ranges and specializations replace shader-local state.
struct PipelinePolicyDesc
{
    rhi::VertexLayoutDesc vertexLayout;
    std::vector<rhi::ConstantRangeDesc> constantRanges;
    std::vector<rhi::SpecializationConstant> specializations;
    std::optional<rhi::CullMode> cullMode;
    std::optional<bool> depthTestEnabled;
    std::optional<bool> depthWriteEnabled;
    std::optional<rhi::CompareOp> depthCompare;
    std::optional<rhi::ColorBlendDesc> colorBlend;
};

/// Borrowed immutable CPU reflection plus the actual pass/material binding
/// layouts. No device, GPU owner, asset lookup or backend types are required.
struct MaterialPipelineInput
{
    const asset::ShaderProgramInterface& programInterface;
    const asset::ShaderInterface* fragmentInterface = nullptr;
    uint32_t materialBindingGroup = 1;
    std::vector<rhi::BindingLayoutDesc> bindingLayouts;
    uint32_t colorAttachmentCount = 1;
};

/// Canonical mesh/Draw ABI, without per-material state or input pruning.
[[nodiscard]] PipelinePolicyDesc defaultScenePipelinePolicy();
/// Full-screen output ABI and its depth/culling policy.
[[nodiscard]] PipelinePolicyDesc defaultOutputPipelinePolicy();

class MaterialPipelineCompiler final
{
public:
    /// Validates scene ABI and resolves material state before GPU pipeline lookup.
    [[nodiscard]] PipelinePolicyDesc compile(const MaterialPipelineInput& input,
                                             const PipelineVariantKey& key) const;
};

} // namespace rubia::render
