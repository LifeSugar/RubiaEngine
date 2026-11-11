#include "vulkan/RenderPass.hpp"
#include "vulkan/PipelineKeyData.hpp"
#include "VulkanResourceDevice.hpp"
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
RenderPass::RenderPass(VkDevice device, const VkRenderPassCreateInfo &info)
{
    create(device, info);
}
RenderPass::~RenderPass() = default;
RenderPass::RenderPass(RenderPass &&other) noexcept = default;
RenderPass &RenderPass::operator=(RenderPass &&other) noexcept = default;
void RenderPass::reset() noexcept
{
    state_.reset();
}
RenderPassState::~RenderPassState()
{
    if (handle_)
        vkDestroyRenderPass(device_, handle_, nullptr);
}

void RenderPass::create(VkDevice device, const VkRenderPassCreateInfo &info)
{
    if (!device || info.pNext || !info.subpassCount || !info.pSubpasses ||
        (info.attachmentCount && !info.pAttachments) ||
        (info.dependencyCount && !info.pDependencies))
        throw std::invalid_argument(
            "RenderPass requires a device and core descriptions without pNext");
    auto state = std::shared_ptr<RenderPassState>(new RenderPassState);
    state->device_ = device;
    detail::PipelineKeyData key;
    key.add(info.flags);
    key.add(info.attachmentCount);
    for (uint32_t i = 0; i < info.attachmentCount; ++i)
    {
        const auto &a = info.pAttachments[i];
        key.add(a.flags);
        key.add(a.format);
        key.add(a.samples);
        // Load/store and initial/final layouts do not affect compatibility.
    }
    const auto references = [&](uint32_t count, const VkAttachmentReference *items) {
        key.add(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto index = items ? items[i].attachment : VK_ATTACHMENT_UNUSED;
            if (index != VK_ATTACHMENT_UNUSED && index >= info.attachmentCount)
                throw std::invalid_argument("render pass attachment reference is out of range");
            // Conservative: retaining indices/counts can miss compatible reorderings,
            // but cannot alias incompatible passes. Reference layouts are omitted.
            key.add(index);
        }
    };
    key.add(info.subpassCount);
    for (uint32_t i = 0; i < info.subpassCount; ++i)
    {
        const auto &s = info.pSubpasses[i];
        if ((s.colorAttachmentCount && !s.pColorAttachments) ||
            (s.inputAttachmentCount && !s.pInputAttachments) ||
            (s.preserveAttachmentCount && !s.pPreserveAttachments))
            throw std::invalid_argument("render pass subpass arrays are missing");
        key.add(s.flags);
        key.add(s.pipelineBindPoint);
        references(s.inputAttachmentCount, s.pInputAttachments);
        references(s.colorAttachmentCount, s.pColorAttachments);
        references(s.colorAttachmentCount, s.pResolveAttachments);
        references(1, s.pDepthStencilAttachment);
        key.add(s.preserveAttachmentCount);
        for (uint32_t j = 0; j < s.preserveAttachmentCount; ++j)
            key.add(s.pPreserveAttachments[j]);
        state->colorCounts_.push_back(s.colorAttachmentCount);
    }
    key.add(info.dependencyCount);
    for (uint32_t i = 0; i < info.dependencyCount; ++i)
    {
        const auto &d = info.pDependencies[i];
        key.add(d.srcSubpass);
        key.add(d.dstSubpass);
        key.add(d.srcStageMask);
        key.add(d.dstStageMask);
        key.add(d.srcAccessMask);
        key.add(d.dstAccessMask);
        key.add(d.dependencyFlags);
    }
    state->key_ = std::move(key.words);
    // Keep legacy render passes available; only the expressible subset receives
    // a portable signature. No opaque Vulkan compatibility words cross the RHI.
    if (info.subpassCount == 1 && !info.pSubpasses[0].inputAttachmentCount &&
        !info.pSubpasses[0].preserveAttachmentCount)
    {
        try
        {
            const auto& subpass = info.pSubpasses[0];
            rhi::AttachmentSignature signature;
            bool hasSamples = false;
            const auto sampleCount = [&](uint32_t attachment) {
                const auto count = static_cast<uint32_t>(info.pAttachments[attachment].samples);
                if (hasSamples && signature.samples != count)
                    throw rhi::ResourceError(rhi::ResourceErrorCode::Unsupported,
                                             "mixed attachment samples are outside the RHI subset");
                signature.samples = count;
                hasSamples = true;
            };
            for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i)
            {
                const auto color = subpass.pColorAttachments[i].attachment;
                if (color == VK_ATTACHMENT_UNUSED)
                    throw rhi::ResourceError(rhi::ResourceErrorCode::Unsupported,
                                             "unused color slots are outside the RHI subset");
                sampleCount(color);
                rhi::ColorAttachmentSignature target;
                target.format = fromVulkanFormat(info.pAttachments[color].format);
                if (subpass.pResolveAttachments &&
                    subpass.pResolveAttachments[i].attachment != VK_ATTACHMENT_UNUSED)
                    target.resolveFormat = fromVulkanFormat(
                        info.pAttachments[subpass.pResolveAttachments[i].attachment].format);
                signature.colors.push_back(target);
            }
            if (subpass.pDepthStencilAttachment &&
                subpass.pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
            {
                const auto depth = subpass.pDepthStencilAttachment->attachment;
                sampleCount(depth);
                signature.depthStencilFormat = fromVulkanFormat(info.pAttachments[depth].format);
            }
            state->signature_ = std::move(signature);
        }
        catch (const rhi::ResourceError&)
        {
            // Native-only low-level tests can still use formats/subpasses outside RHI.
        }
    }
    if (vkCreateRenderPass(device, &info, nullptr, &state->handle_) != VK_SUCCESS)
        throw std::runtime_error("failed to create render pass");
    state_ = std::move(state);
}
} // namespace rubia::rhi::vulkan
