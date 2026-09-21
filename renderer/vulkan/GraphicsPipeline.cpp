#include "vulkan/GraphicsPipeline.hpp"

#include "vulkan/Device.hpp"
#include "vulkan/GpuPreparedAssets.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{

namespace
{

using Info = GraphicsPipeline::CreateInfo;

bool dynamic(const Info& info, VkDynamicState state)
{
    return std::find(info.dynamicStates.begin(), info.dynamicStates.end(), state) !=
        info.dynamicStates.end();
}

bool sameBlend(const VkPipelineColorBlendAttachmentState& a,
               const VkPipelineColorBlendAttachmentState& b)
{
    return a.blendEnable == b.blendEnable &&
        a.srcColorBlendFactor == b.srcColorBlendFactor &&
        a.dstColorBlendFactor == b.dstColorBlendFactor && a.colorBlendOp == b.colorBlendOp &&
        a.srcAlphaBlendFactor == b.srcAlphaBlendFactor &&
        a.dstAlphaBlendFactor == b.dstAlphaBlendFactor && a.alphaBlendOp == b.alphaBlendOp &&
        a.colorWriteMask == b.colorWriteMask;
}

void validateState(const Device& device, const Info& info)
{
    // This wrapper exposes core VS/FS pipelines, not tessellation, geometry,
    // mesh shaders, or extension dynamic states. Never silently drop a request.
    const auto topology = info.inputAssembly.topology;
    if (topology < VK_PRIMITIVE_TOPOLOGY_POINT_LIST ||
        topology > VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
        throw std::invalid_argument("pipeline topology requires an unsupported shader stage");
    if (info.inputAssembly.primitiveRestartEnable &&
        topology != VK_PRIMITIVE_TOPOLOGY_LINE_STRIP &&
        topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
        topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
        throw std::invalid_argument("primitive restart requires a strip or fan topology");

    std::unordered_set<VkDynamicState> dynamicStates;
    for (const auto state : info.dynamicStates)
    {
        if (state < VK_DYNAMIC_STATE_VIEWPORT || state > VK_DYNAMIC_STATE_STENCIL_REFERENCE ||
            !dynamicStates.insert(state).second)
            throw std::invalid_argument("unsupported or duplicate pipeline dynamic state");
    }

    const auto& features = device.enabledFeatures();
    const auto& raster = info.rasterization;
    const auto& depth = info.depthStencil;
    const auto& samples = info.multisample;
    const auto requireFeature = [](bool requested, VkBool32 enabled, const char* name)
    {
        if (requested && !enabled)
            throw std::invalid_argument(std::string("pipeline requires enabled device feature: ") + name);
    };
    requireFeature(raster.depthClampEnable, features.depthClamp, "depthClamp");
    requireFeature(raster.polygonMode != VK_POLYGON_MODE_FILL, features.fillModeNonSolid,
                   "fillModeNonSolid");
    requireFeature(!dynamic(info, VK_DYNAMIC_STATE_LINE_WIDTH) && raster.lineWidth != 1.0f,
                   features.wideLines, "wideLines");
    requireFeature(!dynamic(info, VK_DYNAMIC_STATE_DEPTH_BIAS) && raster.depthBiasClamp != 0.0f,
                   features.depthBiasClamp, "depthBiasClamp");
    requireFeature(depth.depthBoundsTestEnable, features.depthBounds, "depthBounds");
    requireFeature(samples.sampleShadingEnable, features.sampleRateShading, "sampleRateShading");
    requireFeature(samples.alphaToOneEnable, features.alphaToOne, "alphaToOne");
    requireFeature(info.colorBlend.logicOpEnable, features.logicOp, "logicOp");
    requireFeature(info.viewport.viewports.size() > 1, features.multiViewport, "multiViewport");

    const auto sampleCount = static_cast<uint32_t>(samples.samples);
    if (sampleCount == 0 || sampleCount > 64 || (sampleCount & (sampleCount - 1)) != 0 ||
        (!samples.sampleMask.empty() && samples.sampleMask.size() != (sampleCount + 31) / 32))
        throw std::invalid_argument("invalid pipeline sample count or sample mask length");
    if (!std::isfinite(samples.minSampleShading) || samples.minSampleShading < 0.0f ||
        samples.minSampleShading > 1.0f)
        throw std::invalid_argument("minSampleShading must be in [0, 1]");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.physical(), &properties);
    if (info.viewport.viewports.empty() ||
        info.viewport.viewports.size() != info.viewport.scissors.size() ||
        info.viewport.viewports.size() > properties.limits.maxViewports)
        throw std::invalid_argument("pipeline requires matching, nonempty viewport/scissor arrays");
    if (info.colorBlend.attachments.size() > properties.limits.maxColorAttachments)
        throw std::invalid_argument("pipeline exceeds maxColorAttachments");

    for (const auto& attachment : info.colorBlend.attachments)
    {
        requireFeature(!sameBlend(attachment, info.colorBlend.attachments.front()),
                       features.independentBlend, "independentBlend");
        const auto dualSource = [](VkBlendFactor factor)
        {
            return factor >= VK_BLEND_FACTOR_SRC1_COLOR &&
                factor <= VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        };
        requireFeature(attachment.blendEnable &&
            (dualSource(attachment.srcColorBlendFactor) || dualSource(attachment.dstColorBlendFactor) ||
             dualSource(attachment.srcAlphaBlendFactor) || dualSource(attachment.dstAlphaBlendFactor)),
            features.dualSrcBlend, "dualSrcBlend");
        if (attachment.colorBlendOp < VK_BLEND_OP_ADD || attachment.colorBlendOp > VK_BLEND_OP_MAX ||
            attachment.alphaBlendOp < VK_BLEND_OP_ADD || attachment.alphaBlendOp > VK_BLEND_OP_MAX)
            throw std::invalid_argument("only core color blend operations are supported");
    }

    std::unordered_set<VkShaderStageFlagBits> stages;
    for (const auto& specialization : info.specializations)
    {
        if ((specialization.stage != VK_SHADER_STAGE_VERTEX_BIT &&
             specialization.stage != VK_SHADER_STAGE_FRAGMENT_BIT) ||
            !stages.insert(specialization.stage).second)
            throw std::invalid_argument("unsupported or duplicate specialization stage");
        std::unordered_set<uint32_t> constants;
        for (const auto& entry : specialization.entries)
        {
            if (entry.offset >= specialization.data.size() || entry.size == 0 ||
                entry.size > specialization.data.size() - entry.offset ||
                !constants.insert(entry.constantID).second)
                throw std::invalid_argument("invalid specialization constant range or duplicate ID");
        }
    }
}

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
      pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE)),
      renderPass_(std::move(other.renderPass_)),
      descriptorSetLayouts_(std::move(other.descriptorSetLayouts_))
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
        renderPass_ = std::move(other.renderPass_);
        descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
    }
    return *this;
}

