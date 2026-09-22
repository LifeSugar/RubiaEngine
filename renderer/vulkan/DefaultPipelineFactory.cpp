#include "vulkan/DefaultPipelineFactory.hpp"

#include "asset/MeshAsset.hpp"
#include "render/RenderData.hpp"
#include "vulkan/GpuPreparedAssets.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

GraphicsPipeline::CreateInfo makeDefaultScenePipeline(
    std::shared_ptr<const GpuShaderProgram> program,
    std::shared_ptr<const DescriptorSetLayoutState> materialDescriptorSetLayout)
{
    if (!program)
    {
        throw std::invalid_argument("pipeline requires a prepared shader program");
    }
    GraphicsPipeline::CreateInfo createInfo{};
    createInfo.program = std::move(program);
    createInfo.descriptorSetLayouts = {materialDescriptorSetLayout};

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof(render::DrawPushConstants);
    createInfo.pushConstantRanges = {pushConstantRange};

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(asset::Vertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    createInfo.vertexBindings = {vertexBinding};

    std::array<VkVertexInputAttributeDescription, 5> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(asset::Vertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(asset::Vertex, color);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(asset::Vertex, normal);
    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[3].offset = offsetof(asset::Vertex, texCoord);
    attributes[4].binding = 0;
    attributes[4].location = 4;
    attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[4].offset = offsetof(asset::Vertex, tangent);
    createInfo.vertexAttributes.assign(attributes.begin(), attributes.end());

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
    createInfo.program = std::move(program);
    createInfo.rasterization.cullMode = VK_CULL_MODE_NONE;
    createInfo.depthStencil.depthTestEnable = VK_FALSE;
    createInfo.depthStencil.depthWriteEnable = VK_FALSE;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof(render::PresentPushConstants);
    createInfo.pushConstantRanges = {pushConstantRange};
    return createInfo;
}

} // namespace rubia::rhi::vulkan
