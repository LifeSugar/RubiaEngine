#include "shader/SpirvShaderImporter.hpp"

#include "shader/SpirvReflection.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::importer::shader
{

asset::ShaderAssetHandle SpirvShaderImporter::import(
    const CreateInfo& createInfo) const
{
    if (createInfo.assets == nullptr)
    {
        throw std::invalid_argument(
            "SpirvShaderImporter requires an AssetManager");
    }
    if (createInfo.path.empty())
    {
        throw std::invalid_argument(
            "SpirvShaderImporter requires a source path");
    }

    std::ifstream file(
        createInfo.path,
        std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error(
            "failed to open SPIR-V shader: " +
            createInfo.path.string());
    }

    const std::streamoff byteSize =
        static_cast<std::streamoff>(file.tellg());
    if (byteSize <= 0 ||
        byteSize % static_cast<std::streamoff>(sizeof(uint32_t)) != 0 ||
        static_cast<uint64_t>(byteSize) >
            std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error(
            "SPIR-V shader has an invalid byte size: " +
            createInfo.path.string());
    }

    asset::ShaderAsset::CreateInfo shaderInfo{};
    shaderInfo.name = createInfo.name.empty()
        ? createInfo.path.stem().string()
        : createInfo.name;
    shaderInfo.stage = createInfo.stage;
    shaderInfo.entryPoint = createInfo.entryPoint;
    std::vector<uint32_t> words(
        static_cast<std::size_t>(byteSize) / sizeof(uint32_t));

    file.seekg(0);
    if (!file.read(
            reinterpret_cast<char*>(words.data()),
            static_cast<std::streamsize>(byteSize)))
    {
        throw std::runtime_error(
            "failed to read SPIR-V shader: " +
            createInfo.path.string());
    }

    if (words.size() < 5 || words.front() != 0x07230203u)
        throw std::runtime_error("Invalid legacy SPIR-V header");
    const auto profile = "spirv_" + std::to_string((words[1] >> 16) & 0xff) +
        "_" + std::to_string((words[1] >> 8) & 0xff);
    shaderInfo.artifact = ShaderArtifact::copy({ShaderBackend::Vulkan, profile},
        ShaderFormat::SpirV, words.data(), static_cast<size_t>(byteSize));
    shaderInfo.interface = SpirvReflection::reflect(
        words,
        shaderInfo.stage,
        shaderInfo.entryPoint);

    return createInfo.assets->createShader(std::move(shaderInfo));
}

} // namespace rubia::importer::shader
