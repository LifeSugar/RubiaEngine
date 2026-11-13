#include "VulkanPipelineDevice.hpp"
#include <set>

namespace rubia::rhi::vulkan
{
namespace
{
struct PipelineOwner final : rhi::PipelineResource
{
    DeviceId device = 0;
    rhi::GraphicsPipelineDesc desc;
    std::shared_ptr<const RenderPassState> pass;
    GraphicsPipeline value;
    DeviceId deviceId() const noexcept override { return device; }
    const rhi::GraphicsPipelineDesc& description() const noexcept override { return desc; }
};
const PipelineOwner& checked(const rhi::PipelineRef& pipeline)
{
    const auto* owner = dynamic_cast<const PipelineOwner*>(pipeline.get());
    if (!owner) throw std::invalid_argument("pipeline does not belong to the Vulkan backend");
    return *owner;
}
bool depthFormat(Format format)
{
    return format == Format::D32Float || format == Format::D32FloatS8 || format == Format::D24UNormS8;
}
RenderPass compatiblePass(const vulkan::Device& device, const AttachmentSignature& signature)
{
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colors, resolves;
    const auto append = [&](Format format, uint32_t samples, VkImageLayout layout) {
        VkAttachmentDescription attachment{};
        attachment.format = toVulkan(format);
        attachment.samples = static_cast<VkSampleCountFlagBits>(samples);
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = layout;
        attachments.push_back(attachment);
        return static_cast<uint32_t>(attachments.size() - 1);
    };
    for (const auto& color : signature.colors)
        colors.push_back({append(color.format,signature.samples,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    VkAttachmentReference depth{VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (signature.depthStencilFormat != Format::Undefined)
        depth.attachment=append(signature.depthStencilFormat,signature.samples,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    bool hasResolve=false;
    for (const auto& color : signature.colors)
    {
        auto index=VK_ATTACHMENT_UNUSED;
        if(color.resolveFormat != Format::Undefined)
        { index=append(color.resolveFormat,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);hasResolve=true; }
        resolves.push_back({index,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    }
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=static_cast<uint32_t>(colors.size());
    subpass.pColorAttachments=colors.data();
    subpass.pResolveAttachments=hasResolve?resolves.data():nullptr;
    subpass.pDepthStencilAttachment=depth.attachment==VK_ATTACHMENT_UNUSED?nullptr:&depth;
    VkRenderPassCreateInfo info{};
    info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount=static_cast<uint32_t>(attachments.size());info.pAttachments=attachments.data();
    info.subpassCount=1;info.pSubpasses=&subpass;
    return RenderPass(device.get(),info);
}
}
VkPipeline nativePipeline(const rhi::PipelineRef& pipeline) { return checked(pipeline).value.get(); }
VkPipelineLayout nativePipelineLayout(const rhi::PipelineRef& pipeline) { return checked(pipeline).value.layout(); }
const RenderPassState& nativePipelinePass(const rhi::PipelineRef& pipeline) { return *checked(pipeline).pass; }

void VulkanPipelineDevice::validateGraphicsPipeline(const rhi::GraphicsPipelineDesc& description) const
{
    const auto& signature=description.attachments;
    if (description.target)
    {
        if (description.target->deviceId()!=deviceId())
            throw std::invalid_argument("pipeline target belongs to another device session");
        const auto native = nativePipelineTarget(description.target);
        if (native->device()!=resources_.native().get())
            throw std::invalid_argument("pipeline target belongs to another native device");
        const auto& expected=description.target->description();
        if(expected.samples!=signature.samples || expected.depthStencilFormat!=signature.depthStencilFormat || expected.colors.size()!=signature.colors.size())
            throw std::invalid_argument("pipeline target signature differs from pipeline attachments");
        for(size_t i=0;i<signature.colors.size();++i)
            if(expected.colors[i].format!=signature.colors[i].format || expected.colors[i].resolveFormat!=signature.colors[i].resolveFormat)
                throw std::invalid_argument("pipeline target color signature differs from pipeline attachments");
    }
    if (description.shaders.empty() || signature.samples != description.multisample.samples ||
        !signature.samples || signature.samples>64 || (signature.samples&(signature.samples-1)))
        throw std::invalid_argument("pipeline requires shaders and matching valid attachment sample count");
    if(!description.rasterization.discard && description.colorBlend.attachments.size()!=signature.colors.size())
        throw std::invalid_argument("pipeline color blend slots do not match attachments");
    for(const auto& shader:description.shaders)
        if(!shader || shader->deviceId()!=deviceId()) throw std::invalid_argument("pipeline shader belongs to another device session");
    for(const auto& layout:description.bindingLayouts)
        if(!layout || layout->deviceId()!=deviceId()) throw std::invalid_argument("pipeline layout belongs to another device session");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(resources_.native().physical(),&properties);
    if(description.bindingLayouts.size()>properties.limits.maxBoundDescriptorSets)
        throw std::invalid_argument("pipeline exceeds binding layout limit");
    VkShaderStageFlags usedConstantStages=0;
    for(const auto& range:description.constantRanges)
    {
        const auto visibility=toVulkanStages(range.stages);
        if(!visibility || (usedConstantStages&visibility) || !range.size || (range.offset%4) || (range.size%4) ||
            range.offset>properties.limits.maxPushConstantsSize || range.size>properties.limits.maxPushConstantsSize-range.offset)
            throw std::invalid_argument("invalid pipeline constant range");
        usedConstantStages|=visibility;
    }
    std::set<uint32_t> bindings,locations;
    for(const auto& binding:description.vertexLayout.bindings)
        if(!bindings.insert(binding.binding).second || binding.binding>=properties.limits.maxVertexInputBindings ||
            binding.stride>properties.limits.maxVertexInputBindingStride)
            throw std::invalid_argument("invalid pipeline vertex binding");
    for(const auto& attribute:description.vertexLayout.attributes)
        if(!locations.insert(attribute.location).second || !bindings.count(attribute.binding) ||
            attribute.location>=properties.limits.maxVertexInputAttributes || attribute.offset>properties.limits.maxVertexInputAttributeOffset)
            throw std::invalid_argument("invalid pipeline vertex attribute");
    const auto supports=[&](Format format,VkImageUsageFlags usage,VkFormatFeatureFlags features,uint32_t samples) {
        const auto native=toVulkan(format);
        (void)resources_.native().findSupportedFormat({native},VK_IMAGE_TILING_OPTIMAL,features);
        VkImageFormatProperties image{};
        if(vkGetPhysicalDeviceImageFormatProperties(resources_.native().physical(),native,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&image)!=VK_SUCCESS ||
            !(image.sampleCounts&samples)) throw std::invalid_argument("pipeline attachment format/sample count is unsupported");
    };
    for(const auto& color:signature.colors)
    {
        if(color.format==Format::Undefined || depthFormat(color.format)) throw std::invalid_argument("invalid pipeline color format");
        supports(color.format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,signature.samples);
        if(color.resolveFormat!=Format::Undefined)
        {
            if(signature.samples==1 || color.resolveFormat!=color.format) throw std::invalid_argument("resolve requires multisampling and a matching format");
            supports(color.resolveFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,1);
        }
    }
    if(signature.depthStencilFormat!=Format::Undefined)
    {
        if(!depthFormat(signature.depthStencilFormat)) throw std::invalid_argument("invalid pipeline depth format");
        supports(signature.depthStencilFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,signature.samples);
    }
    GraphicsPipeline::validateState(resources_.native(),nativeGraphicsPipelineDescription(description));
}
rhi::PipelineRef VulkanPipelineDevice::createGraphicsPipeline(const rhi::GraphicsPipelineDesc& description) const
{
    validateGraphicsPipeline(description);
    auto result=std::make_shared<PipelineOwner>();
    result->device=deviceId();result->desc=description;
    auto info=nativeGraphicsPipelineDescription(description);
    if(description.target) info.renderPass=nativePipelineTarget(description.target);
    else info.renderPass=compatiblePass(resources_.native(),description.attachments).reference();
    result->pass = info.renderPass;
    result->value.create(resources_.native(),info);
    return result;
}
} // namespace rubia::rhi::vulkan
