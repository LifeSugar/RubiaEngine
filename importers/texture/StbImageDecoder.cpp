#include "texture/StbImageDecoder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rubia::importer::texture
{
namespace
{

asset::TextureAsset::CreateInfo decodeRgba8(
    const uint8_t* encodedBytes,
    std::size_t encodedSize,
    std::string name)
{
    if (encodedBytes == nullptr || encodedSize == 0 ||
        encodedSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument(
            "stb_image encoded image payload has an invalid size");
    }

    int width = 0;
    int height = 0;
    using DecodedPayload =
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    DecodedPayload decodedPayload(
        stbi_load_from_memory(
            encodedBytes,
            static_cast<int>(encodedSize),
            &width,
            &height,
            nullptr,
            STBI_rgb_alpha),
        &stbi_image_free);
    if (!decodedPayload)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            std::string("stb_image failed to decode image") +
            (reason == nullptr ? "" : std::string(": ") + reason));
    }
    if (width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() /
                static_cast<std::size_t>(height) / 4)
    {
        throw std::runtime_error("stb_image decoded image dimensions are invalid");
    }

    const std::size_t byteSize =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    asset::TextureAsset::CreateInfo textureInfo{};
    textureInfo.name = std::move(name);
    textureInfo.width = static_cast<uint32_t>(width);
    textureInfo.height = static_cast<uint32_t>(height);
    textureInfo.format = asset::TextureFormat::RGBA8UNorm;
    textureInfo.payload.resize(byteSize);
    std::memcpy(
        textureInfo.payload.data(),
        decodedPayload.get(),
        byteSize);
    return textureInfo;
}

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path)
{
    if (path.empty())
    {
        throw std::invalid_argument("stb_image decoder requires a file path");
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error(
            "failed to open image file: " + path.string());
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0 ||
        static_cast<std::uintmax_t>(fileSize) >
            std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error(
            "image file has an invalid size: " + path.string());
    }

    std::vector<uint8_t> bytes(static_cast<std::size_t>(fileSize));
    file.seekg(0);
    if (!file.read(
            reinterpret_cast<char*>(bytes.data()),
            fileSize))
    {
        throw std::runtime_error(
            "failed to read image file: " + path.string());
    }
    return bytes;
}

} // namespace

asset::TextureAsset::CreateInfo StbImageDecoder::decodeMemory(
    const std::vector<uint8_t>& encodedBytes,
    const std::string& name) const
{
    return decodeRgba8(encodedBytes.data(), encodedBytes.size(), name);
}

asset::TextureAsset::CreateInfo StbImageDecoder::decodeFile(
    const std::filesystem::path& path,
    const std::string& name) const
{
    const std::vector<uint8_t> encodedBytes = readFileBytes(path);
    return decodeRgba8(
        encodedBytes.data(),
        encodedBytes.size(),
        name.empty() ? path.filename().u8string() : name);
}

} // namespace rubia::importer::texture
