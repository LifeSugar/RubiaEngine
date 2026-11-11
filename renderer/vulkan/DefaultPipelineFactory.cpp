#include "vulkan/DefaultPipelineFactory.hpp"

#include "vulkan/PipelineDescriptionAdapter.hpp"
#include "VulkanResourceDevice.hpp"

#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

GraphicsPipeline::CreateInfo makeDefaultScenePipeline(
    std::shared_ptr<const GpuShaderProgram> program,
    rhi::BindingLayoutRef materialDescriptorSetLayout)
{
    if (!program)
    {
        throw std::invalid_argument("pipeline requires a prepared shader program");
    }
    GraphicsPipeline::CreateInfo createInfo{};
    for (const auto& shader : program->shaders()) createInfo.shaders.push_back(shader->resource());
    createInfo.descriptorSetLayouts = {nativeBindingLayout(materialDescriptorSetLayout)};

    applyPipelinePolicy(createInfo, render::defaultScenePipelinePolicy());

    return createInfo;
}

GraphicsPipeline::CreateInfo makeDefaultPresentPipeline(
    std::shared_ptr<const GpuShaderProgram> program)
{
    if (!program)
    {
        throw std::invalid_argument("pipeline requires a prepared shader program");
    }
    GraphicsPipeline::CreateInfo createInfo{};
    for (const auto& shader : program->shaders()) createInfo.shaders.push_back(shader->resource());
    applyPipelinePolicy(createInfo, render::defaultOutputPipelinePolicy());
    return createInfo;
}

} // namespace rubia::rhi::vulkan
