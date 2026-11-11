#include "VulkanPass.hpp"
#include "VulkanResourceDevice.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
namespace
{
bool isDepth(Format format)
{
    return format == Format::D32Float || format == Format::D32FloatS8 || format == Format::D24UNormS8;
}
void validateAccess(ResourceAccess access, bool depth, bool before)
{
    switch (access.usage)
    {
    case AccessUsage::Undefined:
        if (before) return;
        break;
    case AccessUsage::ColorWrite:
    case AccessUsage::Present:
        if (!depth) return;
        break;
    case AccessUsage::DepthReadWrite:
        if (depth) return;
        break;
    case AccessUsage::CopySource:
    case AccessUsage::CopyDestination:
    case AccessUsage::ShaderRead:
    case AccessUsage::ShaderReadWrite:
    case AccessUsage::HostRead:
    case AccessUsage::HostWrite:
        return;
    default: break;
    }
    throw std::invalid_argument("render pass attachment has an invalid texture access state");
}
}
RenderPass createRenderPass(const Device& device, const rhi::RenderPassLayoutDesc& desc)
{
    const auto load = [](LoadOp value) {
        switch (value) {
        case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Discard: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        throw std::invalid_argument("invalid attachment load operation");
    };
    const auto store = [](StoreOp value) {
        switch (value) {
        case StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::Discard: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        throw std::invalid_argument("invalid attachment store operation");
    };
    if (desc.attachments.empty())
        throw std::invalid_argument("render pass requires an attachment in the supported subset");
    std::vector<VkAttachmentDescription> attachments;
    for (const auto& a : desc.attachments)
    {
        if (a.format == Format::Undefined || !a.samples || a.samples > 64 || (a.samples & (a.samples - 1)))
            throw std::invalid_argument("invalid attachment format or sample count");
        validateAccess(a.before, isDepth(a.format), true);
        validateAccess(a.after, isDepth(a.format), false);
        if (a.before.usage == AccessUsage::Undefined &&
            (a.load == LoadOp::Load || a.stencilLoad == LoadOp::Load))
            throw std::invalid_argument("attachment load requires defined previous contents");
        if ((a.before.usage == AccessUsage::Present || a.after.usage == AccessUsage::Present) && a.samples != 1)
            throw std::invalid_argument("present attachment must be single sampled");
        const auto before = toVulkanAccess(a.before), after = toVulkanAccess(a.after);
        attachments.push_back({0, toVulkan(a.format), static_cast<VkSampleCountFlagBits>(a.samples),
            load(a.load), store(a.store), load(a.stencilLoad), store(a.stencilStore), before.layout, after.layout});
    }
    const auto validIndex = [&](int64_t index) { return index >= 0 && static_cast<size_t>(index) < attachments.size(); };
    std::vector<VkAttachmentReference> colors, resolves;
    std::vector<bool> used(attachments.size(), false);
    uint32_t rasterSamples = 0;
    const auto useAttachment = [&](uint32_t index, bool depth, bool raster) {
        if (used[index] || isDepth(desc.attachments[index].format) != depth)
            throw std::invalid_argument("attachment is reused or has the wrong format for its role");
        used[index] = true;
        if (raster)
        {
            const auto samples = desc.attachments[index].samples;
            if (rasterSamples && samples != rasterSamples)
                throw std::invalid_argument("mixed raster attachment sample counts are unsupported");
            rasterSamples = samples;
        }
    };
    bool hasResolve = false;
    for (const auto& c : desc.colors)
    {
        if (!validIndex(c.attachment) || (c.resolve != -1 && !validIndex(c.resolve)))
            throw std::invalid_argument("render pass color attachment index out of range");
        useAttachment(c.attachment, false, true);
        if (c.resolve >= 0)
        {
            useAttachment(static_cast<uint32_t>(c.resolve), false, false);
            const auto& source = desc.attachments[c.attachment];
            const auto& destination = desc.attachments[c.resolve];
            if (source.samples == 1 || destination.samples != 1 || destination.format != source.format)
                throw std::invalid_argument("resolve requires multisampled source and matching single-sample target");
        }
        colors.push_back({c.attachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        resolves.push_back({c.resolve < 0 ? VK_ATTACHMENT_UNUSED : static_cast<uint32_t>(c.resolve),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        hasResolve |= c.resolve >= 0;
    }
    if (desc.depthStencil != -1 && !validIndex(desc.depthStencil))
        throw std::invalid_argument("render pass depth attachment index out of range");
    if (desc.depthStencil >= 0) useAttachment(static_cast<uint32_t>(desc.depthStencil), true, true);
    for (const auto referenced : used)
        if (!referenced) throw std::invalid_argument("unused attachments are outside the supported pass subset");
    VkAttachmentReference depth{static_cast<uint32_t>(desc.depthStencil), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    subpass.pColorAttachments = colors.data();
    subpass.pResolveAttachments = hasResolve ? resolves.data() : nullptr;
    subpass.pDepthStencilAttachment = desc.depthStencil >= 0 ? &depth : nullptr;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    for (size_t i = 0; i < desc.attachments.size(); ++i)
    {
        const auto& a = desc.attachments[i];
        const auto before = toVulkanAccess(a.before), after = toVulkanAccess(a.after);
        const auto during = toVulkanAccess({static_cast<int32_t>(i) == desc.depthStencil ? AccessUsage::DepthReadWrite : AccessUsage::ColorWrite});
        // Discard permits undefined contents, but execution still waits for prior
        // attachment use. Frame-slot/queue completion supplies external reuse safety.
        deps[0].srcStageMask |= a.before.usage == AccessUsage::Undefined ? during.stages : before.stages;
        deps[0].srcAccessMask |= before.access;
        deps[0].dstStageMask |= during.stages; deps[0].dstAccessMask |= during.access;
        deps[1].srcStageMask |= during.stages; deps[1].srcAccessMask |= during.access;
        deps[1].dstStageMask |= after.stages; deps[1].dstAccessMask |= after.access;
    }
    for (auto& dep : deps) dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<uint32_t>(attachments.size()); info.pAttachments = attachments.data();
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size()); info.pDependencies = deps.data();
    return RenderPass(device.get(), info);
}
} // namespace rubia::rhi::vulkan
