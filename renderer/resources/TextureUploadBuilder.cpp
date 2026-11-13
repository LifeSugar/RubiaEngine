#include "resources/TextureUploadBuilder.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace rubia::render
{
rhi::Format textureFormat(asset::TextureFormat format, asset::TextureColorSpace colorSpace)
{
    const bool srgb = colorSpace == asset::TextureColorSpace::Srgb;
    switch (format)
    {
    case asset::TextureFormat::R8UNorm:
        return srgb ? rhi::Format::R8Srgb : rhi::Format::R8UNorm;
    case asset::TextureFormat::RG8UNorm:
        return srgb ? rhi::Format::RG8Srgb : rhi::Format::RG8UNorm;
    case asset::TextureFormat::RGBA8UNorm:
        return srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8UNorm;
    case asset::TextureFormat::RGBA16Float:
        if (!srgb)
            return rhi::Format::RGBA16Float;
        break;
    case asset::TextureFormat::RGBA32Float:
        if (!srgb)
            return rhi::Format::RGBA32Float;
        break;
    case asset::TextureFormat::BC1RGBUNorm:
        return srgb ? rhi::Format::BC1RGBSrgb : rhi::Format::BC1RGBUNorm;
    case asset::TextureFormat::BC1RGBAUNorm:
        return srgb ? rhi::Format::BC1RGBASrgb : rhi::Format::BC1RGBAUNorm;
    case asset::TextureFormat::BC2UNorm:
        return srgb ? rhi::Format::BC2Srgb : rhi::Format::BC2UNorm;
    case asset::TextureFormat::BC3UNorm:
        return srgb ? rhi::Format::BC3Srgb : rhi::Format::BC3UNorm;
    case asset::TextureFormat::BC4UNorm:
        if (!srgb)
            return rhi::Format::BC4UNorm;
        break;
    case asset::TextureFormat::BC4SNorm:
        if (!srgb)
            return rhi::Format::BC4SNorm;
        break;
    case asset::TextureFormat::BC5UNorm:
        if (!srgb)
            return rhi::Format::BC5UNorm;
        break;
    case asset::TextureFormat::BC5SNorm:
        if (!srgb)
            return rhi::Format::BC5SNorm;
        break;
    case asset::TextureFormat::BC6HUFloat:
        if (!srgb)
            return rhi::Format::BC6HUFloat;
        break;
    case asset::TextureFormat::BC6HSFloat:
        if (!srgb)
            return rhi::Format::BC6HSFloat;
        break;
    case asset::TextureFormat::BC7UNorm:
        return srgb ? rhi::Format::BC7Srgb : rhi::Format::BC7UNorm;
    case asset::TextureFormat::Undefined:
        break;
    }
    throw std::invalid_argument("texture format and color space have no render representation");
}

TextureResource::CreateInfo makeTextureCreateInfo(const asset::TextureAsset &source)
{
    if (!source || source.mipLevels().empty() || source.mipLevels().size() > UINT32_MAX)
        throw std::invalid_argument("invalid texture asset");
    TextureResource::CreateInfo info;
    info.image.extent = {source.width(), source.height(), 1};
    info.image.mipLevels = static_cast<uint32_t>(source.mipLevels().size());
    info.image.format = textureFormat(source.format(), source.colorSpace());
    info.image.usage =
        rhi::flag(rhi::TextureUsage::TransferDestination) | rhi::flag(rhi::TextureUsage::Sampled);
    info.viewRange.mipLevels = info.image.mipLevels;
    const auto &sampler = source.sampler();
    const auto filter = [](asset::TextureFilter value)
    { return value == asset::TextureFilter::Nearest ? rhi::Filter::Nearest : rhi::Filter::Linear; };
    const auto address = [](asset::TextureAddressMode value)
    {
        switch (value)
        {
        case asset::TextureAddressMode::Repeat:
            return rhi::AddressMode::Repeat;
        case asset::TextureAddressMode::MirroredRepeat:
            return rhi::AddressMode::MirroredRepeat;
        case asset::TextureAddressMode::ClampToEdge:
            return rhi::AddressMode::ClampToEdge;
        }
        throw std::invalid_argument("invalid texture address mode");
    };
    info.sampler.minFilter = filter(sampler.minFilter);
    info.sampler.magFilter = filter(sampler.magFilter);
    info.sampler.mipFilter = filter(sampler.mipFilter);
    info.sampler.addressU = address(sampler.addressU);
    info.sampler.addressV = address(sampler.addressV);
    info.sampler.addressW = address(sampler.addressW);
    info.sampler.maxLod = static_cast<float>(info.image.mipLevels - 1);
    return info;
}
rhi::UploadRequest makeTextureUploadRequest(std::shared_ptr<TextureResource> texture,
                                            std::shared_ptr<const asset::TextureAsset> source)
{
    if (!texture || !*texture || !source || !*source)
        throw std::invalid_argument("missing texture upload owner");
    const auto &desc = texture->image()->description();
    if (desc.dimension != rhi::TextureDimension::D2 || desc.arrayLayers != 1 ||
        desc.extent.depth != 1 || source->width() != desc.extent.width ||
        source->height() != desc.extent.height || source->mipLevels().size() != desc.mipLevels ||
        textureFormat(source->format(), source->colorSpace()) != desc.format)
        throw std::invalid_argument("texture upload source does not match allocation");
    rhi::TextureUpload upload;
    upload.destination = texture->image();
    upload.source = {source, source->payload().data(), source->payload().size()};
    upload.range.mipLevels = desc.mipLevels;
    upload.before = {rhi::AccessUsage::Undefined, 0};
    upload.after = {rhi::AccessUsage::ShaderRead, rhi::shaderStageMask(rhi::ShaderStage::Fragment)};
    for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
    {
        const auto &input = source->mipLevels()[mip];
        if (input.width != std::max(1u, source->width() >> std::min(mip, 31u)) ||
            input.height != std::max(1u, source->height() >> std::min(mip, 31u)))
            throw std::invalid_argument("invalid texture mip extent");
        rhi::TextureCopyRegion copy;
        copy.sourceOffset = input.byteOffset;
        copy.mipLevel = mip;
        copy.extent = {input.width, input.height, 1};
        upload.regions.push_back(copy);
    }
    rhi::UploadRequest result;
    result.operations.emplace_back(std::move(upload));
    return result;
}
} // namespace rubia::render