void GraphicsPipeline::validate(
    const Device& device,
    const CreateInfo& createInfo)
{
    if (!device || !createInfo.renderPass || createInfo.renderPass->device() != device.get() ||
        (createInfo.program && createInfo.program->device() != device.get()) ||
        (!createInfo.program &&
         (createInfo.vertexShaderSpirv.empty() || createInfo.vertexEntryPoint.empty() ||
          (!createInfo.fragmentShaderSpirv.empty() && createInfo.fragmentEntryPoint.empty()))))
    {
        throw std::invalid_argument("graphics pipeline create info is incomplete");
    }
    if (createInfo.subpass >= createInfo.renderPass->subpassCount())
        throw std::invalid_argument("pipeline subpass index is out of range");
    if (!createInfo.rasterization.rasterizerDiscardEnable &&
        createInfo.colorBlend.attachments.size() != createInfo.renderPass->colorAttachmentCount(createInfo.subpass))
        throw std::invalid_argument("pipeline color blend slots do not match the subpass");
    for (const auto& layout : createInfo.descriptorSetLayouts)
        if (!layout || layout->device() != device.get())
            throw std::invalid_argument("pipeline descriptor layout is missing or belongs to another device");
    validateState(device, createInfo);
    VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT;
    if (createInfo.program || !createInfo.fragmentShaderSpirv.empty()) stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    for (const auto& specialization : createInfo.specializations)
        if (!(stages & specialization.stage))
            throw std::invalid_argument("specialization targets an absent shader stage");
}

