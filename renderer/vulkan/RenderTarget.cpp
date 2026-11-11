#include "vulkan/RenderTarget.hpp"

#include "vulkan/Device.hpp"
#include "VulkanResourceDevice.hpp"

#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

RenderTarget::RenderTarget(
    const Device& device,
    const CreateInfo& createInfo)
{
    create(device, createInfo);
}

RenderTarget::~RenderTarget()
{
    reset();
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : attachments_(std::move(other.attachments_)),
      framebuffer_(std::move(other.framebuffer_)),
      renderPass_(std::exchange(other.renderPass_, VK_NULL_HANDLE)),
      extent_(std::exchange(other.extent_, VkExtent2D{}))
{
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept
{
    if (this != &other)
    {
        reset();
        attachments_ = std::move(other.attachments_);
        framebuffer_ = std::move(other.framebuffer_);
        renderPass_ = std::exchange(other.renderPass_, VK_NULL_HANDLE);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
    }
    return *this;
}

void RenderTarget::create(
    const Device& device,
    const CreateInfo& createInfo)
{
    if (!device ||
        createInfo.renderPass == VK_NULL_HANDLE ||
        createInfo.extent.width == 0 ||
        createInfo.extent.height == 0 ||
        createInfo.attachments.empty())
    {
        throw std::invalid_argument("render target create info is incomplete");
    }

    std::vector<AttachmentResources> newAttachments;
    std::vector<VkImageView> newAttachmentViews;
    newAttachments.reserve(createInfo.attachments.size());
    newAttachmentViews.reserve(createInfo.attachments.size());

    VulkanResourceDevice resourceDevice(device);
    for (const AttachmentInfo& info : createInfo.attachments)
    {
        if (info.format == VK_FORMAT_UNDEFINED ||
            info.usage == 0 ||
            info.aspectMask == 0 ||
            info.samples == 0)
        {
            throw std::invalid_argument(
                "render target attachment description is incomplete");
        }

        AttachmentResources resources;
        resources.info = info;
        if (info.tiling != VK_IMAGE_TILING_OPTIMAL)
            throw std::invalid_argument("render targets require optimal image tiling");
        rhi::TextureDesc imageInfo;
        imageInfo.extent = {createInfo.extent.width, createInfo.extent.height, 1};
        imageInfo.format = fromVulkanFormat(info.format);
        imageInfo.samples = static_cast<uint32_t>(info.samples);
        constexpr VkImageUsageFlags allowedUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (info.usage & ~allowedUsage) throw std::invalid_argument("unsupported render target image usage");
        if (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) imageInfo.usage |= flag(TextureUsage::ColorAttachment);
        if (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) imageInfo.usage |= flag(TextureUsage::DepthStencilAttachment);
        if (info.usage & VK_IMAGE_USAGE_SAMPLED_BIT) imageInfo.usage |= flag(TextureUsage::Sampled);
        if (info.usage & VK_IMAGE_USAGE_STORAGE_BIT) imageInfo.usage |= flag(TextureUsage::Storage);
        if (info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) imageInfo.usage |= flag(TextureUsage::TransferSource);
        if (info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) imageInfo.usage |= flag(TextureUsage::TransferDestination);
        constexpr VkMemoryPropertyFlags allowedMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if (info.memoryProperties & ~allowedMemory) throw std::invalid_argument("unsupported render target memory properties");
        imageInfo.requiredMemory = 0;
        if (info.memoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) imageInfo.requiredMemory |= flag(MemoryProperty::DeviceLocal);
        if (info.memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) imageInfo.requiredMemory |= flag(MemoryProperty::HostVisible);
        if (info.memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) imageInfo.requiredMemory |= flag(MemoryProperty::HostCoherent);
        if (info.memoryProperties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) imageInfo.requiredMemory |= flag(MemoryProperty::HostCached);
        resources.image = resourceDevice.createTexture(imageInfo);
        rhi::TextureViewDesc viewInfo;
        viewInfo.format = imageInfo.format;
        viewInfo.range.aspects = 0;
        if (info.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) viewInfo.range.aspects |= flag(TextureAspect::Color);
        if (info.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) viewInfo.range.aspects |= flag(TextureAspect::Depth);
        if (info.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) viewInfo.range.aspects |= flag(TextureAspect::Stencil);
        resources.view = resourceDevice.createTextureView(resources.image, viewInfo);
        newAttachmentViews.push_back(nativeTextureView(resources.view));
        newAttachments.push_back(std::move(resources));
    }

    Framebuffer newFramebuffer(
        device.get(),
        createInfo.renderPass,
        newAttachmentViews,
        createInfo.extent);

    // Commit only after every attachment and the framebuffer succeeded.
    reset();
    attachments_ = std::move(newAttachments);
    framebuffer_ = std::move(newFramebuffer);
    renderPass_ = createInfo.renderPass;
    extent_ = createInfo.extent;
}

void RenderTarget::reset() noexcept
{
    framebuffer_.reset();
    attachments_.clear();
    renderPass_ = VK_NULL_HANDLE;
    extent_ = {};
}

const rhi::TextureRef& RenderTarget::texture(std::size_t index) const { return attachment(index).image; }
const rhi::TextureViewRef& RenderTarget::view(std::size_t index) const { return attachment(index).view; }

VkImage RenderTarget::image(std::size_t attachmentIndex) const
{
    return nativeImage(attachment(attachmentIndex).image).get();
}

VkImageView RenderTarget::imageView(std::size_t attachmentIndex) const
{
    return nativeTextureView(attachment(attachmentIndex).view);
}

const RenderTarget::AttachmentInfo& RenderTarget::attachmentInfo(
    std::size_t attachmentIndex) const
{
    return attachment(attachmentIndex).info;
}

const RenderTarget::AttachmentResources& RenderTarget::attachment(
    std::size_t attachmentIndex) const
{
    if (attachmentIndex >= attachments_.size())
    {
        throw std::out_of_range("render target attachment index is out of range");
    }
    return attachments_[attachmentIndex];
}

} // namespace rubia::rhi::vulkan
