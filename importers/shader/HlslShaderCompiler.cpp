#include "shader/HlslShaderCompiler.hpp"
#include "shader/SlangShaderCompiler.hpp"

namespace rubia::importer::shader
{
asset::ShaderAsset::CreateInfo HlslShaderCompiler::compile(const std::filesystem::path& source,
    asset::ShaderStage stage, const std::string& entryPoint) const
{
    return SlangShaderCompiler{}.compile(source, stage, entryPoint);
}
} // namespace rubia::importer::shader
