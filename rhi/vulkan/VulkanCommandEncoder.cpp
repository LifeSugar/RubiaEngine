#include "VulkanCommandEncoder.hpp"
#include "VulkanResourceDevice.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
RenderEncoder& VulkanCommandEncoder::beginPass(const RenderPassDesc& desc)
{
    if (activePass_) throw std::logic_error("render pass already active");
    const auto* target = dynamic_cast<const VulkanPassTarget*>(desc.target);
    if (!target || target->deviceId() != device_ || !target->pass() || !target->framebuffer() || !target->width() || !target->height())
        throw std::invalid_argument("invalid render pass target");
    std::vector<VkClearValue> clears(desc.clears.size());
    for (size_t i = 0; i < clears.size(); ++i)
    {
        const auto& source = desc.clears[i];
        if (source.depthStencil) clears[i].depthStencil = {source.depth, source.stencil};
        else for (size_t component = 0; component < 4; ++component)
            clears[i].color.float32[component] = source.color[component];
    }
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = target->pass();
    info.framebuffer = target->framebuffer();
    info.renderArea.extent = {target->width(), target->height()};
    info.clearValueCount = static_cast<uint32_t>(clears.size());
    info.pClearValues = clears.data();
    vkCmdBeginRenderPass(commands_, &info, VK_SUBPASS_CONTENTS_INLINE);
    render_ = VulkanRenderEncoder(device_, commands_, target->passReference());
    activePass_ = true;
    return render_;
}
void VulkanCommandEncoder::endPass()
{
    if (!activePass_) throw std::logic_error("no render pass active");
    vkCmdEndRenderPass(commands_);
    activePass_ = false;
}
void VulkanCommandEncoder::transition(const TextureRef& texture, const SubresourceRange& range,
    ResourceAccess before, ResourceAccess after)
{
    if (activePass_) throw std::logic_error("texture transition inside render pass");
    if (!texture || texture->deviceId() != device_)
        throw std::invalid_argument("texture transition belongs to another device");
    const auto& desc = texture->description();
    const bool depth = desc.format == Format::D32Float || desc.format == Format::D32FloatS8 || desc.format == Format::D24UNormS8;
    const uint32_t aspects = depth ? flag(TextureAspect::Depth) |
        (desc.format == Format::D32Float ? 0 : flag(TextureAspect::Stencil)) : flag(TextureAspect::Color);
    // This Vulkan 1.0 subset does not enable separate depth/stencil layouts.
    if (range.aspects != aspects || !range.mipLevels || !range.arrayLayers ||
        range.baseMipLevel >= desc.mipLevels || range.mipLevels > desc.mipLevels - range.baseMipLevel ||
        range.baseArrayLayer >= desc.arrayLayers || range.arrayLayers > desc.arrayLayers - range.baseArrayLayer)
        throw std::invalid_argument("texture transition subresource range is invalid");
    const auto validateAccess = [&](ResourceAccess state, bool source) {
        uint32_t requiredUsage = 0;
        switch (state.usage)
        {
        case AccessUsage::Undefined:
            if (source) return;
            throw std::invalid_argument("texture transition destination cannot be undefined");
        case AccessUsage::CopySource: requiredUsage = flag(TextureUsage::TransferSource); break;
        case AccessUsage::CopyDestination: requiredUsage = flag(TextureUsage::TransferDestination); break;
        case AccessUsage::ShaderRead: requiredUsage = flag(TextureUsage::Sampled); break;
        case AccessUsage::ShaderReadWrite: requiredUsage = flag(TextureUsage::Storage); break;
        case AccessUsage::ColorWrite:
            if (depth) throw std::invalid_argument("depth texture cannot use color access");
            requiredUsage = flag(TextureUsage::ColorAttachment); break;
        case AccessUsage::DepthReadWrite:
            if (!depth) throw std::invalid_argument("color texture cannot use depth access");
            requiredUsage = flag(TextureUsage::DepthStencilAttachment); break;
        case AccessUsage::Present:
            if (depth || desc.samples != 1) throw std::invalid_argument("invalid presentation texture");
            break;
        case AccessUsage::HostRead:
        case AccessUsage::HostWrite: break;
        default: throw std::invalid_argument("buffer access is invalid for a texture transition");
        }
        if (requiredUsage && !(desc.usage & requiredUsage))
            throw std::invalid_argument("texture transition requires a missing texture usage");
    };
    validateAccess(before, true);
    validateAccess(after, false);
    const auto source = toVulkanAccess(before), destination = toVulkanAccess(after);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = source.access;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = source.layout;
    barrier.newLayout = destination.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = nativeImage(texture).get();
    barrier.subresourceRange = {toVulkanAspects(range.aspects), range.baseMipLevel, range.mipLevels,
        range.baseArrayLayer, range.arrayLayers};
    vkCmdPipelineBarrier(commands_, source.stages, destination.stages, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}
} // namespace rubia::rhi::vulkan
