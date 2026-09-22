#pragma once
#include "asset/ShaderAsset.hpp"
#include <filesystem>
namespace rubia::importer::shader
{
// CPU-only DXC invocation. Call from an import worker, never the render thread.
class HlslShaderCompiler final
{
  public:
    [[nodiscard]] asset::ShaderAsset::CreateInfo compile(
        const std::filesystem::path &source, asset::ShaderStage stage,
        const std::string &entryPoint = "main") const;
};
} // namespace rubia::importer::shader
