#pragma once
#include "asset/ShaderAsset.hpp"
#include <filesystem>
namespace rubia::importer::shader
{
// Compatibility spelling: HLSL input now uses Slang compilation and reflection.
class HlslShaderCompiler final
{
  public:
    [[nodiscard]] asset::ShaderAsset::CreateInfo compile(
        const std::filesystem::path &source, asset::ShaderStage stage,
        const std::string &entryPoint = "main") const;
};
} // namespace rubia::importer::shader
