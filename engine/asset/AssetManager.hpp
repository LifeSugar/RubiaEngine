#pragma once

#include "asset/AssetRegistry.hpp"
#include "asset/AssetSnapshot.hpp"
#include "asset/MaterialAsset.hpp"
#include "asset/MaterialTemplateAsset.hpp"
#include "asset/MeshAsset.hpp"
#include "asset/ModelAsset.hpp"
#include "asset/ShaderAsset.hpp"
#include "asset/ShaderProgramAsset.hpp"
#include "asset/TextureAsset.hpp"
#include "asset/ValidationReport.hpp"

namespace rubia::asset
{

/// Owns validated CPU assets without knowing how their CreateInfo was produced.
class AssetManager final
{
public:
    [[nodiscard]] AssetDomainId domain() const
    {
        return domain_.id();
    }
    [[nodiscard]] AssetSnapshot<TextureAsset> snapshot(TextureAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<MeshAsset> snapshot(MeshAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<ShaderAsset> snapshot(ShaderAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<ShaderProgramAsset> snapshot(ShaderProgramAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<MaterialTemplateAsset> snapshot(
        MaterialTemplateAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<MaterialAsset> snapshot(MaterialAssetHandle handle) const;
    [[nodiscard]] AssetSnapshot<ModelAsset> snapshot(ModelAssetHandle handle) const;
    [[nodiscard]] MeshAsset replaceMesh(MeshAssetHandle handle, MeshAsset replacement);
    [[nodiscard]] TextureAssetHandle createTexture(
        TextureAsset::CreateInfo createInfo);
    /// Replaces texture content while preserving references held by materials.
    [[nodiscard]] TextureAsset replaceTexture(
        TextureAssetHandle handle,
        TextureAsset replacement);
    [[nodiscard]] MaterialTemplateAssetHandle createMaterialTemplate(
        MaterialTemplateAsset::CreateInfo createInfo);
    [[nodiscard]] MaterialAssetHandle createMaterial(
        MaterialAsset::CreateInfo createInfo);
    [[nodiscard]] ValidationReport validateMaterialTemplate(
        const MaterialTemplateAsset::CreateInfo& createInfo) const;
    [[nodiscard]] ValidationReport validateMaterial(
        const MaterialAsset::CreateInfo& createInfo) const;
    [[nodiscard]] MeshAssetHandle createMesh(
        MeshAsset::CreateInfo createInfo);
    [[nodiscard]] ShaderAssetHandle createShader(
        ShaderAsset::CreateInfo createInfo);
    [[nodiscard]] ShaderProgramAssetHandle createShaderProgram(
        ShaderProgramAsset::CreateInfo createInfo);
    [[nodiscard]] ModelAssetHandle createModel(
        ModelAsset::CreateInfo createInfo);

    [[nodiscard]] const TextureAsset& texture(TextureAssetHandle handle) const;
    [[nodiscard]] const MaterialTemplateAsset& materialTemplate(
        MaterialTemplateAssetHandle handle) const;
    [[nodiscard]] const MaterialAsset& material(MaterialAssetHandle handle) const;
    [[nodiscard]] const MeshAsset& mesh(MeshAssetHandle handle) const;
    [[nodiscard]] const ShaderAsset& shader(ShaderAssetHandle handle) const;
    [[nodiscard]] const ShaderProgramAsset& shaderProgram(ShaderProgramAssetHandle handle) const;
    [[nodiscard]] const ModelAsset& model(ModelAssetHandle handle) const;

    [[nodiscard]] bool contains(TextureAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(MaterialTemplateAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(MaterialAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(MeshAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(ShaderAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(ShaderProgramAssetHandle handle) const noexcept;
    [[nodiscard]] bool contains(ModelAssetHandle handle) const noexcept;

    /// The version of this asset's own content, not its dependency graph.
    /// Invalid or stale handles throw, just like the asset accessors above.
    [[nodiscard]] AssetContentRevision contentRevision(TextureAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(MaterialTemplateAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(MaterialAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(MeshAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(ShaderAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(ShaderProgramAssetHandle handle) const;
    [[nodiscard]] AssetContentRevision contentRevision(ModelAssetHandle handle) const;

    /// Metadata only: does not retain bytes or synchronize concurrent asset replacement.
    template <typename Asset>
    [[nodiscard]] AssetVersion<Asset> version(AssetHandle<Asset> handle) const
    {
        return {handle, contentRevision(handle)};
    }

    template <typename Asset>
    [[nodiscard]] bool isCurrent(AssetVersion<Asset> version) const noexcept
    {
        return contains(version.handle) &&
               contentRevision(version.handle) == version.contentRevision;
    }

    [[nodiscard]] bool isMaterialTemplateCurrent(
        MaterialTemplateAssetHandle handle) const noexcept;

    [[nodiscard]] std::vector<TextureAssetHandle> textureHandles() const;
    [[nodiscard]] std::vector<MaterialAssetHandle> materialHandles() const;
    [[nodiscard]] std::vector<ModelAssetHandle> modelHandles() const;

    void reset() noexcept;

private:
    AnyAssetSnapshot captureSnapshot(AnyAssetHandle root) const;
    AssetDomain domain_;
    AssetRegistry<TextureAsset> textures_;
    AssetRegistry<MaterialTemplateAsset> materialTemplates_;
    AssetRegistry<MaterialAsset> materials_;
    AssetRegistry<MeshAsset> meshes_;
    AssetRegistry<ShaderAsset> shaders_;
    AssetRegistry<ShaderProgramAsset> shaderPrograms_;
    AssetRegistry<ModelAsset> models_;
};

} // namespace rubia::asset
