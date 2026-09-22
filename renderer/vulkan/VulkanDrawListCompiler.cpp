#include "vulkan/VulkanDrawListCompiler.hpp"

#include "render/MaterialKey.hpp"
#include "render/RenderData.hpp"
#include "render/RenderList.hpp"
#include "render/RenderQueue.hpp"
#include "vulkan/DefaultPipelineFactory.hpp"
#include "vulkan/GpuMaterial.hpp"
#include "vulkan/GraphicsPipelineCache.hpp"
#include "vulkan/Mesh.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include <algorithm>
#include <cstring>
#include <map>

#include <stdexcept>
#include <vector>

namespace rubia::rhi::vulkan
{
namespace
{

void validatePassPipeline(const GraphicsPipeline::CreateInfo &passPipeline)
{
    if (!passPipeline.renderPass ||
        passPipeline.subpass >= passPipeline.renderPass->subpassCount() ||
        passPipeline.descriptorSetLayouts.empty() || !passPipeline.descriptorSetLayouts.front())
        throw std::invalid_argument("draw compilation requires a pass/subpass and frame layout");
    if (!passPipeline.specializations.empty())
        throw std::invalid_argument(
            "scene shader specialization needs a per-program variant contract");
    for (auto state : passPipeline.dynamicStates)
        if (state != VK_DYNAMIC_STATE_VIEWPORT && state != VK_DYNAMIC_STATE_SCISSOR)
            throw std::invalid_argument("scene recorder only supplies dynamic viewport/scissor");
}

VkCompareOp compareOp(asset::DepthCompare compare)
{
    switch (compare)
    {
    case asset::DepthCompare::Never:
        return VK_COMPARE_OP_NEVER;
    case asset::DepthCompare::Less:
        return VK_COMPARE_OP_LESS;
    case asset::DepthCompare::Equal:
        return VK_COMPARE_OP_EQUAL;
    case asset::DepthCompare::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case asset::DepthCompare::Greater:
        return VK_COMPARE_OP_GREATER;
    case asset::DepthCompare::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case asset::DepthCompare::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case asset::DepthCompare::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    throw std::invalid_argument("unsupported material depth comparison");
}

VkDescriptorType descriptorType(asset::ShaderResourceType type)
{
    switch (type)
    {
    case asset::ShaderResourceType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case asset::ShaderResourceType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case asset::ShaderResourceType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case asset::ShaderResourceType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case asset::ShaderResourceType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    throw std::invalid_argument("unsupported draw shader descriptor type");
}

VkFormat vertexFormat(asset::ShaderValueType type)
{
    switch (type)
    {
    case asset::ShaderValueType::Float:
        return VK_FORMAT_R32_SFLOAT;
    case asset::ShaderValueType::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case asset::ShaderValueType::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case asset::ShaderValueType::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
        throw std::invalid_argument("draw shader requires an unsupported vertex input type");
    }
}

void validateInterface(const GraphicsPipeline::CreateInfo &info)
{
    const auto &interface = info.program->source().interface();
    for (const auto &resource : interface.bindings)
    {
        if (resource.set >= info.descriptorSetLayouts.size())
            throw std::invalid_argument(
                "draw shader requires descriptor sets outside frame/material");
        const auto &bindings = info.descriptorSetLayouts[resource.set]->bindings();
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto &binding) {
            return binding.binding == resource.binding;
        });
        VkShaderStageFlags stages = 0;
        if (resource.stages & asset::shaderStageMask(asset::ShaderStage::Vertex))
            stages |= VK_SHADER_STAGE_VERTEX_BIT;
        if (resource.stages & asset::shaderStageMask(asset::ShaderStage::Fragment))
            stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (found == bindings.end() || found->descriptorType != descriptorType(resource.type) ||
            found->descriptorCount != resource.arrayCount || (found->stageFlags & stages) != stages)
            throw std::invalid_argument(
                "draw shader is incompatible with frame/material descriptors");
    }
    for (const auto &range : interface.pushConstants)
        if (range.offset > sizeof(render::DrawPushConstants) ||
            range.size > sizeof(render::DrawPushConstants) - range.offset)
            throw std::invalid_argument("draw shader exceeds the DrawPushConstants ABI");
    for (const auto &input : interface.vertexInputs)
    {
        const auto found = std::find_if(
            info.vertexAttributes.begin(), info.vertexAttributes.end(),
            [&](const auto &attribute) { return attribute.location == input.location; });
        if (found == info.vertexAttributes.end() || found->format != vertexFormat(input.type))
            throw std::invalid_argument("draw shader is incompatible with asset::Vertex");
    }
    for (const auto &output : interface.fragmentOutputs)
        if (output.location >= info.renderPass->colorAttachmentCount(info.subpass))
            throw std::invalid_argument("draw shader output is outside the pass color attachments");
}

void specializeAlphaClip(GraphicsPipeline::CreateInfo &info, bool enabled)
{
    for (const auto &shader : info.program->shaders())
    {
        if (shader->source().stage() != asset::ShaderStage::Fragment)
            continue;
        const auto &interface = shader->source().interface();
        const auto constant =
            std::find_if(interface.specializationConstants.begin(),
                         interface.specializationConstants.end(), [](const auto &value) {
                             return value.constantId == render::AlphaClipSpecializationId;
                         });
        if (constant == interface.specializationConstants.end())
            break;
        if (constant->type != asset::ShaderValueType::Bool)
            throw std::invalid_argument("AlphaClip specialization constant 1000 must be bool");
        bool hasThreshold = false;
        for (const auto &push : interface.pushConstants)
            for (const auto &member : push.members)
                hasThreshold |=
                    member.offset == offsetof(render::DrawPushConstants, alphaClipThreshold) &&
                    member.type == asset::ShaderValueType::Float && member.size == sizeof(float) &&
                    member.arrayCount == 1;
        if (!hasThreshold)
            throw std::invalid_argument(
                "AlphaClip shader requires float threshold at push-constant offset 8");
        PipelineStageSpecialization specialization;
        specialization.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        // Vulkan specialization booleans use VkBool32, not sizeof(C++ bool).
        const VkBool32 value = enabled ? VK_TRUE : VK_FALSE;
        specialization.entries = {{render::AlphaClipSpecializationId, 0, sizeof(value)}};
        specialization.data.resize(sizeof(value));
        std::memcpy(specialization.data.data(), &value, sizeof(value));
        info.specializations.push_back(std::move(specialization));
        return;
    }
    if (enabled)
        throw std::invalid_argument(
            "material enables AlphaClip but its fragment shader does not support it");
}

GraphicsPipeline::CreateInfo describePipeline(const GpuMaterialTemplate &materialTemplate,
                                              const render::PipelineVariantKey &key,
                                              const GraphicsPipeline::CreateInfo &passPipeline)
{
    const auto featureBits = render::shaderFeatureBits(key.shaderFeatures);
    const auto alphaClipBit = render::shaderFeatureBits(render::ShaderFeatureFlags::AlphaClip);
    if (featureBits & ~alphaClipBit)
        throw std::invalid_argument("unsupported scene shader feature");
    if (materialTemplate.source().parameterBlock().descriptor.set != 1)
        throw std::invalid_argument("scene draws require material descriptors at set 1");
    auto info = passPipeline;
    // Shader-specific data from the bootstrap pipeline must not leak to other programs.
    auto draw =
        makeDefaultScenePipeline(materialTemplate.program(), materialTemplate.layoutReference());
    info.program = std::move(draw.program);
    info.vertexShaderSpirv.clear();
    info.fragmentShaderSpirv.clear();
    info.vertexBindings = std::move(draw.vertexBindings);
    info.vertexAttributes = std::move(draw.vertexAttributes);
    info.pushConstantRanges = std::move(draw.pushConstantRanges);
    info.descriptorSetLayouts = {passPipeline.descriptorSetLayouts.front(),
                                 materialTemplate.layoutReference()};
    info.depthStencil.depthTestEnable = key.depthTestEnabled;
    info.depthStencil.depthWriteEnable = key.depthWriteEnabled;
    info.depthStencil.depthCompareOp = compareOp(key.depthCompare);
    switch (key.cullMode)
    {
    case render::PipelineCullMode::None:
        info.rasterization.cullMode = VK_CULL_MODE_NONE;
        break;
    case render::PipelineCullMode::Front:
        info.rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    case render::PipelineCullMode::Back:
        info.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    default:
        throw std::invalid_argument("unsupported material cull mode");
    }
    for (auto &blend : info.colorBlend.attachments)
    {
        blend.blendEnable = key.blendMode == render::PipelineBlendMode::Alpha;
        blend.srcColorBlendFactor =
            blend.blendEnable ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor =
            blend.blendEnable ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor =
            blend.blendEnable ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    specializeAlphaClip(info, (featureBits & alphaClipBit) != 0);
    validateInterface(info);
    // Keep the canonical mesh stride/offsets, but declare only inputs this VS consumes.
    const auto &inputs = info.program->source().interface().vertexInputs;
    info.vertexAttributes.erase(
        std::remove_if(info.vertexAttributes.begin(), info.vertexAttributes.end(),
                       [&](const auto &attribute) {
                           return std::none_of(inputs.begin(), inputs.end(),
                                               [&](const auto &input) {
                                                   return input.location == attribute.location;
                                               });
                       }),
        info.vertexAttributes.end());
    return info;
}

// A frame can contain thousands of objects sharing one resident material variant.
// The full content key is built once per distinct snapshot/variant in this compile.
using ResolvedPipelines =
    std::map<const GpuMaterialTemplate *,
             std::map<render::PipelineVariantKey, std::shared_ptr<const GraphicsPipeline>,
                      render::PipelineVariantKeyLess>>;

std::vector<VulkanDrawItem> compileItems(const std::vector<render::RenderItem> &source,
                                         std::size_t objectCount, bool transparent,
                                         const RenderAssetCache &resources,
                                         const GraphicsPipeline::CreateInfo &passPipeline,
                                         GraphicsPipelineCache &pipelines,
                                         ResolvedPipelines &resolved)
{
    std::vector<VulkanDrawItem> result;
    result.reserve(source.size());
    for (const render::RenderItem &item : source)
    {
        if (!item.mesh || !item.material || !item.materialKey || !item.pipelineKey ||
            item.materialKey != render::makeMaterialKey(item.material) ||
            item.objectIndex >= objectCount ||
            render::isTransparentQueue(item.queue) != transparent)
        {
            throw std::invalid_argument("RenderList contains an invalid backend-neutral draw item");
        }

        const Mesh *mesh = resources.tryMesh(item.mesh);
        const GpuMaterial *material = resources.tryMaterial(item.material);
        if (mesh == nullptr || material == nullptr)
        {
            throw std::invalid_argument("RenderList references a non-resident Vulkan resource");
        }
        if (item.submeshIndex >= mesh->submeshes().size())
        {
            throw std::out_of_range("RenderList references an invalid Vulkan submesh");
        }

        const asset::MaterialRenderState &renderState = material->renderState();
        if (material->materialTemplate() != item.pipelineKey.materialTemplate ||
            render::renderQueueFor(renderState) != item.queue ||
            render::makePipelineVariantKey(item.pipelineKey.materialTemplate, renderState) !=
                item.pipelineKey)
        {
            throw std::invalid_argument(
                "RenderList material state does not match its semantic keys");
        }

        const auto preparedTemplate = resources.materialTemplateForMaterial(item.material);
        if (!preparedTemplate)
            throw std::invalid_argument("resident material has no prepared template snapshot");
        auto &variants = resolved[preparedTemplate.get()];
        auto found = variants.find(item.pipelineKey);
        if (found == variants.end())
            found =
                variants
                    .emplace(item.pipelineKey,
                             pipelines.getOrCreate(VulkanDrawListCompiler::describeMaterialPipeline(
                                 *preparedTemplate, item.pipelineKey, passPipeline)))
                    .first;
        result.push_back(
            {mesh, material, item.pipelineKey, found->second, item.submeshIndex, item.objectIndex});
    }
    return result;
}

} // namespace

GraphicsPipeline::CreateInfo VulkanDrawListCompiler::describeMaterialPipeline(
    const GpuMaterialTemplate &materialTemplate, const render::PipelineVariantKey &key,
    const GraphicsPipeline::CreateInfo &passPipeline)
{
    validatePassPipeline(passPipeline);
    return describePipeline(materialTemplate, key, passPipeline);
}

VulkanDrawList VulkanDrawListCompiler::compile(const render::RenderList &source,
                                               const RenderAssetCache &resources,
                                               const GraphicsPipeline::CreateInfo &passPipeline,
                                               GraphicsPipelineCache &pipelines) const
{
    validatePassPipeline(passPipeline);
    ResolvedPipelines resolved;
    VulkanDrawList result{};
    result.opaque = compileItems(source.opaque, source.objectData.size(), false, resources,
                                 passPipeline, pipelines, resolved);
    result.transparent = compileItems(source.transparent, source.objectData.size(), true, resources,
                                      passPipeline, pipelines, resolved);
    return result;
}

} // namespace rubia::rhi::vulkan
