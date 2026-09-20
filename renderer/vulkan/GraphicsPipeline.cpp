#include "vulkan/GraphicsPipeline.hpp"

#include "vulkan/Device.hpp"
#include "vulkan/GpuPreparedAssets.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{

namespace
{

class ShaderModule final
{
public:
    ShaderModule(VkDevice device, const std::vector<uint32_t>& code)
        : device_(device)
    {
        constexpr uint32_t kSpirvMagic = 0x07230203u;
        if (code.empty() || code.front() != kSpirvMagic)
        {
            throw std::invalid_argument("SPIR-V shader code is invalid");
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        if (vkCreateShaderModule(device_, &createInfo, nullptr, &module_) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }
    }

    ~ShaderModule()
    {
        if (device_ != VK_NULL_HANDLE && module_ != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    [[nodiscard]] VkShaderModule get() const noexcept { return module_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

} // namespace

GraphicsPipeline::GraphicsPipeline(
    const Device& device,
    const CreateInfo& createInfo)
{
    create(device, createInfo);
}

GraphicsPipeline::~GraphicsPipeline()
{
    reset();
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      layout_(std::exchange(other.layout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE))
{
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept
{
    if (this != &other)
    {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
    }
    return *this;
}

void GraphicsPipeline::create(
    const Device& device,
    const CreateInfo& createInfo)
{
    if (!device || createInfo.renderPass == VK_NULL_HANDLE ||
        (!createInfo.program &&
         (createInfo.vertexShaderSpirv.empty() || createInfo.vertexEntryPoint.empty() ||
          createInfo.fragmentShaderSpirv.empty() || createInfo.fragmentEntryPoint.empty())) ||
        createInfo.sampleCount == 0)
    {
        throw std::invalid_argument("graphics pipeline create info is incomplete");
    }

    std::unique_ptr<ShaderModule> vertexModule;
    std::unique_ptr<ShaderModule> fragmentModule;
    auto shaderStages = createInfo.program ? createInfo.program->stages()
                                           : std::vector<VkPipelineShaderStageCreateInfo>(2);
    if (!createInfo.program)
    {
        vertexModule = std::make_unique<ShaderModule>(device.get(), createInfo.vertexShaderSpirv);
        fragmentModule =
            std::make_unique<ShaderModule>(device.get(), createInfo.fragmentShaderSpirv);
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertexModule->get();
        shaderStages[0].pName = createInfo.vertexEntryPoint.c_str();
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentModule->get();
        shaderStages[1].pName = createInfo.fragmentEntryPoint.c_str();
    }

    VkPipelineLayout newLayout = VK_NULL_HANDLE;
    VkPipeline newPipeline = VK_NULL_HANDLE;

    try
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount =
            static_cast<uint32_t>(createInfo.descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = createInfo.descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount =
            static_cast<uint32_t>(createInfo.pushConstantRanges.size());
        layoutInfo.pPushConstantRanges =
            createInfo.pushConstantRanges.data();

        if (vkCreatePipelineLayout(
                device.get(),
                &layoutInfo,
                nullptr,
                &newLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount =
            static_cast<uint32_t>(createInfo.vertexBindings.size());
        vertexInput.pVertexBindingDescriptions =
            createInfo.vertexBindings.data();
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(createInfo.vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions =
            createInfo.vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = createInfo.primitiveTopology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =
            static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = createInfo.cullMode;
        rasterizer.frontFace = createInfo.frontFace;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = createInfo.sampleCount;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = createInfo.depthTestEnable;
        depthStencil.depthWriteEnable = createInfo.depthWriteEnable;
        depthStencil.depthCompareOp = createInfo.depthCompareOp;
        depthStencil.maxDepthBounds = 1.0f;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = newLayout;
        pipelineInfo.renderPass = createInfo.renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(
                device.get(),
                VK_NULL_HANDLE,
                1,
                &pipelineInfo,
                nullptr,
                &newPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create graphics pipeline!");
        }
    }
    catch (...)
    {
        if (newPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.get(), newPipeline, nullptr);
        }
        if (newLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device.get(), newLayout, nullptr);
        }
        throw;
    }

    reset();
    device_ = device.get();
    layout_ = newLayout;
    pipeline_ = newPipeline;
}

void GraphicsPipeline::reset() noexcept
{
    if (device_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }

    device_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
}

} // namespace rubia::rhi::vulkan
