#pragma once
// LEGACY: precompiled SPIR-V compatibility only. Source shaders use Slang.

#include "asset/ShaderInterface.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rubia::importer::shader
{

/// CPU-only SPIR-V reflection. It does not create a Vulkan shader module.
class SpirvReflection final
{
public:
    [[nodiscard]] static asset::ShaderInterface reflect(
        const std::vector<uint32_t>& spirv,
        asset::ShaderStage stage,
        const std::string& entryPoint);
};

} // namespace rubia::importer::shader
