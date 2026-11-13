#include "texture/KtxTextureImporter.hpp"

#include "texture/Ktx2Format.hpp"

#include <ktx.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::importer::texture
{
namespace
{

using KtxTexture =
    std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)>;

[[noreturn]] void throwKtxError(
    const char* operation,
    KTX_error_code error)
{
    throw std::runtime_error(
        std::string(operation) + ": " + ktxErrorString(error));
}

ktx_transcode_fmt_e transcodeTarget(asset::TextureFormat format)
{
    switch (format)
    {
    case asset::TextureFormat::BC1RGBUNorm:
        return KTX_TTF_BC1_RGB;
    case asset::TextureFormat::BC3UNorm:
        return KTX_TTF_BC3_RGBA;
    case asset::TextureFormat::BC4UNorm:
        return KTX_TTF_BC4_R;
    case asset::TextureFormat::BC5UNorm:
        return KTX_TTF_BC5_RG;
    case asset::TextureFormat::BC7UNorm:
        return KTX_TTF_BC7_RGBA;
    default:
        throw std::invalid_argument(
            "KTX2 Basis transcoding target must be BC1 RGB, BC3, BC4, BC5, or BC7");
    }
}

void validateTextureShape(const ktxTexture2& texture)
{
    if (texture.numDimensions != 2 || texture.baseWidth == 0 ||
        texture.baseHeight == 0 || texture.baseDepth != 1 ||
        texture.numLevels == 0 || texture.isArray || texture.isCubemap ||
        texture.numLayers != 1 || texture.numFaces != 1 ||
        texture.generateMipmaps)
    {
        throw std::invalid_argument(
            "KtxTextureImporter currently requires a complete 2D, non-array, non-cubemap KTX2 texture");
    }
}

struct CollectedLevels
{
    struct Level
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<std::byte> payload;
        bool present = false;
    };

    std::vector<Level> levels;
    std::exception_ptr error;
};

KTX_error_code collectLevel(
    int mipLevel,
    int face,
    int width,
    int height,
    int depth,
    ktx_uint64_t byteSize,
    void* data,
    void* userData)
{
    auto& collected = *static_cast<CollectedLevels*>(userData);
    try
    {
        if (mipLevel < 0 ||
            static_cast<std::size_t>(mipLevel) >= collected.levels.size() ||
            face != 0 || width <= 0 || height <= 0 || depth != 1 ||
            data == nullptr || byteSize == 0 ||
            byteSize > std::numeric_limits<std::size_t>::max())
        {
            return KTX_INVALID_VALUE;
        }

        CollectedLevels::Level& level =
            collected.levels[static_cast<std::size_t>(mipLevel)];
        if (level.present)
        {
            return KTX_INVALID_OPERATION;
        }

        level.width = static_cast<uint32_t>(width);
        level.height = static_cast<uint32_t>(height);
        level.payload.resize(static_cast<std::size_t>(byteSize));
        std::memcpy(level.payload.data(), data, level.payload.size());
        level.present = true;
        return KTX_SUCCESS;
    }
    catch (...)
    {
        collected.error = std::current_exception();
        return KTX_OUT_OF_MEMORY;
    }
}

KtxTexture openFile(const std::filesystem::path& path)
{
    if (path.empty())
    {
        throw std::invalid_argument(
            "KtxTextureImporter requires a file path");
    }

    ktxTexture2* rawTexture = nullptr;
    const std::string nativePath = path.u8string();
    const KTX_error_code createResult = ktxTexture2_CreateFromNamedFile(
        nativePath.c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &rawTexture);
    if (createResult != KTX_SUCCESS)
    {
        throwKtxError("failed to open KTX2 file", createResult);
    }
    return KtxTexture(rawTexture, &ktxTexture2_Destroy);
}

