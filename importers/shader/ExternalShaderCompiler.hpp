#pragma once
#include "asset/ShaderAsset.hpp"
#include <filesystem>

namespace rubia::importer::shader::detail
{
enum class ShaderCompiler { Slang };

// Shared process isolation, timeout, diagnostics and binary output.
asset::ShaderAsset::CreateInfo compileExternalShader(const std::filesystem::path& source,
    asset::ShaderStage stage, const std::string& entryPoint, ShaderCompiler compiler);
}
