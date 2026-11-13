#include "vulkan/PipelineDescriptionAdapter.hpp"
#include "PipelineConversions.hpp"
#include "PipelineNativeDescription.hpp"

namespace rubia::rhi::vulkan
{
namespace
{
rhi::BindingType bindingType(VkDescriptorType type)
{
    switch (type)
    {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return rhi::BindingType::UniformBuffer;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return rhi::BindingType::StorageBuffer;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return rhi::BindingType::SampledTexture;
    case VK_DESCRIPTOR_TYPE_SAMPLER: return rhi::BindingType::Sampler;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return rhi::BindingType::CombinedTextureSampler;
    // Unused extra bindings need not be part of the scene ABI. If a shader
    // requires one, the CPU compiler rejects its incompatible type.
    default: return rhi::BindingType::Unknown;
    }
}

} // namespace

rhi::BindingLayoutDesc describeBindingLayout(const DescriptorSetLayoutState& layout)
{
    rhi::BindingLayoutDesc result;
    result.bindings.reserve(layout.bindings().size());
    for (const auto& binding : layout.bindings())
    {
        rhi::ShaderStageMask stages = 0;
        for (auto stage : {rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment,
                           rhi::ShaderStage::Compute})
            if (binding.stageFlags & toVulkanStages(rhi::shaderStageMask(stage)))
                stages |= rhi::shaderStageMask(stage);
        result.bindings.push_back(
            {binding.binding, bindingType(binding.descriptorType), binding.descriptorCount, stages});
    }
    return result;
}

void applyPipelinePolicy(GraphicsPipeline::CreateInfo& destination,
                         const render::PipelinePolicyDesc& policy)
{
    using pipeline_detail::toNative;
    destination.vertexBindings.clear();
    destination.vertexAttributes.clear();
    destination.pushConstantRanges.clear();
    for (const auto& binding : policy.vertexLayout.bindings)
        destination.vertexBindings.push_back({binding.binding, binding.stride, toNative(binding.rate)});
    for (const auto& attribute : policy.vertexLayout.attributes)
        destination.vertexAttributes.push_back({attribute.location, attribute.binding, toNative(attribute.format), attribute.offset});
    for (const auto& range : policy.constantRanges)
        destination.pushConstantRanges.push_back({toVulkanStages(range.stages), range.offset, range.size});
    applySpecializations(destination, policy.specializations);
    if (policy.cullMode) destination.rasterization.cullMode = toNative(*policy.cullMode);
    if (policy.depthTestEnabled) destination.depthStencil.depthTestEnable = *policy.depthTestEnabled;
    if (policy.depthWriteEnabled) destination.depthStencil.depthWriteEnable = *policy.depthWriteEnabled;
    if (policy.depthCompare) destination.depthStencil.depthCompareOp = toNative(*policy.depthCompare);
    if (policy.colorBlend)
    {
        const auto& blend = *policy.colorBlend;
        for (auto& attachment : destination.colorBlend.attachments)
        {
            attachment.blendEnable = blend.enabled;
            attachment.srcColorBlendFactor = toNative(blend.sourceColor);
            attachment.dstColorBlendFactor = toNative(blend.destinationColor);
            attachment.colorBlendOp = toNative(blend.colorOperation);
            attachment.srcAlphaBlendFactor = toNative(blend.sourceAlpha);
            attachment.dstAlphaBlendFactor = toNative(blend.destinationAlpha);
            attachment.alphaBlendOp = toNative(blend.alphaOperation);
        }
    }
}
} // namespace rubia::rhi::vulkan
