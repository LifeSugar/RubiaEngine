#include "pipeline/PipelineKey.hpp"
#include "pipeline/PipelineKeyData.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace rubia::render
{
PipelineKey PipelineKey::from(const rhi::GraphicsPipelineDesc& info)
{
    detail::PipelineKeyData key;
    key.add(1);
    key.count(info.attachments.colors.size());
    for (const auto& color : info.attachments.colors)
    {
        key.add(color.format);
        key.add(color.resolveFormat);
    }
    key.add(info.attachments.depthStencilFormat);
    key.add(info.attachments.samples);
    key.add(static_cast<bool>(info.target));
    if (info.target) key.append(info.target->compatibilityKey());
    auto shaders = info.shaders;
    for (const auto& shader : shaders)
        if (!shader) throw std::invalid_argument("pipeline key requires shader snapshots");
    std::sort(shaders.begin(), shaders.end(), [](const auto& a, const auto& b) {
        return a->code().stage < b->code().stage;
    });
    key.count(shaders.size());
    for (const auto& shader : shaders)
    {
        key.add(shader->code().stage);
        key.string(shader->code().entryPoint);
        const auto& artifact = shader->code().artifact;
        key.add(artifact.target.backend);
        key.add(artifact.format);
        key.string(artifact.target.profile);
        key.bytes(artifact.bytes.data(), artifact.bytes.size());
    }
    auto specializations = info.specializations;
    std::sort(specializations.begin(), specializations.end(), [](const auto& a, const auto& b) {
        return std::tie(a.stage, a.id) < std::tie(b.stage, b.id);
    });
    key.count(specializations.size());
    for (const auto& constant : specializations)
    {
        key.add(constant.stage);
        key.add(constant.id);
        std::visit([&](auto value) {
            if constexpr (std::is_same_v<decltype(value), std::vector<std::byte>>)
                key.bytes(value.data(), value.size());
            else if constexpr (std::is_same_v<decltype(value), bool>)
            {
                const uint32_t stored = value ? 1u : 0u;
                key.bytes(&stored, sizeof(stored));
            }
            else key.bytes(&value, sizeof(value));
        }, constant.value);
    }
    key.count(info.bindingLayouts.size());
    for (const auto& layout : info.bindingLayouts)
    {
        if (!layout) throw std::invalid_argument("pipeline key requires binding layout snapshots");
        auto bindings = layout->description().bindings;
        std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
            return a.binding < b.binding;
        });
        key.count(bindings.size());
        for (const auto& binding : bindings)
        {
            key.add(binding.binding); key.add(binding.type);
            key.add(binding.count); key.add(binding.stages);
        }
    }
    auto ranges = info.constantRanges;
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
        return std::tie(a.offset, a.size, a.stages) < std::tie(b.offset, b.size, b.stages);
    });
    key.count(ranges.size());
    for (const auto& range : ranges)
    { key.add(range.stages); key.add(range.offset); key.add(range.size); }
    auto bindings = info.vertexLayout.bindings;
    std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
    key.count(bindings.size());
    for (const auto& binding : bindings)
    { key.add(binding.binding); key.add(binding.stride); key.add(binding.rate); }
    auto attributes = info.vertexLayout.attributes;
    std::sort(attributes.begin(), attributes.end(), [](const auto& a, const auto& b) { return a.location < b.location; });
    key.count(attributes.size());
    for (const auto& attribute : attributes)
    { key.add(attribute.location); key.add(attribute.binding); key.add(attribute.format); key.add(attribute.offset); }
    auto dynamics = info.dynamicStates;
    std::sort(dynamics.begin(), dynamics.end());
    key.count(dynamics.size());
    for (auto dynamic : dynamics) key.add(dynamic);
    const auto isDynamic = [&](rhi::DynamicState state) {
        return std::binary_search(dynamics.begin(), dynamics.end(), state);
    };
    key.add(info.inputAssembly.topology); key.add(info.inputAssembly.primitiveRestart);
    const auto& raster = info.rasterization;
    key.add(raster.depthClamp); key.add(raster.discard); key.add(raster.polygonMode);
    key.add(raster.cullMode); key.add(raster.frontFace); key.add(raster.depthBias);
    if (!isDynamic(rhi::DynamicState::DepthBias))
    { key.real(raster.depthBiasConstant); key.real(raster.depthBiasClamp); key.real(raster.depthBiasSlope); }
    if (!isDynamic(rhi::DynamicState::LineWidth)) key.real(raster.lineWidth);
    const auto& depth = info.depthStencil;
    key.add(depth.depthTest); key.add(depth.depthWrite); key.add(depth.depthCompare);
    key.add(depth.depthBounds); key.add(depth.stencilTest);
    const auto stencil = [&](const rhi::StencilFaceDesc& face) {
        key.add(face.fail); key.add(face.pass); key.add(face.depthFail); key.add(face.compare);
        if (!isDynamic(rhi::DynamicState::StencilCompareMask)) key.add(face.compareMask);
        if (!isDynamic(rhi::DynamicState::StencilWriteMask)) key.add(face.writeMask);
        if (!isDynamic(rhi::DynamicState::StencilReference)) key.add(face.reference);
    };
    stencil(depth.front); stencil(depth.back);
    if (!isDynamic(rhi::DynamicState::DepthBounds))
    { key.real(depth.minDepthBounds); key.real(depth.maxDepthBounds); }
    const auto& samples = info.multisample;
    key.add(samples.samples); key.add(samples.sampleShading); key.real(samples.minSampleShading);
    key.add(samples.alphaToCoverage); key.add(samples.alphaToOne);
    if (!samples.samples || samples.samples > 64 || (samples.samples & (samples.samples - 1)) ||
        (!samples.sampleMask.empty() && samples.sampleMask.size() != (samples.samples + 31) / 32))
        throw std::invalid_argument("invalid sample count/mask in pipeline key");
    for (uint32_t i = 0; i < (samples.samples + 31) / 32; ++i)
    {
        uint32_t mask = samples.sampleMask.empty() ? ~0u : samples.sampleMask[i];
        if (samples.samples < 32) mask &= (1u << samples.samples) - 1;
        key.add(mask);
    }
    key.add(info.colorBlend.logicOpEnabled); key.add(info.colorBlend.logicOp);
    key.count(info.colorBlend.attachments.size());
    for (const auto& attachment : info.colorBlend.attachments)
    {
        const auto& blend = attachment.blend;
        key.add(blend.enabled); key.add(blend.sourceColor); key.add(blend.destinationColor);
        key.add(blend.colorOperation); key.add(blend.sourceAlpha); key.add(blend.destinationAlpha);
        key.add(blend.alphaOperation); key.add(attachment.writeMask);
    }
    if (!isDynamic(rhi::DynamicState::BlendConstants))
        for (auto value : info.colorBlend.constants) key.real(value);
    key.count(info.viewport.viewports.size());
    if (!isDynamic(rhi::DynamicState::Viewport))
        for (const auto& viewport : info.viewport.viewports)
        {
            key.real(viewport.x); key.real(viewport.y); key.real(viewport.width);
            key.real(viewport.height); key.real(viewport.minDepth); key.real(viewport.maxDepth);
        }
    key.count(info.viewport.scissors.size());
    if (!isDynamic(rhi::DynamicState::Scissor))
        for (const auto& scissor : info.viewport.scissors)
        { key.add(scissor.x); key.add(scissor.y); key.add(scissor.width); key.add(scissor.height); }
    PipelineKey result;
    result.words_ = std::move(key.words);
    uint64_t hash = 14695981039346656037ull;
    for (auto word : result.words_) { hash ^= word; hash *= 1099511628211ull; }
    result.hash_ = static_cast<size_t>(hash);
    return result;
}
} // namespace rubia::render
