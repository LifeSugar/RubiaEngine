#include "pipeline/MaterialPipelineCompiler.hpp"

#include "asset/MeshAsset.hpp"
#include "render/RenderData.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace rubia::render
{
namespace
{

rhi::CompareOp compareOp(asset::DepthCompare compare)
{
    switch (compare)
    {
    case asset::DepthCompare::Never: return rhi::CompareOp::Never;
    case asset::DepthCompare::Less: return rhi::CompareOp::Less;
    case asset::DepthCompare::Equal: return rhi::CompareOp::Equal;
    case asset::DepthCompare::LessEqual: return rhi::CompareOp::LessEqual;
    case asset::DepthCompare::Greater: return rhi::CompareOp::Greater;
    case asset::DepthCompare::NotEqual: return rhi::CompareOp::NotEqual;
    case asset::DepthCompare::GreaterEqual: return rhi::CompareOp::GreaterEqual;
    case asset::DepthCompare::Always: return rhi::CompareOp::Always;
    }
    throw std::invalid_argument("unsupported material depth comparison");
}

rhi::BindingType bindingType(asset::ShaderResourceType type)
{
    switch (type)
    {
    case asset::ShaderResourceType::UniformBuffer: return rhi::BindingType::UniformBuffer;
    case asset::ShaderResourceType::StorageBuffer: return rhi::BindingType::StorageBuffer;
    case asset::ShaderResourceType::SampledImage: return rhi::BindingType::SampledTexture;
    case asset::ShaderResourceType::Sampler: return rhi::BindingType::Sampler;
    case asset::ShaderResourceType::CombinedImageSampler:
        return rhi::BindingType::CombinedTextureSampler;
    }
    throw std::invalid_argument("unsupported draw shader binding type");
}

rhi::VertexFormat vertexFormat(asset::ShaderValueType type)
{
    switch (type)
    {
    case asset::ShaderValueType::Float: return rhi::VertexFormat::Float;
    case asset::ShaderValueType::Float2: return rhi::VertexFormat::Float2;
    case asset::ShaderValueType::Float3: return rhi::VertexFormat::Float3;
    case asset::ShaderValueType::Float4: return rhi::VertexFormat::Float4;
    default: throw std::invalid_argument("draw shader requires an unsupported vertex input type");
    }
}

rhi::ShaderStageMask stages(asset::ShaderStageMask mask)
{
    rhi::ShaderStageMask result = 0;
    if (mask & asset::shaderStageMask(asset::ShaderStage::Vertex))
        result |= rhi::shaderStageMask(rhi::ShaderStage::Vertex);
    if (mask & asset::shaderStageMask(asset::ShaderStage::Fragment))
        result |= rhi::shaderStageMask(rhi::ShaderStage::Fragment);
    return result;
}

void validateInterface(const MaterialPipelineInput& input, const PipelinePolicyDesc& policy)
{
    if (input.bindingLayouts.size() != 2)
        throw std::invalid_argument("scene draws require frame and material binding layouts");
    for (const auto& resource : input.programInterface.bindings)
    {
        if (resource.set >= input.bindingLayouts.size())
            throw std::invalid_argument("draw shader requires binding groups outside frame/material");
        const auto& bindings = input.bindingLayouts[resource.set].bindings;
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
            return binding.binding == resource.binding;
        });
        const auto requiredStages = stages(resource.stages);
        if (found == bindings.end() || found->type != bindingType(resource.type) ||
            found->count != resource.arrayCount ||
            (found->stages & requiredStages) != requiredStages)
            throw std::invalid_argument("draw shader is incompatible with frame/material bindings");
    }
    for (const auto& range : input.programInterface.pushConstants)
        if (range.offset > sizeof(DrawPushConstants) ||
            range.size > sizeof(DrawPushConstants) - range.offset)
            throw std::invalid_argument("draw shader exceeds the DrawPushConstants ABI");
    for (const auto& vertexInput : input.programInterface.vertexInputs)
    {
        const auto found = std::find_if(policy.vertexLayout.attributes.begin(),
            policy.vertexLayout.attributes.end(), [&](const auto& attribute) {
                return attribute.location == vertexInput.location;
            });
        if (found == policy.vertexLayout.attributes.end() ||
            found->format != vertexFormat(vertexInput.type))
            throw std::invalid_argument("draw shader is incompatible with asset::Vertex");
    }
    for (const auto& output : input.programInterface.fragmentOutputs)
        if (output.location >= input.colorAttachmentCount)
            throw std::invalid_argument("draw shader output is outside the pass color attachments");
}

