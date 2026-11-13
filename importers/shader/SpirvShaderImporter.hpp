#pragma once
// LEGACY: precompiled SPIR-V compatibility only. Source shaders use Slang.

#include "asset/AssetManager.hpp"

#include <filesystem>
#include <string>

namespace rubia::importer::shader
{

/// Imports one SPIR-V file into the source-independent ShaderAsset format.
class SpirvShaderImporter final
{
public:
    struct CreateInfo
    {
        asset::AssetManager* assets = nullptr;
        std::filesystem::path path;
        std::string name;
        asset::ShaderStage stage = asset::ShaderStage::Vertex;
        std::string entryPoint = "main";
    };

    [[nodiscard]] asset::ShaderAssetHandle import(
        const CreateInfo& createInfo) const;
};

} // namespace rubia::importer::shader
