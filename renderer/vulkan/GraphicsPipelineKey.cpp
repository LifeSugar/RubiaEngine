#include "vulkan/GraphicsPipelineKey.hpp"
#include "vulkan/GpuPreparedAssets.hpp"
#include "vulkan/PipelineKeyData.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace rubia::rhi::vulkan
{
GraphicsPipelineKey GraphicsPipelineKey::from(const GraphicsPipeline::CreateInfo &info)
{
    if (!info.renderPass)
        throw std::invalid_argument("pipeline key requires a render pass snapshot");
    detail::PipelineKeyData key;
    key.add(1); // Encoding version, not a disk cache format.
    key.append(info.renderPass->compatibilityKey());
    key.add(info.subpass);

    const auto shader = [&](VkShaderStageFlagBits stage, const std::string &entry,
                            const std::vector<uint32_t> &code) {
        key.add(stage);
        key.string(entry);
        key.append(code);
    };
    if (info.program)
    {
        // Use the immutable shader snapshots, not recyclable modules or an asset
        // handle. Dependency revisions changing code/entry points change this key.
        auto shaders = info.program->shaders();
        std::sort(shaders.begin(), shaders.end(), [](const auto &a, const auto &b) {
            return a->stageInfo().stage < b->stageInfo().stage;
        });
        key.count(shaders.size());
        for (const auto &s : shaders)
            shader(s->stageInfo().stage, s->source().entryPoint(), s->source().spirv());
    }
    else
    {
        key.count(info.fragmentShaderSpirv.empty() ? 1 : 2);
        shader(VK_SHADER_STAGE_VERTEX_BIT, info.vertexEntryPoint, info.vertexShaderSpirv);
        if (!info.fragmentShaderSpirv.empty())
            shader(VK_SHADER_STAGE_FRAGMENT_BIT, info.fragmentEntryPoint, info.fragmentShaderSpirv);
    }

    auto specializations = info.specializations;
    specializations.erase(std::remove_if(specializations.begin(), specializations.end(),
                                         [](const auto &s) { return s.entries.empty(); }),
                          specializations.end());
    std::sort(specializations.begin(), specializations.end(),
              [](const auto &a, const auto &b) { return a.stage < b.stage; });
    key.count(specializations.size());
    for (auto &s : specializations)
    {
        key.add(s.stage);
        std::sort(s.entries.begin(), s.entries.end(),
                  [](const auto &a, const auto &b) { return a.constantID < b.constantID; });
        key.count(s.entries.size());
        for (const auto &e : s.entries)
        {
            if (e.offset >= s.data.size() || e.size > s.data.size() - e.offset)
                throw std::invalid_argument("pipeline specialization range is out of bounds");
            key.add(e.constantID);
            key.bytes(s.data.data() + e.offset, e.size); // Ignore byte offsets/padding/order.
        }
    }

    key.count(info.descriptorSetLayouts.size());
    for (const auto &layout : info.descriptorSetLayouts)
    {
        if (!layout)
            throw std::invalid_argument("pipeline key requires layout snapshots");
        key.append(layout->key()); // Set index/order is significant, handle is not.
    }
    auto ranges = info.pushConstantRanges;
    std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) {
        return std::tie(a.offset, a.size, a.stageFlags) < std::tie(b.offset, b.size, b.stageFlags);
    });
    key.count(ranges.size());
    for (const auto &r : ranges)
    {
        key.add(r.stageFlags);
        key.add(r.offset);
        key.add(r.size);
    }

    auto bindings = info.vertexBindings;
    std::sort(bindings.begin(), bindings.end(),
              [](const auto &a, const auto &b) { return a.binding < b.binding; });
    key.count(bindings.size());
    for (const auto &b : bindings)
    {
        key.add(b.binding);
        key.add(b.stride);
        key.add(b.inputRate);
    }
    auto attributes = info.vertexAttributes;
    std::sort(attributes.begin(), attributes.end(),
              [](const auto &a, const auto &b) { return a.location < b.location; });
    key.count(attributes.size());
    for (const auto &a : attributes)
    {
        key.add(a.location);
        key.add(a.binding);
        key.add(a.format);
        key.add(a.offset);
    }

    auto dynamics = info.dynamicStates;
    std::sort(dynamics.begin(), dynamics.end());
    key.count(dynamics.size());
    for (auto d : dynamics)
        key.add(d);
    const auto isDynamic = [&](VkDynamicState d) {
        return std::binary_search(dynamics.begin(), dynamics.end(), d);
    };
    key.add(info.inputAssembly.topology);
    key.add(info.inputAssembly.primitiveRestartEnable);
    const auto &raster = info.rasterization;
    key.add(raster.depthClampEnable);
    key.add(raster.rasterizerDiscardEnable);
    key.add(raster.polygonMode);
    key.add(raster.cullMode);
    key.add(raster.frontFace);
    key.add(raster.depthBiasEnable);
    if (!isDynamic(VK_DYNAMIC_STATE_DEPTH_BIAS))
    {
        key.real(raster.depthBiasConstantFactor);
        key.real(raster.depthBiasClamp);
        key.real(raster.depthBiasSlopeFactor);
    }
    if (!isDynamic(VK_DYNAMIC_STATE_LINE_WIDTH))
        key.real(raster.lineWidth);

    const auto &depth = info.depthStencil;
    key.add(depth.depthTestEnable);
    key.add(depth.depthWriteEnable);
    key.add(depth.depthCompareOp);
    key.add(depth.depthBoundsTestEnable);
    key.add(depth.stencilTestEnable);
    const auto stencil = [&](const VkStencilOpState &s) {
        key.add(s.failOp);
        key.add(s.passOp);
        key.add(s.depthFailOp);
        key.add(s.compareOp);
        if (!isDynamic(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK))
            key.add(s.compareMask);
        if (!isDynamic(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK))
            key.add(s.writeMask);
        if (!isDynamic(VK_DYNAMIC_STATE_STENCIL_REFERENCE))
            key.add(s.reference);
    };
    stencil(depth.front);
    stencil(depth.back);
    if (!isDynamic(VK_DYNAMIC_STATE_DEPTH_BOUNDS))
    {
        key.real(depth.minDepthBounds);
        key.real(depth.maxDepthBounds);
    }

    const auto &samples = info.multisample;
    key.add(samples.samples);
    key.add(samples.sampleShadingEnable);
    key.real(samples.minSampleShading);
    key.add(samples.alphaToCoverageEnable);
    key.add(samples.alphaToOneEnable);
    // NULL sample mask and all-ones mean the same thing; ignore unused high bits.
    const auto sampleCount = static_cast<uint32_t>(samples.samples);
    if (!sampleCount || sampleCount > 64 || (sampleCount & (sampleCount - 1)) ||
        (!samples.sampleMask.empty() && samples.sampleMask.size() != (sampleCount + 31) / 32))
        throw std::invalid_argument("invalid sample count/mask in pipeline key");
    for (uint32_t i = 0; i < (sampleCount + 31) / 32; ++i)
    {
        uint32_t mask = samples.sampleMask.empty() ? ~0u : samples.sampleMask[i];
        if (sampleCount < 32)
            mask &= (1u << sampleCount) - 1;
        key.add(mask);
    }

    const auto &blend = info.colorBlend;
    key.add(blend.logicOpEnable);
    key.add(blend.logicOp);
    key.count(blend.attachments.size());
    for (const auto &a : blend.attachments)
    {
        key.add(a.blendEnable);
        key.add(a.srcColorBlendFactor);
        key.add(a.dstColorBlendFactor);
        key.add(a.colorBlendOp);
        key.add(a.srcAlphaBlendFactor);
        key.add(a.dstAlphaBlendFactor);
        key.add(a.alphaBlendOp);
        key.add(a.colorWriteMask);
    }
    if (!isDynamic(VK_DYNAMIC_STATE_BLEND_CONSTANTS))
        for (auto value : blend.constants)
            key.real(value);
    key.count(info.viewport.viewports.size());
    if (!isDynamic(VK_DYNAMIC_STATE_VIEWPORT))
        for (const auto &v : info.viewport.viewports)
        {
            key.real(v.x);
            key.real(v.y);
            key.real(v.width);
            key.real(v.height);
            key.real(v.minDepth);
            key.real(v.maxDepth);
        }
    key.count(info.viewport.scissors.size());
    if (!isDynamic(VK_DYNAMIC_STATE_SCISSOR))
        for (const auto &s : info.viewport.scissors)
        {
            key.add(static_cast<uint32_t>(s.offset.x));
            key.add(static_cast<uint32_t>(s.offset.y));
            key.add(s.extent.width);
            key.add(s.extent.height);
        }
    GraphicsPipelineKey result;
    result.words_ = std::move(key.words);
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t word : result.words_)
    {
        hash ^= word;
        hash *= 1099511628211ull;
    }
    result.hash_ = static_cast<size_t>(hash);
    return result;
}
} // namespace rubia::rhi::vulkan
