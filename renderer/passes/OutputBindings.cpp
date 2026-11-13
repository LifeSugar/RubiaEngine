#include "passes/OutputBindings.hpp"
namespace rubia::render
{
void OutputBindings::create(const rhi::Device& device, const std::vector<rhi::TextureViewRef>& views)
{
    OutputBindings replacement;
    rhi::SamplerDesc sampler;
    sampler.mipFilter = rhi::Filter::Nearest;
    sampler.addressU = sampler.addressV = sampler.addressW = rhi::AddressMode::ClampToEdge;
    replacement.sampler_ = device.createSampler(sampler);
    const auto fragment = rhi::shaderStageMask(rhi::ShaderStage::Fragment);
    replacement.layout_ = device.createBindingLayout({{
        {0, rhi::BindingType::SampledTexture, 1, fragment},
        {1, rhi::BindingType::Sampler, 1, fragment}}});
    replacement.rebind(device, views);
    *this = std::move(replacement);
}
void OutputBindings::rebind(const rhi::Device& device, const std::vector<rhi::TextureViewRef>& views)
{
    std::vector<rhi::BindingSetRef> replacement;
    replacement.reserve(views.size());
    for (const auto& view : views)
        replacement.push_back(device.createBindingSet({layout_, {
            {0, 0, rhi::TextureBinding{view, {rhi::AccessUsage::ShaderRead,
                rhi::shaderStageMask(rhi::ShaderStage::Fragment)}}},
            {1, 0, sampler_}}}));
    sets_.swap(replacement);
}
void OutputBindings::reset() noexcept
{
    sets_.clear(); layout_.reset(); sampler_.reset();
}
}