void specializeAlphaClip(PipelinePolicyDesc& policy, const asset::ShaderInterface* fragment,
                         bool enabled)
{
    if (fragment)
    {
        if (fragment->stage != asset::ShaderStage::Fragment)
            throw std::invalid_argument("AlphaClip reflection must describe the fragment shader");
        const auto constant = std::find_if(fragment->specializationConstants.begin(),
            fragment->specializationConstants.end(), [](const auto& value) {
                return value.constantId == AlphaClipSpecializationId;
            });
        if (constant != fragment->specializationConstants.end())
        {
            if (constant->type != asset::ShaderValueType::Bool)
                throw std::invalid_argument("AlphaClip specialization constant 1000 must be bool");
            bool hasThreshold = false;
            for (const auto& push : fragment->pushConstants)
                for (const auto& member : push.members)
                    hasThreshold |= member.offset == offsetof(DrawPushConstants, alphaClipThreshold) &&
                        member.type == asset::ShaderValueType::Float &&
                        member.size == sizeof(float) && member.arrayCount == 1;
            if (!hasThreshold)
                throw std::invalid_argument(
                    "AlphaClip shader requires float threshold at push-constant offset 8");
            policy.specializations.push_back(
                {rhi::ShaderStage::Fragment, AlphaClipSpecializationId, enabled});
            return;
        }
    }
    if (enabled)
        throw std::invalid_argument(
            "material enables AlphaClip but its fragment shader does not support it");
}

} // namespace

PipelinePolicyDesc defaultScenePipelinePolicy()
{
    PipelinePolicyDesc result;
    result.constantRanges = {{rhi::shaderStageMask(rhi::ShaderStage::Vertex) |
                                 rhi::shaderStageMask(rhi::ShaderStage::Fragment),
                             0, sizeof(DrawPushConstants)}};
    result.vertexLayout.bindings = {{0, sizeof(asset::Vertex), rhi::VertexInputRate::Vertex}};
    result.vertexLayout.attributes = {
        {0, 0, rhi::VertexFormat::Float3, offsetof(asset::Vertex, position)},
        {1, 0, rhi::VertexFormat::Float4, offsetof(asset::Vertex, color)},
        {2, 0, rhi::VertexFormat::Float3, offsetof(asset::Vertex, normal)},
        {3, 0, rhi::VertexFormat::Float2, offsetof(asset::Vertex, texCoord)},
        {4, 0, rhi::VertexFormat::Float4, offsetof(asset::Vertex, tangent)}};
    return result;
}

PipelinePolicyDesc defaultOutputPipelinePolicy()
{
    PipelinePolicyDesc result;
    result.constantRanges = {{rhi::shaderStageMask(rhi::ShaderStage::Fragment),
                             0, sizeof(PresentPushConstants)}};
    result.cullMode = rhi::CullMode::None;
    result.depthTestEnabled = false;
    result.depthWriteEnabled = false;
    return result;
}

PipelinePolicyDesc MaterialPipelineCompiler::compile(const MaterialPipelineInput& input,
                                                     const PipelineVariantKey& key) const
{
    const auto featureBits = shaderFeatureBits(key.shaderFeatures);
    const auto alphaClipBit = shaderFeatureBits(ShaderFeatureFlags::AlphaClip);
    if (featureBits & ~alphaClipBit)
        throw std::invalid_argument("unsupported scene shader feature");
    if (input.materialBindingGroup != 1)
        throw std::invalid_argument("scene draws require material bindings at group 1");

    auto result = defaultScenePipelinePolicy();
    result.depthTestEnabled = key.depthTestEnabled;
    result.depthWriteEnabled = key.depthWriteEnabled;
    result.depthCompare = compareOp(key.depthCompare);
    switch (key.cullMode)
    {
    case PipelineCullMode::None: result.cullMode = rhi::CullMode::None; break;
    case PipelineCullMode::Front: result.cullMode = rhi::CullMode::Front; break;
    case PipelineCullMode::Back: result.cullMode = rhi::CullMode::Back; break;
    default: throw std::invalid_argument("unsupported material cull mode");
    }
    rhi::ColorBlendDesc blend;
    blend.enabled = key.blendMode == PipelineBlendMode::Alpha;
    blend.sourceColor = blend.enabled ? rhi::BlendFactor::SourceAlpha : rhi::BlendFactor::One;
    blend.destinationColor = blend.enabled ? rhi::BlendFactor::OneMinusSourceAlpha
                                          : rhi::BlendFactor::Zero;
    blend.destinationAlpha = blend.destinationColor;
    result.colorBlend = blend;

    specializeAlphaClip(result, input.fragmentInterface, (featureBits & alphaClipBit) != 0);
    validateInterface(input, result);
    // Preserve canonical stride/offsets while declaring only consumed attributes.
    const auto& inputs = input.programInterface.vertexInputs;
    auto& attributes = result.vertexLayout.attributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(), [&](const auto& attribute) {
        return std::none_of(inputs.begin(), inputs.end(), [&](const auto& vertexInput) {
            return attribute.location == vertexInput.location;
        });
    }), attributes.end());
    return result;
}

} // namespace rubia::render
