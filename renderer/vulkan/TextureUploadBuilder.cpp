#include "vulkan/TextureUploadBuilder.hpp"
#include "vulkan/TextureVkFormat.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{
VkFilter textureFilter(asset::TextureFilter filter)
{
    return filter == asset::TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerMipmapMode mipFilter(asset::TextureFilter filter)
{
    return filter == asset::TextureFilter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
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

} // namespace
GpuTexture::CreateInfo makeTextureCreateInfo(const asset::TextureAsset& source)
{
    if (!source || source.mipLevels().empty() ||
        source.mipLevels().size() > std::numeric_limits<uint32_t>::max())
    {
        throw std::invalid_argument("incomplete texture asset or excessive mip count");
    }
    GpuTexture::CreateInfo info;
    info.image.extent = {source.width(), source.height(), 1};
    info.image.mipLevels = static_cast<uint32_t>(source.mipLevels().size());
    info.image.format = textureVkFormat(source.format(), source.colorSpace());
    info.image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.image.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const auto& sampler = source.sampler();
    info.sampler.magFilter = textureFilter(sampler.magFilter);
    info.sampler.minFilter = textureFilter(sampler.minFilter);
    info.sampler.mipmapMode = mipFilter(sampler.mipFilter);
    info.sampler.addressModeU = addressMode(sampler.addressU);
    info.sampler.addressModeV = addressMode(sampler.addressV);
    info.sampler.addressModeW = addressMode(sampler.addressW);
    info.sampler.maxAnisotropy = 1.0f;
    info.sampler.maxLod = static_cast<float>(info.image.mipLevels - 1);
    return info;
}

UploadRequest makeTextureUploadRequest(std::shared_ptr<GpuTexture> texture,
                                       std::shared_ptr<const asset::TextureAsset> source)
{
    if (!texture || !*texture || !source || !*source)
    {
        throw std::invalid_argument("missing texture upload owner");
    }
    const auto& asset = *source;
    const auto& desc = texture->image().description();
    const auto mipCount = desc.mipLevels;
    if (desc.type != VK_IMAGE_TYPE_2D || desc.arrayLayers != 1 || desc.extent.depth != 1 ||
        asset.width() != desc.extent.width || asset.height() != desc.extent.height ||
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

    ImageUpload op;
    op.destination = std::shared_ptr<const Image>(texture, &texture->image());
    op.source = {source, asset.payload().data(), asset.payload().size()};
    op.regions = std::move(copyRegions);
    op.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1};
    op.before = {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    op.after = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT};
    UploadRequest request;
    request.operations.emplace_back(std::move(op));
    return request;
}

} // namespace rubia::rhi::vulkan
