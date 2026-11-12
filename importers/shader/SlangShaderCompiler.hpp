#pragma once
#include "asset/ShaderAsset.hpp"
#include <filesystem>

namespace rubia::importer::shader
{
// Slang code generation plus target-specific SDK reflection. No SPIRV-Cross.
// Call from an import worker, never the render thread. The target controls code generation and reflection.
class SlangShaderCompiler final
{
public:
    [[nodiscard]] asset::ShaderAsset::CreateInfo compile(const std::filesystem::path& source,
        asset::ShaderStage stage, const std::string& entryPoint = "main", const ShaderTarget& target = {}) const;
};
}
