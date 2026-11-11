#include "resources/MaterialBinding.hpp"
#include "asset/MaterialTemplateAsset.hpp"
#include <stdexcept>
namespace rubia::render
{
namespace
{
rhi::BindingSetRef createBindings(const rhi::Device &device,
                                  const asset::MaterialTemplateAsset &materialTemplate,
                                  const std::vector<const TextureResource *> &textures,
                                  rhi::BindingLayoutRef layout, const rhi::BufferRef &parameters)
{
    if (!layout || !parameters || parameters->deviceId() != device.id() ||
        !(parameters->description().usage & rhi::flag(rhi::BufferUsage::Uniform)))
        throw std::invalid_argument("invalid material binding inputs");
    rhi::BindingSetDesc bindings;
    bindings.layout = std::move(layout);
    bindings.writes.push_back({materialTemplate.parameterBlock().descriptor.binding, 0,
                               rhi::BufferBinding{parameters, 0, parameters->description().size}});
    for (const auto &slot : materialTemplate.textureSlots())
    {
        if (slot.slot >= textures.size() || !textures[slot.slot] || !*textures[slot.slot])
            throw std::invalid_argument("invalid material texture slot");
        const auto &texture = *textures[slot.slot];
        bindings.writes.push_back(
            {slot.imageBinding.binding, 0,
             rhi::TextureBinding{texture.view(), {rhi::AccessUsage::ShaderRead, 0}}});
        bindings.writes.push_back({slot.samplerBinding.binding, 0, texture.sampler()});
    }
    return device.createBindingSet(bindings);
}
} // namespace
void MaterialBinding::create(const rhi::Device &device, const asset::MaterialAsset &source,
                             const asset::MaterialTemplateAsset &layout,
                             const std::vector<const TextureResource *> &textures,
                             rhi::BindingLayoutRef bindingLayout, rhi::BufferRef parameters)
{
    if (!source || source.parameterData().empty() || !parameters ||
        parameters->description().size != source.parameterData().size())
        throw std::invalid_argument("invalid material parameter allocation");
    auto bindings = createBindings(device, layout, textures, std::move(bindingLayout), parameters);
    parameterBuffer_ = std::move(parameters);
    materialTemplate_ = source.materialTemplate();
    renderState_ = source.renderState();
    bindings_ = std::move(bindings);
}
MaterialBinding MaterialBinding::withTextures(const rhi::Device &device,
                                              const asset::MaterialTemplateAsset &layout,
                                              const std::vector<const TextureResource *> &textures,
                                              rhi::BindingLayoutRef bindingLayout) const
{
    if (!*this)
        throw std::invalid_argument("cannot rebind an empty material");
    return fromParameters(device, layout, textures, std::move(bindingLayout), parameterSnapshot());
}
MaterialBinding MaterialBinding::fromParameters(
    const rhi::Device &device, const asset::MaterialTemplateAsset &layout,
    const std::vector<const TextureResource *> &textures, rhi::BindingLayoutRef bindingLayout,
    const ParameterSnapshot &source)
{
    if (!source.buffer || !source.materialTemplate)
        throw std::invalid_argument("invalid material snapshot");
    MaterialBinding result;
    result.bindings_ =
        createBindings(device, layout, textures, std::move(bindingLayout), source.buffer);
    result.parameterBuffer_ = source.buffer;
    result.materialTemplate_ = source.materialTemplate;
    result.renderState_ = source.renderState;
    return result;
}
} // namespace rubia::render