asset::TextureAsset::CreateInfo buildTextureAsset(
    ktxTexture2& texture,
    const KtxTextureImporter::CreateInfo& createInfo)
{
    validateTextureShape(texture);

    if (ktxTexture2_NeedsTranscoding(&texture))
    {
        const ktx_transcode_fmt_e target =
            transcodeTarget(createInfo.transcodeFormat);
        const asset::TextureFormatInfo targetInfo =
            textureFormatInfo(createInfo.transcodeFormat);
        if (ktxTexture2_GetTransferFunction_e(&texture) ==
                KHR_DF_TRANSFER_SRGB &&
            !targetInfo.supportsSrgb)
        {
            throw std::invalid_argument(
                "cannot transcode an sRGB KTX2 texture to a BC format without an sRGB variant");
        }

        const ktx_transcode_flags flags = createInfo.highQuality
            ? KTX_TF_HIGH_QUALITY
            : 0;
        const KTX_error_code result = ktxTexture2_TranscodeBasis(
            &texture,
            target,
            flags);
        if (result != KTX_SUCCESS)
        {
            throwKtxError("failed to transcode KTX2 Basis payload", result);
        }
    }

    const std::optional<Ktx2TextureFormatMapping> mapping =
        textureFormatFromKtx2(texture.vkFormat);
    if (!mapping)
    {
        throw std::invalid_argument(
            "KTX2 format code is not supported by TextureAsset");
    }

    CollectedLevels collected{};
    collected.levels.resize(texture.numLevels);
    const KTX_error_code iterateResult = ktxTexture_IterateLevels(
        ktxTexture(&texture),
        &collectLevel,
        &collected);
    if (collected.error)
    {
        std::rethrow_exception(collected.error);
    }
    if (iterateResult != KTX_SUCCESS)
    {
        throwKtxError("failed to iterate KTX2 mip levels", iterateResult);
    }

    asset::TextureAsset::CreateInfo result{};
    result.name = createInfo.name;
    result.width = texture.baseWidth;
    result.height = texture.baseHeight;
    result.format = mapping->format;
    result.colorSpace = mapping->colorSpace;
    result.sampler = createInfo.sampler;
    result.mipLevels.reserve(collected.levels.size());

    for (uint32_t mipLevel = 0;
         mipLevel < static_cast<uint32_t>(collected.levels.size());
         ++mipLevel)
    {
        const CollectedLevels::Level& source = collected.levels[mipLevel];
        const uint32_t expectedWidth = std::max(1u, result.width >>
            std::min(mipLevel, 31u));
        const uint32_t expectedHeight = std::max(1u, result.height >>
            std::min(mipLevel, 31u));
        const std::size_t expectedSize = textureMipByteSize(
            result.format,
            expectedWidth,
            expectedHeight);
        if (!source.present || source.width != expectedWidth ||
            source.height != expectedHeight ||
            source.payload.size() != expectedSize)
        {
            throw std::invalid_argument(
                "KTX2 mip layout does not match its format and dimensions");
        }
        if (result.payload.size() >
            std::numeric_limits<std::size_t>::max() - expectedSize)
        {
            throw std::overflow_error(
                "KTX2 texture payload exceeds the host address range");
        }

        const std::size_t byteOffset = result.payload.size();
        result.payload.insert(
            result.payload.end(),
            source.payload.begin(),
            source.payload.end());
        result.mipLevels.push_back({
            expectedWidth,
            expectedHeight,
            byteOffset,
            expectedSize});
    }
    return result;
}

} // namespace

asset::TextureAsset::CreateInfo KtxTextureImporter::importMemory(
    const void* data,
    std::size_t size,
    const CreateInfo& createInfo) const
{
    if (data == nullptr || size == 0 ||
        size > std::numeric_limits<ktx_size_t>::max())
    {
        throw std::invalid_argument(
            "KtxTextureImporter memory payload is invalid");
    }

    ktxTexture2* rawTexture = nullptr;
    const KTX_error_code createResult = ktxTexture2_CreateFromMemory(
        static_cast<const ktx_uint8_t*>(data),
        static_cast<ktx_size_t>(size),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &rawTexture);
    if (createResult != KTX_SUCCESS)
    {
        throwKtxError("failed to parse KTX2 payload", createResult);
    }

    KtxTexture texture(rawTexture, &ktxTexture2_Destroy);
    return buildTextureAsset(*texture, createInfo);
}

asset::TextureAsset::CreateInfo KtxTextureImporter::importFile(
    const std::filesystem::path& path,
    const CreateInfo& createInfo) const
{
    KtxTexture texture = openFile(path);
    CreateInfo resolvedInfo = createInfo;
    if (resolvedInfo.name.empty())
    {
        resolvedInfo.name = path.filename().u8string();
    }
    return buildTextureAsset(*texture, resolvedInfo);
}

} // namespace rubia::importer::texture
