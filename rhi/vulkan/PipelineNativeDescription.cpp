#include "SpirvCode.hpp"
#include "PipelineNativeDescription.hpp"
#include "PipelineConversions.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace rubia::rhi::vulkan
{
namespace
{
struct NativeTarget final : rhi::PipelineTargetResource
{
    DeviceId device = 0;
    std::shared_ptr<const RenderPassState> pass;
    DeviceId deviceId() const noexcept override { return device; }
    const AttachmentSignature& description() const noexcept override { return *pass->attachmentSignature(); }
    const std::vector<uint32_t>& compatibilityKey() const noexcept override { return pass->compatibilityKey(); }
};
rhi::ShaderStageMask stages(VkShaderStageFlags value)
{
    constexpr auto known = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    if (value & ~known) throw std::invalid_argument("shader visibility is outside the RHI subset");
    rhi::ShaderStageMask result = 0;
    if (value & VK_SHADER_STAGE_VERTEX_BIT) result |= rhi::shaderStageMask(rhi::ShaderStage::Vertex);
    if (value & VK_SHADER_STAGE_FRAGMENT_BIT) result |= rhi::shaderStageMask(rhi::ShaderStage::Fragment);
    if (value & VK_SHADER_STAGE_COMPUTE_BIT) result |= rhi::shaderStageMask(rhi::ShaderStage::Compute);
    return result;
}
VkStencilOpState nativeStencil(const rhi::StencilFaceDesc& d)
{
    using pipeline_detail::toNative;
    return {toNative(d.fail), toNative(d.pass), toNative(d.depthFail), toNative(d.compare),
            d.compareMask, d.writeMask, d.reference};
}
rhi::StencilFaceDesc stencil(const VkStencilOpState& d)
{
    using namespace pipeline_detail;
    return {fromNativeStencilOp(d.failOp), fromNativeStencilOp(d.passOp), fromNativeStencilOp(d.depthFailOp),
            fromNativeCompareOp(d.compareOp), d.compareMask, d.writeMask, d.reference};
}
}
std::shared_ptr<const RenderPassState> nativePipelineTarget(const rhi::PipelineTargetRef& target)
{
    const auto* native = dynamic_cast<const NativeTarget*>(target.get());
    if (!native) throw std::invalid_argument("pipeline target does not belong to Vulkan");
    return native->pass;
}
void applySpecializations(GraphicsPipeline::CreateInfo& destination,
                          const std::vector<rhi::SpecializationConstant>& constants)
{
    destination.specializations.clear();
    for (const auto& constant : constants)
    {
        const auto stage = static_cast<VkShaderStageFlagBits>(toVulkanStages(rhi::shaderStageMask(constant.stage)));
        auto found = std::find_if(destination.specializations.begin(), destination.specializations.end(),
            [&](const auto& value) { return value.stage == stage; });
        if (found == destination.specializations.end())
        {
            destination.specializations.push_back({stage, {}, {}});
            found = destination.specializations.end() - 1;
        }
        const auto append = [&](const void* data, size_t size) {
            const auto offset = found->data.size();
            if (!size || offset > std::numeric_limits<uint32_t>::max() ||
                size > std::numeric_limits<uint32_t>::max() - offset)
                throw std::invalid_argument("invalid specialization value size");
            found->entries.push_back({constant.id, static_cast<uint32_t>(offset), size});
            found->data.resize(offset + size);
            std::memcpy(found->data.data() + offset, data, size);
        };
        std::visit([&](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, std::vector<std::byte>>) append(value.data(), value.size());
            else if constexpr (std::is_same_v<Value, bool>)
            {
                const VkBool32 stored = value ? VK_TRUE : VK_FALSE;
                append(&stored, sizeof(stored));
            }
            else append(&value, sizeof(value));
        }, constant.value);
    }
}