void GraphicsPipeline::create(const Device& device, const CreateInfo& createInfo)
{
    validate(device, createInfo);
    // Allocate ownership storage before creating Vulkan objects for strong exception safety.
    auto layoutOwners = createInfo.descriptorSetLayouts;

    std::unique_ptr<ShaderModule> vertexModule;
    std::unique_ptr<ShaderModule> fragmentModule;
    auto shaderStages = createInfo.program ? createInfo.program->stages()
        : std::vector<VkPipelineShaderStageCreateInfo>(
            createInfo.fragmentShaderSpirv.empty() ? 1 : 2);
    if (!createInfo.program)
    {
        vertexModule = std::make_unique<ShaderModule>(device.get(), createInfo.vertexShaderSpirv);
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertexModule->get();
        shaderStages[0].pName = createInfo.vertexEntryPoint.c_str();
        if (!createInfo.fragmentShaderSpirv.empty())
        {
            fragmentModule = std::make_unique<ShaderModule>(device.get(), createInfo.fragmentShaderSpirv);
            shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStages[1].module = fragmentModule->get();
            shaderStages[1].pName = createInfo.fragmentEntryPoint.c_str();
        }
    }

    VkShaderStageFlags seenStages = 0;
    for (const auto& stage : shaderStages)
    {
        if ((stage.stage != VK_SHADER_STAGE_VERTEX_BIT && stage.stage != VK_SHADER_STAGE_FRAGMENT_BIT) ||
            (seenStages & stage.stage) != 0)
            throw std::invalid_argument("graphics pipeline supports one vertex and optional fragment stage");
        seenStages |= stage.stage;
    }
    if ((seenStages & VK_SHADER_STAGE_VERTEX_BIT) == 0)
        throw std::invalid_argument("graphics pipeline requires a vertex shader");
    // Sized once so pointers stored in shaderStages remain stable until Vulkan returns.
    std::vector<VkSpecializationInfo> specializations(createInfo.specializations.size());
    for (size_t i = 0; i < createInfo.specializations.size(); ++i)
    {
        const auto& source = createInfo.specializations[i];
        auto stage = std::find_if(shaderStages.begin(), shaderStages.end(),
            [&](const auto& candidate) { return candidate.stage == source.stage; });
        if (stage == shaderStages.end())
            throw std::invalid_argument("specialization targets an absent shader stage");
        specializations[i] = {static_cast<uint32_t>(source.entries.size()), source.entries.data(),
                              source.data.size(), source.data.data()};
        stage->pSpecializationInfo = &specializations[i];
    }

    VkPipelineLayout newLayout = VK_NULL_HANDLE;
    VkPipeline newPipeline = VK_NULL_HANDLE;

    try
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        std::vector<VkDescriptorSetLayout> layoutHandles;
        for (const auto& layout : layoutOwners) layoutHandles.push_back(layout->get());
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount =
            static_cast<uint32_t>(createInfo.descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = layoutHandles.data();
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
        inputAssembly.topology = createInfo.inputAssembly.topology;
        inputAssembly.primitiveRestartEnable = createInfo.inputAssembly.primitiveRestartEnable;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = static_cast<uint32_t>(createInfo.viewport.viewports.size());
        viewportState.pViewports = dynamic(createInfo, VK_DYNAMIC_STATE_VIEWPORT)
            ? nullptr : createInfo.viewport.viewports.data();
        viewportState.scissorCount = static_cast<uint32_t>(createInfo.viewport.scissors.size());
        viewportState.pScissors = dynamic(createInfo, VK_DYNAMIC_STATE_SCISSOR)
            ? nullptr : createInfo.viewport.scissors.data();
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =
            static_cast<uint32_t>(createInfo.dynamicStates.size());
        dynamicState.pDynamicStates = createInfo.dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        const auto& raster = createInfo.rasterization;
        rasterizer.depthClampEnable = raster.depthClampEnable;
        rasterizer.rasterizerDiscardEnable = raster.rasterizerDiscardEnable;
        rasterizer.polygonMode = raster.polygonMode;
        rasterizer.lineWidth = raster.lineWidth;
        rasterizer.cullMode = raster.cullMode;
        rasterizer.frontFace = raster.frontFace;
        rasterizer.depthBiasEnable = raster.depthBiasEnable;
        rasterizer.depthBiasConstantFactor = raster.depthBiasConstantFactor;
        rasterizer.depthBiasClamp = raster.depthBiasClamp;
        rasterizer.depthBiasSlopeFactor = raster.depthBiasSlopeFactor;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = createInfo.multisample.samples;
        multisampling.sampleShadingEnable = createInfo.multisample.sampleShadingEnable;
        multisampling.minSampleShading = createInfo.multisample.minSampleShading;
        multisampling.pSampleMask = createInfo.multisample.sampleMask.empty()
            ? nullptr : createInfo.multisample.sampleMask.data();
        multisampling.alphaToCoverageEnable = createInfo.multisample.alphaToCoverageEnable;
        multisampling.alphaToOneEnable = createInfo.multisample.alphaToOneEnable;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = createInfo.colorBlend.logicOpEnable;
        colorBlending.logicOp = createInfo.colorBlend.logicOp;
        colorBlending.attachmentCount = static_cast<uint32_t>(createInfo.colorBlend.attachments.size());
        colorBlending.pAttachments = createInfo.colorBlend.attachments.data();
        std::copy(createInfo.colorBlend.constants.begin(), createInfo.colorBlend.constants.end(),
                  colorBlending.blendConstants);

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        const auto& depth = createInfo.depthStencil;
        depthStencil.depthTestEnable = depth.depthTestEnable;
        depthStencil.depthWriteEnable = depth.depthWriteEnable;
        depthStencil.depthCompareOp = depth.depthCompareOp;
        depthStencil.depthBoundsTestEnable = depth.depthBoundsTestEnable;
        depthStencil.stencilTestEnable = depth.stencilTestEnable;
        depthStencil.front = depth.front;
        depthStencil.back = depth.back;
        depthStencil.minDepthBounds = depth.minDepthBounds;
        depthStencil.maxDepthBounds = depth.maxDepthBounds;

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
        pipelineInfo.renderPass = createInfo.renderPass->get();
        pipelineInfo.subpass = createInfo.subpass;
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
    renderPass_ = createInfo.renderPass;
    descriptorSetLayouts_ = std::move(layoutOwners);
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
    renderPass_.reset();
    descriptorSetLayouts_.clear();
}

} // namespace rubia::rhi::vulkan
