#include "vulkan/GpuPreparedAssets.hpp"
#include "vulkan/Device.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace rubia::rhi::vulkan
{
namespace
{
VkShaderStageFlags stageFlags(asset::ShaderStageMask mask)
{
    VkShaderStageFlags result = 0;
    if (mask & asset::shaderStageMask(asset::ShaderStage::Vertex))
    {
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (mask & asset::shaderStageMask(asset::ShaderStage::Fragment))
    {
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (mask & asset::shaderStageMask(asset::ShaderStage::Compute))
    {
        result |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return result;
}
VkDescriptorSetLayoutBinding binding(const asset::ProgramResourceBinding& source)
{
    VkDescriptorSetLayoutBinding result{};
    result.binding = source.binding;
    result.descriptorCount = source.arrayCount;
    result.stageFlags = stageFlags(source.stages);
    switch (source.type)
    {
    case asset::ShaderResourceType::UniformBuffer:
        result.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        break;
    case asset::ShaderResourceType::StorageBuffer:
        result.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        break;
    case asset::ShaderResourceType::SampledImage:
        result.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        break;
    case asset::ShaderResourceType::Sampler:
        result.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        break;
    case asset::ShaderResourceType::CombinedImageSampler:
        result.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        break;
    default:
        throw std::invalid_argument("unsupported program descriptor type");
    }
    if (!result.descriptorCount || !result.stageFlags)
    {
        throw std::invalid_argument("incomplete program binding");
    }
    return result;
}
} // namespace
GpuShader::GpuShader(const Device& device, std::shared_ptr<const asset::ShaderAsset> source)
    : device_(device.get()), source_(std::move(source))
{
    if (!device || !source_ || !*source_)
    {
        throw std::invalid_argument("invalid shader source");
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = source_->spirv().size() * sizeof(uint32_t);
    info.pCode = source_->spirv().data();
    if (vkCreateShaderModule(device_, &info, nullptr, &module_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create prepared shader module");
    }
}
GpuShader::~GpuShader()
{
    if (module_)
    {
        vkDestroyShaderModule(device_, module_, nullptr);
    }
}
VkPipelineShaderStageCreateInfo GpuShader::stageInfo() const
{
    VkPipelineShaderStageCreateInfo result{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    result.stage =
        static_cast<VkShaderStageFlagBits>(stageFlags(asset::shaderStageMask(source_->stage())));
    result.module = module_;
    result.pName = source_->entryPoint().c_str();
    return result;
}
GpuShaderProgram::GpuShaderProgram(const Device& device,
                                   std::shared_ptr<const asset::ShaderProgramAsset> source,
                                   std::vector<std::shared_ptr<const GpuShader>> shaders)
    : device_(device.get()), source_(std::move(source)), shaders_(std::move(shaders))
{
    if (!device || !source_ || shaders_.size() != 2 || !shaders_[0] || !shaders_[1] ||
        shaders_[0]->source().stage() != asset::ShaderStage::Vertex ||
        shaders_[1]->source().stage() != asset::ShaderStage::Fragment)
    {
        throw std::invalid_argument("program preparation requires a VS/PS pair");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.physical(), &properties);
    uint32_t setCount = 0;
    for (const auto& item : source_->interface().bindings)
    {
        if (item.set >= properties.limits.maxBoundDescriptorSets)
        {
            throw std::invalid_argument("program descriptor set exceeds device limit");
        }
        setCount = std::max(setCount, item.set + 1);
    }
    sets_.resize(setCount);
    for (uint32_t set = 0; set < setCount; ++set)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& item : source_->interface().bindings)
        {
            if (item.set == set)
            {
                bindings.push_back(binding(item));
            }
        }
        sets_[set].create(device_, bindings);
    }
    std::vector<VkPushConstantRange> ranges;
    for (const auto& range : source_->interface().pushConstants)
    {
        if (!range.size || range.offset > properties.limits.maxPushConstantsSize ||
            range.size > properties.limits.maxPushConstantsSize - range.offset)
        {
            throw std::invalid_argument("program push constants exceed device limit");
        }
        ranges.push_back({stageFlags(range.stages), range.offset, range.size});
    }
    const auto layouts = setLayouts();
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    info.pushConstantRangeCount = static_cast<uint32_t>(ranges.size());
    info.pPushConstantRanges = ranges.data();
    if (vkCreatePipelineLayout(device_, &info, nullptr, &layout_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create prepared program layout");
    }
}
GpuShaderProgram::~GpuShaderProgram()
{
    if (layout_)
    {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
}
std::vector<VkPipelineShaderStageCreateInfo> GpuShaderProgram::stages() const
{
    std::vector<VkPipelineShaderStageCreateInfo> result;
    for (const auto& shader : shaders_)
    {
        result.push_back(shader->stageInfo());
    }
    return result;
}
std::vector<VkDescriptorSetLayout> GpuShaderProgram::setLayouts() const
{
    std::vector<VkDescriptorSetLayout> result;
    for (const auto& set : sets_)
    {
        result.push_back(set.get());
    }
    return result;
}
GpuMaterialTemplate::GpuMaterialTemplate(const Device& device,
                                         std::shared_ptr<const asset::MaterialTemplateAsset> source,
                                         std::shared_ptr<const GpuShaderProgram> program)
    : source_(std::move(source)), program_(std::move(program))
{
    if (!source_ || !*source_ || !program_ ||
        source_->programInterfaceSignature() != program_->source().interfaceSignature())
    {
        throw std::invalid_argument("material template and prepared program interfaces differ");
    }
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& item : source_->bindings())
    {
        bindings.push_back(binding(item));
    }
    layout_.create(device.get(), bindings);
}
DescriptorPool GpuMaterialTemplate::createDescriptorPool(const Device& device) const
{
    std::map<VkDescriptorType, uint32_t> counts;
    for (const auto& source : source_->bindings())
    {
        const auto converted = binding(source);
        counts[converted.descriptorType] += converted.descriptorCount;
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (const auto& count : counts)
    {
        sizes.push_back({count.first, count.second});
    }
    return DescriptorPool(device.get(), sizes, 1);
}
} // namespace rubia::rhi::vulkan