GraphicsPipeline::CreateInfo nativeGraphicsPipelineDescription(const rhi::GraphicsPipelineDesc& d)
{
    using pipeline_detail::toNative;
    GraphicsPipeline::CreateInfo result;
    result.shaders = d.shaders;
    for (const auto& layout : d.bindingLayouts) result.descriptorSetLayouts.push_back(nativeBindingLayout(layout));
    for (const auto& range : d.constantRanges)
        result.pushConstantRanges.push_back({toVulkanStages(range.stages), range.offset, range.size});
    applySpecializations(result, d.specializations);
    for (const auto& b : d.vertexLayout.bindings) result.vertexBindings.push_back({b.binding, b.stride, toNative(b.rate)});
    for (const auto& a : d.vertexLayout.attributes) result.vertexAttributes.push_back({a.location, a.binding, toNative(a.format), a.offset});
    result.inputAssembly = {toNative(d.inputAssembly.topology), d.inputAssembly.primitiveRestart};
    const auto& r = d.rasterization;
    result.rasterization = {r.depthClamp, r.discard, toNative(r.polygonMode), toNative(r.cullMode), toNative(r.frontFace),
                           r.depthBias, r.depthBiasConstant, r.depthBiasClamp, r.depthBiasSlope, r.lineWidth};
    const auto& depth = d.depthStencil;
    result.depthStencil = {depth.depthTest, depth.depthWrite, toNative(depth.depthCompare), depth.depthBounds,
                           depth.stencilTest, nativeStencil(depth.front), nativeStencil(depth.back),
                           depth.minDepthBounds, depth.maxDepthBounds};
    const auto& m = d.multisample;
    result.multisample = {static_cast<VkSampleCountFlagBits>(m.samples), m.sampleShading, m.minSampleShading,
                         m.sampleMask, m.alphaToCoverage, m.alphaToOne};
    result.colorBlend.logicOpEnable = d.colorBlend.logicOpEnabled;
    result.colorBlend.logicOp = toNative(d.colorBlend.logicOp);
    result.colorBlend.constants = d.colorBlend.constants;
    result.colorBlend.attachments.clear();
    for (const auto& attachment : d.colorBlend.attachments)
    {
        const auto& b = attachment.blend;
        result.colorBlend.attachments.push_back({b.enabled, toNative(b.sourceColor), toNative(b.destinationColor),
            toNative(b.colorOperation), toNative(b.sourceAlpha), toNative(b.destinationAlpha), toNative(b.alphaOperation), attachment.writeMask});
    }
    result.viewport.viewports.clear(); result.viewport.scissors.clear(); result.dynamicStates.clear();
    for (const auto& v : d.viewport.viewports) result.viewport.viewports.push_back({v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth});
    for (const auto& s : d.viewport.scissors) result.viewport.scissors.push_back({{s.x,s.y},{s.width,s.height}});
    for (auto dynamic : d.dynamicStates) result.dynamicStates.push_back(toNative(dynamic));
    return result;
}

