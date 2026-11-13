#pragma once

#include "asset/AssetManager.hpp"
#include "gltf/GLBMaterialImporter.hpp"
#include "gltf/GLBTextureImporter.hpp"

#include <filesystem>
#include <functional>
#include <vector>

namespace rubia::importer::gltf
{

/// Orchestrates the GLB-specific importers and creates one ModelAsset.
class GLBModelImporter final
{
public:
    struct CreateInfo
    {
        asset::AssetManager* assets = nullptr;
        /// Optional cooperative cancellation checkpoint between import phases.
        std::function<void()> checkpoint;

        std::filesystem::path baseDirectory;
        asset::TextureColorSpace textureColorSpace = asset::TextureColorSpace::Srgb;
        asset::TextureSamplerDesc textureSampler;
        asset::TextureAssetHandle defaultTexture;
        asset::TextureAssetHandle defaultDataTexture;
        asset::TextureAssetHandle defaultNormalTexture;
        GLBTextureImporter::Decoder textureDecoder;

        GLBMaterialMapping materialMapping;
        asset::MaterialAssetHandle fallbackMaterial;
    };

    struct Result
    {
        asset::ModelAssetHandle model;
        std::vector<asset::TextureAssetHandle> textures;
        std::vector<asset::MaterialAssetHandle> materials;
        std::vector<asset::MeshAssetHandle> meshes;
    };

    [[nodiscard]] Result import(
        const GLBModel& source,
        const CreateInfo& createInfo) const;
};

} // namespace rubia::importer::gltf
