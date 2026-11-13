#pragma once

#include "vulkan/Framebuffer.hpp"
#include "rhi/Resources.hpp"
#include "vulkan/ImageView.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <vector>

namespace rubia::rhi::vulkan
{

class Device;

/// Owns the images, image views, and framebuffer for one offscreen target.
///
/// Attachment order must match the VkRenderPass supplied at creation. The
/// render pass is a non-owning compatibility contract and must outlive the
/// framebuffer owned by this object.
class RenderTarget final
{
public:
    /// Describes one image-backed framebuffer attachment.
    struct AttachmentInfo
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
        VkImageAspectFlags aspectMask = 0;
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
        VkMemoryPropertyFlags memoryProperties =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    };

    /// Parameters used to build one framebuffer and its owned attachments.
    struct CreateInfo
    {
        /// Non-owning render pass compatible with attachments in this order.
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkExtent2D extent{};
        std::vector<AttachmentInfo> attachments;
    };

    RenderTarget() = default;
    RenderTarget(const Device& device, const CreateInfo& createInfo);
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;

    /// Creates or transactionally replaces the complete render target.
    void create(const Device& device, const CreateInfo& createInfo);
    /// Releases the framebuffer before its attachment views and images.
    void reset() noexcept;

    [[nodiscard]] VkFramebuffer framebuffer() const noexcept
    {
        return framebuffer_.get();
    }
    [[nodiscard]] VkRenderPass renderPass() const noexcept
    {
        return renderPass_;
    }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] std::size_t attachmentCount() const noexcept
    {
        return attachments_.size();
    }

    [[nodiscard]] const rhi::TextureRef& texture(std::size_t attachmentIndex) const;
    [[nodiscard]] const rhi::TextureViewRef& view(std::size_t attachmentIndex) const;
    [[nodiscard]] VkImage image(std::size_t attachmentIndex) const;
    [[nodiscard]] VkImageView imageView(std::size_t attachmentIndex) const;
    [[nodiscard]] const AttachmentInfo& attachmentInfo(
        std::size_t attachmentIndex) const;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(framebuffer_);
    }

private:
    struct AttachmentResources
    {
        AttachmentInfo info;
        rhi::TextureRef image;
        rhi::TextureViewRef view;
    };

    [[nodiscard]] const AttachmentResources& attachment(
        std::size_t attachmentIndex) const;

    // Declaration order makes the framebuffer die before its image views.
    std::vector<AttachmentResources> attachments_;
    Framebuffer framebuffer_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
};

} // namespace rubia::rhi::vulkan