rhi::GraphicsPipelineDesc describeGraphicsPipeline(const GraphicsPipeline::CreateInfo& d,
                                                   const VulkanResourceDevice& device)
{
    using namespace pipeline_detail;
    GraphicsPipeline::validate(device.native(), d);
    if (d.subpass != 0 || !d.renderPass->attachmentSignature())
        throw std::invalid_argument("native render pass is outside the RHI single-subpass subset");
    rhi::GraphicsPipelineDesc result;
    result.attachments = *d.renderPass->attachmentSignature();
    auto target = std::make_shared<NativeTarget>();
    target->device = device.id(); target->pass = d.renderPass;
    result.target = std::move(target);
    result.shaders = d.shaders;
    if (result.shaders.empty())
    {
        result.shaders.push_back(device.createShader({rhi::ShaderStage::Vertex, d.vertexEntryPoint, spirvArtifact(d.vertexShaderSpirv)}));
        if (!d.fragmentShaderSpirv.empty()) result.shaders.push_back(device.createShader({rhi::ShaderStage::Fragment,d.fragmentEntryPoint,spirvArtifact(d.fragmentShaderSpirv)}));
    }
    for (const auto& layout : d.descriptorSetLayouts) result.bindingLayouts.push_back(borrowBindingLayout(device, layout));
    for (const auto& range : d.pushConstantRanges) result.constantRanges.push_back({stages(range.stageFlags),range.offset,range.size});
    for (const auto& specialization : d.specializations)
        for (const auto& entry : specialization.entries)
        {
            std::vector<std::byte> bytes(entry.size);
            std::memcpy(bytes.data(),specialization.data.data()+entry.offset,entry.size);
            const auto stage = specialization.stage == VK_SHADER_STAGE_VERTEX_BIT ? rhi::ShaderStage::Vertex : rhi::ShaderStage::Fragment;
            result.specializations.push_back({stage,entry.constantID,std::move(bytes)});
        }
    for (const auto& b : d.vertexBindings) result.vertexLayout.bindings.push_back({b.binding,b.stride,fromNativeVertexInputRate(b.inputRate)});
    for (const auto& a : d.vertexAttributes) result.vertexLayout.attributes.push_back({a.location,a.binding,fromNativeVertexFormat(a.format),a.offset});
    result.inputAssembly = {fromNativePrimitiveTopology(d.inputAssembly.topology),d.inputAssembly.primitiveRestartEnable != 0};
    const auto& r=d.rasterization;
    result.rasterization = {r.depthClampEnable != 0,r.rasterizerDiscardEnable != 0,fromNativePolygonMode(r.polygonMode),
        fromNativeCullMode(r.cullMode),fromNativeFrontFace(r.frontFace),r.depthBiasEnable != 0,r.depthBiasConstantFactor,r.depthBiasClamp,r.depthBiasSlopeFactor,r.lineWidth};
    const auto& depth=d.depthStencil;
    result.depthStencil = {depth.depthTestEnable != 0,depth.depthWriteEnable != 0,fromNativeCompareOp(depth.depthCompareOp),
        depth.depthBoundsTestEnable != 0,depth.stencilTestEnable != 0,stencil(depth.front),stencil(depth.back),depth.minDepthBounds,depth.maxDepthBounds};
    const auto& m=d.multisample;
    result.multisample = {static_cast<uint32_t>(m.samples),m.sampleShadingEnable != 0,m.minSampleShading,m.sampleMask,m.alphaToCoverageEnable != 0,m.alphaToOneEnable != 0};
    result.colorBlend.logicOpEnabled=d.colorBlend.logicOpEnable != 0;
    result.colorBlend.logicOp=fromNativeLogicOp(d.colorBlend.logicOp);
    result.colorBlend.constants=d.colorBlend.constants;
    result.colorBlend.attachments.clear();
    for(const auto& a:d.colorBlend.attachments) result.colorBlend.attachments.push_back({
        {a.blendEnable != 0,fromNativeBlendFactor(a.srcColorBlendFactor),fromNativeBlendFactor(a.dstColorBlendFactor),fromNativeBlendOp(a.colorBlendOp),
         fromNativeBlendFactor(a.srcAlphaBlendFactor),fromNativeBlendFactor(a.dstAlphaBlendFactor),fromNativeBlendOp(a.alphaBlendOp)},a.colorWriteMask});
    result.viewport.viewports.clear(); result.viewport.scissors.clear(); result.dynamicStates.clear();
    for(const auto& v:d.viewport.viewports) result.viewport.viewports.push_back({v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth});
    for(const auto& s:d.viewport.scissors) result.viewport.scissors.push_back({s.offset.x,s.offset.y,s.extent.width,s.extent.height});
    for(auto state:d.dynamicStates) result.dynamicStates.push_back(fromNativeDynamicState(state));
    return result;
}
} // namespace rubia::rhi::vulkan
