#pragma once
#include "rubia/ShaderArtifact.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
inline ShaderArtifact spirvArtifact(const std::vector<uint32_t>& words)
{
    if (words.size() < 5 || words.front() != 0x07230203u)
        throw std::invalid_argument("invalid SPIR-V header");
    const auto profile = "spirv_" + std::to_string((words[1] >> 16) & 0xff) +
        "_" + std::to_string((words[1] >> 8) & 0xff);
    return ShaderArtifact::copy({ShaderBackend::Vulkan, profile}, ShaderFormat::SpirV,
                                words.data(), words.size() * sizeof(uint32_t));
}
inline std::vector<uint32_t> spirvWords(const ShaderArtifact& artifact)
{
    if (artifact.target.backend != ShaderBackend::Vulkan || artifact.format != ShaderFormat::SpirV)
        throw std::invalid_argument("Vulkan requires a Vulkan SPIR-V shader artifact");
    if (artifact.bytes.size() < 5 * sizeof(uint32_t) || artifact.bytes.size() % sizeof(uint32_t))
        throw std::invalid_argument("invalid SPIR-V byte size");
    std::vector<uint32_t> words(artifact.bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), artifact.bytes.data(), artifact.bytes.size());
    if (words.front() != 0x07230203u)
        throw std::invalid_argument("invalid SPIR-V magic");
    return words;
}
}
