#pragma once

#include "EditorFwd.hpp"
#include "asset/AssetFwd.hpp"
#include "texture/TextureImportSettings.hpp"

#include <filesystem>
#include <functional>

namespace rubia::editor
{

/// Handles produced while loading the fixed demo content used by the runtime.
struct DemoContent
{
    asset::TextureAssetHandle defaultTexture;
    asset::TextureAssetHandle defaultDataTexture;
    asset::TextureAssetHandle defaultNormalTexture;
    asset::ShaderAssetHandle pbrVertexShader;
    asset::ShaderAssetHandle pbrFragmentShader;
    asset::ShaderAssetHandle presentVertexShader;
    asset::ShaderAssetHandle presentFragmentShader;
    asset::ShaderProgramAssetHandle pbrProgram;
    asset::ShaderProgramAssetHandle presentProgram;
    asset::MaterialTemplateAssetHandle materialTemplate;
    asset::MaterialAssetHandle defaultMaterial;
    asset::ModelAssetHandle model;
};

/// Loads the existing built-in assets, demo model, and demo scene.
class DemoContentLoader final
{
public:
    /// Demo-only defaults applied to the editable import record after a glTF
    /// texture is decoded. The policy chooses settings; it does not cook the
    /// texture or change the generic glTF importer.
    struct TextureImportPolicy
    {
        importer::texture::KtxPayloadEncoding payloadEncoding = importer::texture::KtxPayloadEncoding::Uastc;
        bool generateMipmaps = false;
        asset::TextureFormat colorTranscodeFormat = asset::TextureFormat::BC7UNorm;
        asset::TextureFormat dataTranscodeFormat = asset::TextureFormat::BC7UNorm;
        asset::TextureFormat normalTranscodeFormat = asset::TextureFormat::BC5UNorm;
        bool highQualityTranscode = true;
    };

    struct CreateInfo
    {
        std::filesystem::path modelPath;
        std::filesystem::path pbrVertexShader;
        std::filesystem::path pbrFragmentShader;
        std::filesystem::path presentVertexShader;
        std::filesystem::path presentFragmentShader;
        std::filesystem::path cookedAssetDirectory;
        TextureImportPolicy textureImportPolicy;
    };

    [[nodiscard]] static DemoContent load(
        asset::AssetManager& assets,
        scene::Scene& scene,
        const CreateInfo& createInfo,
        importer::texture::TextureImportRegistry* textureImports = nullptr,
        const std::function<void()>& checkpoint = {});

    /// Renderer defaults can be prepared independently of any model file.
    [[nodiscard]] static DemoContent loadBuiltins(
        asset::AssetManager& assets, const CreateInfo& createInfo);
    [[nodiscard]] static DemoContent loadModel(
        asset::AssetManager& assets, scene::Scene& scene,
        const CreateInfo& createInfo, DemoContent builtins,
        importer::texture::TextureImportRegistry* textureImports = nullptr,
        const std::function<void()>& checkpoint = {});
};

} // namespace rubia::editor
