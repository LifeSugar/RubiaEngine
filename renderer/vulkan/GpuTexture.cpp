#include "vulkan/GpuTexture.hpp"

#include "vulkan/Device.hpp"
#include "vulkan/TextureVkFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{
namespace
{

VkFilter textureFilter(asset::TextureFilter filter)
{
    return filter == asset::TextureFilter::Nearest
        ? VK_FILTER_NEAREST
        : VK_FILTER_LINEAR;
}

VkSamplerMipmapMode mipFilter(asset::TextureFilter filter)
{
    return filter == asset::TextureFilter::Nearest
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST
        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkSamplerAddressMode addressMode(asset::TextureAddressMode mode)
{
    switch (mode)
    {
    case asset::TextureAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case asset::TextureAddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case asset::TextureAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkImageSubresourceRange resolveViewRange(
    VkImageSubresourceRange range,
    uint32_t mipLevels,
    uint32_t arrayLayers)
{
    if (range.aspectMask == 0 ||
        range.baseMipLevel >= mipLevels ||
        range.baseArrayLayer >= arrayLayers)
    {
        throw std::invalid_argument(
            "GpuTexture view range starts outside the image");
    }

    if (range.levelCount == VK_REMAINING_MIP_LEVELS)
    {
        range.levelCount = mipLevels - range.baseMipLevel;
    }
    if (range.layerCount == VK_REMAINING_ARRAY_LAYERS)
    {
        range.layerCount = arrayLayers - range.baseArrayLayer;
    }
    if (range.levelCount == 0 ||
        range.levelCount > mipLevels - range.baseMipLevel ||
        range.layerCount == 0 ||
        range.layerCount > arrayLayers - range.baseArrayLayer)
    {
        throw std::invalid_argument(
            "GpuTexture view range exceeds the image");
    }
    return range;
}

} // namespace

GpuTexture::GpuTexture(
    const Device& device,
    UploadContext& uploadContext,
    const CreateInfo& createInfo)
{
    create(device, uploadContext, createInfo);
}

GpuTexture::~GpuTexture()
{
    reset();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED)),
      image_(std::move(other.image_)),
      view_(std::move(other.view_)),
      sampler_(std::exchange(other.sampler_, VK_NULL_HANDLE))
{
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept
{
    if (this != &other)
    {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        image_ = std::move(other.image_);
        view_ = std::move(other.view_);
        sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
    }
    return *this;
}

void GpuTexture::create(const Device& device, UploadContext& uploadContext, const CreateInfo& createInfo)
{
    GpuTexture replacement;
    replacement.allocate(device, createInfo);
    uploadContext.uploadImage(replacement.uploadInfo(*createInfo.asset));
    *this = std::move(replacement);
}

void GpuTexture::allocate(const Device& device, const CreateInfo& createInfo)
{
    if (!device || createInfo.asset == nullptr ||
        !*createInfo.asset || createInfo.asset->mipLevels().empty())
    {
        throw std::invalid_argument(
            "cannot create GpuTexture from incomplete inputs");
    }
    const asset::TextureAsset& asset = *createInfo.asset;

    if (asset.mipLevels().size() >
        std::numeric_limits<uint32_t>::max())
    {
        throw std::overflow_error(
            "GpuTexture mip count exceeds the Vulkan limit");
    }

    const uint32_t mipCount =
        static_cast<uint32_t>(asset.mipLevels().size());
    const VkFormat format = device.findSupportedFormat(
        {textureVkFormat(asset.format(), asset.colorSpace())},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    GpuTexture replacement;
    replacement.device_ = device.get();
    replacement.format_ = format;
    Image::CreateInfo imageInfo{};
    imageInfo.extent = {
        asset.width(),
        asset.height(),
        1};
    imageInfo.mipLevels = mipCount;
    imageInfo.format = format;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    replacement.image_.create(device, imageInfo);

    const VkImageSubresourceRange viewRange = resolveViewRange(
        createInfo.viewRange,
        imageInfo.mipLevels,
        imageInfo.arrayLayers);

    ImageView::CreateInfo viewInfo{};
    viewInfo.image = replacement.image_.get();
    viewInfo.type = createInfo.viewType;
    viewInfo.format = format;
    viewInfo.components = createInfo.components;
    viewInfo.subresourceRange = viewRange;
    replacement.view_.create(device.get(), viewInfo);

    const asset::TextureSamplerDesc& sourceSampler = asset.sampler();
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = textureFilter(sourceSampler.magFilter);
    samplerInfo.minFilter = textureFilter(sourceSampler.minFilter);
    samplerInfo.mipmapMode = mipFilter(sourceSampler.mipFilter);
    samplerInfo.addressModeU = addressMode(sourceSampler.addressU);
    samplerInfo.addressModeV = addressMode(sourceSampler.addressV);
    samplerInfo.addressModeW = addressMode(sourceSampler.addressW);
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(viewRange.levelCount - 1);
    if (vkCreateSampler(
            device.get(),
            &samplerInfo,
            nullptr,
            &replacement.sampler_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create a texture sampler");
    }

    *this = std::move(replacement);
}

UploadContext::ImageUploadInfo GpuTexture::uploadInfo(const asset::TextureAsset& asset) const
{
    const auto& desc = image_.description();
    const auto mipCount = desc.mipLevels;
    if (asset.width() != desc.extent.width || asset.height() != desc.extent.height ||
        asset.mipLevels().size() != mipCount ||
        textureVkFormat(asset.format(), asset.colorSpace()) != desc.format)
    {
        throw std::invalid_argument("texture upload source does not match allocated image");
    }
    std::vector<VkBufferImageCopy> copyRegions;
    copyRegions.reserve(mipCount);
    for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
    {
        const asset::TextureMipLevel& mip = asset.mipLevels()[mipIndex];
        const uint32_t expectedWidth = std::max(1u, asset.width() >> std::min(mipIndex, 31u));
        const uint32_t expectedHeight = std::max(1u, asset.height() >> std::min(mipIndex, 31u));
        if (mip.width != expectedWidth || mip.height != expectedHeight)
        {
            throw std::invalid_argument("GpuTexture mip dimensions do not match the base image");
        }

        VkBufferImageCopy region{};
        region.bufferOffset = static_cast<VkDeviceSize>(mip.byteOffset);
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipIndex;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {mip.width, mip.height, 1};
        copyRegions.push_back(region);
    }

    if (asset.payload().size() > std::numeric_limits<VkDeviceSize>::max())
    {
        throw std::overflow_error("GpuTexture payload exceeds the Vulkan address range");
    }

    UploadContext::ImageUploadInfo info;
    info.sourceData = asset.payload().data();
    info.sourceSize = asset.payload().size();
    info.destination = {image_.get(), desc.format, desc.extent, desc.mipLevels, desc.arrayLayers};
    info.copyRegions = std::move(copyRegions);
    info.destinationRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1};
    return info;
}

UploadRequest GpuTexture::makeUploadRequest(std::shared_ptr<GpuTexture> texture,
                                            std::shared_ptr<const asset::TextureAsset> source)
{
    if (!texture || !*texture || !source)
    {
        throw std::invalid_argument("missing texture upload owner");
    }
    auto info = texture->uploadInfo(*source);
    ImageUpload op;
    op.destination = std::shared_ptr<const Image>(texture, &texture->image_);
    op.source = {source, source->payload().data(), source->payload().size()};
    op.regions = std::move(info.copyRegions);
    op.range = info.destinationRange;
    UploadRequest request;
    request.operations.emplace_back(std::move(op));
    return request;
}

void GpuTexture::reset() noexcept
{
    if (device_ != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, sampler_, nullptr);
    }
    sampler_ = VK_NULL_HANDLE;
    view_.reset();
    image_.reset();
    format_ = VK_FORMAT_UNDEFINED;
    device_ = VK_NULL_HANDLE;
}

} // namespace rubia::rhi::vulkan
