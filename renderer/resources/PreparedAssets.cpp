#include "resources/PreparedAssets.hpp"
#include <algorithm>
#include <stdexcept>
namespace rubia::render
{
rhi::ShaderStage shaderStage(asset::ShaderStage stage)
{
    switch (stage)
    {
    case asset::ShaderStage::Vertex:
        return rhi::ShaderStage::Vertex;
    case asset::ShaderStage::Fragment:
        return rhi::ShaderStage::Fragment;
    case asset::ShaderStage::Compute:
        return rhi::ShaderStage::Compute;
    }
    throw std::invalid_argument("unsupported shader stage");
}
rhi::ShaderStageMask shaderStages(asset::ShaderStageMask mask)
{
    rhi::ShaderStageMask result = 0;
    for (auto stage :
         {asset::ShaderStage::Vertex, asset::ShaderStage::Fragment, asset::ShaderStage::Compute})
        if (mask & asset::shaderStageMask(stage))
            result |= rhi::shaderStageMask(shaderStage(stage));
    return result;
}
rhi::BindingLayoutDesc bindingLayout(const std::vector<asset::ProgramResourceBinding> &source)
{
    rhi::BindingLayoutDesc result;
    for (const auto &item : source)
    {
        rhi::BindingType type;
        switch (item.type)
        {
        case asset::ShaderResourceType::UniformBuffer:
            type = rhi::BindingType::UniformBuffer;
            break;
        case asset::ShaderResourceType::StorageBuffer:
            type = rhi::BindingType::StorageBuffer;
            break;
        case asset::ShaderResourceType::SampledImage:
            type = rhi::BindingType::SampledTexture;
            break;
        case asset::ShaderResourceType::Sampler:
            type = rhi::BindingType::Sampler;
            break;
        case asset::ShaderResourceType::CombinedImageSampler:
            type = rhi::BindingType::CombinedTextureSampler;
            break;
        default:
            throw std::invalid_argument("unsupported binding type");
        }
        result.bindings.push_back({item.binding, type, item.arrayCount, shaderStages(item.stages)});
    }
    return result;
}
PreparedShader::PreparedShader(const rhi::Device &device,
                               std::shared_ptr<const asset::ShaderAsset> source)
    : source_(std::move(source))
{
    if (!source_ || !*source_)
        throw std::invalid_argument("invalid shader source");
    shader_ = device.createShader(
        {shaderStage(source_->stage()), source_->entryPoint(), source_->spirv()});
}
PreparedShaderProgram::PreparedShaderProgram(
    const rhi::Device &device, std::shared_ptr<const asset::ShaderProgramAsset> source,
    std::vector<std::shared_ptr<const PreparedShader>> shaders)
    : device_(device.id()), source_(std::move(source)), shaders_(std::move(shaders))
{
    if (!source_ || shaders_.size() != 2 || !shaders_[0] || !shaders_[1] ||
        shaders_[0]->device() != device_ || shaders_[1]->device() != device_ ||
        shaders_[0]->source().stage() != asset::ShaderStage::Vertex ||
        shaders_[1]->source().stage() != asset::ShaderStage::Fragment)
        throw std::invalid_argument("program preparation requires matching VS/PS pair");
    uint32_t count = 0;
    for (const auto &item : source_->interface().bindings)
    {
        if (item.set == UINT32_MAX)
            throw std::invalid_argument("binding group overflows");
        count = std::max(count, item.set + 1);
    }
    // Prevent unbounded allocation before the backend validates device limits.
    if (count > device.limits().maxBindingGroups)
        throw std::invalid_argument("program exceeds supported binding group count");
    for (uint32_t group = 0; group < count; ++group)
    {
        std::vector<asset::ProgramResourceBinding> selected;
        for (const auto &item : source_->interface().bindings)
            if (item.set == group)
                selected.push_back(item);
        sets_.push_back(device.createBindingLayout(bindingLayout(selected)));
    }
    for (const auto &range : source_->interface().pushConstants)
    {
        if (!range.size || range.offset > device.limits().maxConstantBytes ||
            range.size > device.limits().maxConstantBytes - range.offset)
            throw std::invalid_argument("program constants exceed device limits");
        constants_.push_back({shaderStages(range.stages), range.offset, range.size});
    }
}
PreparedMaterialTemplate::PreparedMaterialTemplate(
    const rhi::Device &device, std::shared_ptr<const asset::MaterialTemplateAsset> source,
    std::shared_ptr<const PreparedShaderProgram> program)
    : source_(std::move(source)), program_(std::move(program))
{
    if (!source_ || !*source_ || !program_ || program_->device() != device.id() ||
        source_->programInterfaceSignature() != program_->source().interfaceSignature())
        throw std::invalid_argument("material template and program interfaces differ");
    layout_ = device.createBindingLayout(bindingLayout(source_->bindings()));
}
} // namespace rubia::render
