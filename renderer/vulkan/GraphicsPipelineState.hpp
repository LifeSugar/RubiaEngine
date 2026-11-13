#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <vector>

namespace rubia::rhi::vulkan
{

// Value-owned state: Vulkan pointers are assembled only during create().
struct PipelineInputAssemblyState
{
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitiveRestartEnable = VK_FALSE;
};

struct PipelineRasterizationState
{
    VkBool32 depthClampEnable = VK_FALSE;
    VkBool32 rasterizerDiscardEnable = VK_FALSE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkBool32 depthBiasEnable = VK_FALSE;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};

struct PipelineDepthStencilState
{
    VkBool32 depthTestEnable = VK_TRUE;
    VkBool32 depthWriteEnable = VK_TRUE;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    VkBool32 depthBoundsTestEnable = VK_FALSE;
    VkBool32 stencilTestEnable = VK_FALSE;
    VkStencilOpState front{VK_STENCIL_OP_KEEP,
                           VK_STENCIL_OP_KEEP,
                           VK_STENCIL_OP_KEEP,
                           VK_COMPARE_OP_ALWAYS,
                           ~0u,
                           ~0u,
                           0};
    VkStencilOpState back = front;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
};

struct PipelineMultisampleState
{
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkBool32 sampleShadingEnable = VK_FALSE;
    float minSampleShading = 0.0f;
    /// Empty means all samples enabled; otherwise ceil(samples / 32) words.
    std::vector<VkSampleMask> sampleMask;
    VkBool32 alphaToCoverageEnable = VK_FALSE;
    VkBool32 alphaToOneEnable = VK_FALSE;
};

struct PipelineColorBlendState
{
    VkBool32 logicOpEnable = VK_FALSE;
    VkLogicOp logicOp = VK_LOGIC_OP_COPY;
    /// One entry per subpass color slot. Clear for a depth-only subpass.
    /// Different entries require the enabled independentBlend device feature.
    std::vector<VkPipelineColorBlendAttachmentState> attachments = {
        {VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE,
         VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
             VK_COLOR_COMPONENT_A_BIT}};
    std::array<float, 4> constants{};
};

struct PipelineViewportState
{
    /// Array sizes declare counts even when the corresponding values are dynamic.
    std::vector<VkViewport> viewports = {{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}};
    std::vector<VkRect2D> scissors = {{{0, 0}, {1, 1}}};
};

struct PipelineStageSpecialization
{
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<std::byte> data;
};

} // namespace rubia::rhi::vulkan
