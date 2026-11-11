#pragma once

#include "asset/AssetManager.hpp"
#include "resources/MaterialBinding.hpp"
#include "resources/PreparedAssets.hpp"
#include "resources/TextureResource.hpp"
#include "resources/IResourcePreparation.hpp"
#include "resources/MeshGeometry.hpp"
#include <type_traits>

#include <cstdint>
#include <vector>

namespace rubia::render
{

/// Resident GPU representations. Publication hands replaced owners to the caller;
/// frame slots and GPU-safe retirement belong to Renderer, not this cache.
class RenderAssetStore final
{
  public:
    RenderAssetStore() = default;
    ~RenderAssetStore();

    RenderAssetStore(const RenderAssetStore &) = delete;
    RenderAssetStore &operator=(const RenderAssetStore &) = delete;
    RenderAssetStore(RenderAssetStore &&) = delete;
    RenderAssetStore &operator=(RenderAssetStore &&) = delete;

    void publishTexture(asset::TextureAssetHandle handle, TextureResource texture);
    void publishMesh(asset::MeshAssetHandle handle, MeshGeometry mesh);
    bool empty() const noexcept;
    struct PreparationVersions
    {
        asset::AssetContentRevision resident = 0;
        asset::AssetContentRevision requested = 0;
        std::vector<asset::AssetDependencyVersion> residentDependencies;
        std::vector<asset::AssetDependencyVersion> requestedDependencies;
    };
    // One asset domain per cache until reset. A recycled generation requires cache reset.
    PreparationVersions inspectPreparation(const ResourcePreparationTarget &target) const;
    void acceptPreparation(const ResourcePreparationTarget &target);
    bool dependenciesCurrent(const ResourcePreparationTarget &target) const;
    struct TextureRebindPlan;
    // Main-thread snapshot -> worker build -> main-thread atomic publication.
    std::shared_ptr<TextureRebindPlan> captureTextureRebind(asset::TextureAssetHandle,
                                                            asset::AssetContentRevision) const;
    static void buildTextureRebind(const rhi::Device &, TextureRebindPlan &,
                                   const TextureResource &);
    void publishPreparedTexture(RetirementBatch &retired, const ResourcePreparationTarget &target,
                                TextureResource texture, std::shared_ptr<TextureRebindPlan> rebind);
    void publishPreparedMesh(RetirementBatch &retired, const ResourcePreparationTarget &target,
                             MeshGeometry mesh);
    // Refresh dependency readiness without allocating/uploading unchanged geometry.
    void publishPreparedMeshDependencies(const ResourcePreparationTarget &target);
    void publishPreparedShader(RetirementBatch &, const ResourcePreparationTarget &,
                               std::shared_ptr<const PreparedShader>);
    void publishPreparedShaderProgram(RetirementBatch &, const ResourcePreparationTarget &,
                                      std::shared_ptr<const PreparedShaderProgram>);
    void publishPreparedMaterialTemplate(RetirementBatch &, const ResourcePreparationTarget &,
                                         std::shared_ptr<const PreparedMaterialTemplate>);

    void publishPreparedMaterial(RetirementBatch &, const ResourcePreparationTarget &,
                                 MaterialBinding, std::shared_ptr<const PreparedMaterialTemplate>,
                                 std::vector<asset::TextureAssetHandle>);
    std::shared_ptr<const PreparedShader> shader(asset::ShaderAssetHandle) const;
    std::shared_ptr<const PreparedShaderProgram>
        shaderProgram(asset::ShaderProgramAssetHandle) const;
    std::shared_ptr<const PreparedMaterialTemplate>
        materialTemplate(asset::MaterialTemplateAssetHandle) const;
    /// Exact template/program version used to publish this material's descriptors.
    std::shared_ptr<const PreparedMaterialTemplate>
    materialTemplateForMaterial(asset::MaterialAssetHandle handle) const;
    uint64_t texturePublication(asset::TextureAssetHandle handle) const noexcept;

    void reset() noexcept;

    [[nodiscard]] const MeshGeometry &mesh(asset::MeshAssetHandle handle) const;
    [[nodiscard]] const MeshGeometry *tryMesh(asset::MeshAssetHandle handle) const noexcept;
    [[nodiscard]] const MaterialBinding &material(asset::MaterialAssetHandle handle) const;
    [[nodiscard]] const MaterialBinding *
    tryMaterial(asset::MaterialAssetHandle handle) const noexcept;
    /// Returns an uploaded texture for Editor previews and material binding.
    [[nodiscard]] const TextureResource &texture(asset::TextureAssetHandle handle) const;
    [[nodiscard]] const TextureResource *
    tryTexture(asset::TextureAssetHandle handle) const noexcept;

  private:
    struct VersionedEntry
    {
        uint32_t generation = 0;
        asset::AssetContentRevision revision = 0;
        asset::AssetContentRevision requested = 0;
        std::vector<asset::AssetDependencyVersion> residentDependencies;
        std::vector<asset::AssetDependencyVersion> requestedDependencies;
    };
    struct TextureEntry : VersionedEntry
    {
        uint64_t publication = 0;
        TextureResource texture;
    };

    // Retire the immutable binding version as one unit. Its parameter allocation,
    // referenced textures, layout and prepared template outlive submitted uses.
    struct MaterialResources
    {
        MaterialBinding material;
        std::vector<asset::TextureAssetHandle> textures;
        asset::MaterialTemplateAsset layout;
        std::shared_ptr<const PreparedMaterialTemplate> preparedTemplate;
    };

    struct MaterialEntry : VersionedEntry
    {
        MaterialResources resources;
    };

    struct MeshEntry : VersionedEntry
    {
        MeshGeometry mesh;
    };

    template <typename Resource> struct PreparedEntry : VersionedEntry
    {
        std::shared_ptr<const Resource> resource;
    };
    template <typename Handle> auto &entries(Handle)
    {
        if constexpr (std::is_same_v<Handle, asset::TextureAssetHandle>)
        {
            return textures_;
        }
        else if constexpr (std::is_same_v<Handle, asset::MeshAssetHandle>)
        {
            return meshes_;
        }
        else if constexpr (std::is_same_v<Handle, asset::ShaderAssetHandle>)
        {
            return shaders_;
        }
        else if constexpr (std::is_same_v<Handle, asset::ShaderProgramAssetHandle>)
        {
            return programs_;
        }
        else if constexpr (std::is_same_v<Handle, asset::MaterialTemplateAssetHandle>)
        {
            return templates_;
        }
        else if constexpr (std::is_same_v<Handle, asset::MaterialAssetHandle>)
        {
            return materials_;
        }
        else
        {
            static_assert(!std::is_same_v<Handle, Handle>, "unsupported render resource handle");
        }
    }
    template <typename Handle> const auto &entries(Handle handle) const
    {
        return const_cast<RenderAssetStore *>(this)->entries(handle);
    }
    template <typename Handle, typename Resource>
    void publishPrepared(RetirementBatch &, const ResourcePreparationTarget &,
                         std::shared_ptr<const Resource>);
    std::vector<PreparedEntry<PreparedShader>> shaders_;
    std::vector<PreparedEntry<PreparedShaderProgram>> programs_;
    std::vector<PreparedEntry<PreparedMaterialTemplate>> templates_;
    asset::AssetDomainId domain_;
    uint64_t nextPublication_ = 1; // Does not reset: GUI tokens must notice cache reuse.
    std::vector<TextureEntry> textures_;
    std::vector<MaterialEntry> materials_;
    std::vector<MeshEntry> meshes_;
};

} // namespace rubia::render
