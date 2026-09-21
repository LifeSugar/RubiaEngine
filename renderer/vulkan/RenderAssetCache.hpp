#pragma once

#include "asset/AssetManager.hpp"
#include "vulkan/DescriptorPool.hpp"
#include "vulkan/GpuMaterial.hpp"
#include "vulkan/GpuPreparedAssets.hpp"
#include "vulkan/GpuTexture.hpp"
#include "vulkan/IResourcePreparation.hpp"
#include "vulkan/Mesh.hpp"
#include <type_traits>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Resident GPU representations. Publication hands replaced owners to the caller;
/// frame slots and GPU-safe retirement belong to Renderer, not this cache.
class RenderAssetCache final
{
public:
    RenderAssetCache() = default;
    ~RenderAssetCache();

    RenderAssetCache(const RenderAssetCache&) = delete;
    RenderAssetCache& operator=(const RenderAssetCache&) = delete;
    RenderAssetCache(RenderAssetCache&&) = delete;
    RenderAssetCache& operator=(RenderAssetCache&&) = delete;

    void publishTexture(asset::TextureAssetHandle handle, GpuTexture texture);
    void publishMesh(asset::MeshAssetHandle handle, Mesh mesh);
    bool empty() const noexcept;
    struct PreparationVersions
    {
        asset::AssetContentRevision resident = 0;
        asset::AssetContentRevision requested = 0;
        std::vector<asset::AssetDependencyVersion> residentDependencies;
        std::vector<asset::AssetDependencyVersion> requestedDependencies;
    };
    // One asset domain per cache until reset. A recycled generation requires cache reset.
    PreparationVersions inspectPreparation(const ResourcePreparationTarget& target) const;
    void acceptPreparation(const ResourcePreparationTarget& target);
    bool dependenciesCurrent(const ResourcePreparationTarget& target) const;
    void publishPreparedTexture(const Device& device, RetiredResources& retired,
                                const ResourcePreparationTarget& target,
                                GpuTexture texture);
    void publishPreparedMesh(RetiredResources& retired, const ResourcePreparationTarget& target,
                             Mesh mesh);
    // Refresh dependency readiness without allocating/uploading unchanged geometry.
    void publishPreparedMeshDependencies(const ResourcePreparationTarget& target);
    void publishPreparedShader(RetiredResources&, const ResourcePreparationTarget&,
                               std::shared_ptr<const GpuShader>);
    void publishPreparedShaderProgram(RetiredResources&, const ResourcePreparationTarget&,
                                      std::shared_ptr<const GpuShaderProgram>);
    void publishPreparedMaterialTemplate(RetiredResources&, const ResourcePreparationTarget&,
                                         std::shared_ptr<const GpuMaterialTemplate>);

    void publishPreparedMaterial(RetiredResources&, const ResourcePreparationTarget&, GpuMaterial,
                                 DescriptorPool, std::shared_ptr<const GpuMaterialTemplate>,
                                 std::vector<asset::TextureAssetHandle>);
    std::shared_ptr<const GpuShader> shader(asset::ShaderAssetHandle) const;
    std::shared_ptr<const GpuShaderProgram> shaderProgram(asset::ShaderProgramAssetHandle) const;
    std::shared_ptr<const GpuMaterialTemplate> materialTemplate(
        asset::MaterialTemplateAssetHandle) const;
    // Legacy CPU/GPU transaction: stamp only after committing matching content.
    void setTextureVersion(asset::AssetDomainId domain,
                           asset::AssetVersion<asset::TextureAsset> version);
    uint64_t texturePublication(asset::TextureAssetHandle handle) const noexcept;

    /// Legacy synchronous transaction: creates replacement material bindings and
    /// returns the previous texture for rollback. Caller must first finish all
    /// GPU users, since the old bindings are reclaimed before this call returns.
    [[nodiscard]] GpuTexture commitTextureReplacement(
        const Device& device,
        const asset::AssetManager& assets,
        asset::TextureAssetHandle handle,
        GpuTexture replacement);
    void reset() noexcept;

    [[nodiscard]] const Mesh& mesh(asset::MeshAssetHandle handle) const;
    [[nodiscard]] const Mesh* tryMesh(
        asset::MeshAssetHandle handle) const noexcept;
    [[nodiscard]] const GpuMaterial& material(
        asset::MaterialAssetHandle handle) const;
    [[nodiscard]] const GpuMaterial* tryMaterial(
        asset::MaterialAssetHandle handle) const noexcept;
    /// Returns an uploaded texture for Editor previews and material binding.
    [[nodiscard]] const GpuTexture& texture(
        asset::TextureAssetHandle handle) const;
    [[nodiscard]] const GpuTexture* tryTexture(
        asset::TextureAssetHandle handle) const noexcept;
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
        GpuTexture texture;
    };

    // Retire as one unit: the descriptor pool must outlive submitted descriptor
    // uses, and its parameter buffer and prepared template must remain alive too.
    struct MaterialResources
    {
        GpuMaterial material;
        std::vector<asset::TextureAssetHandle> textures;
        asset::MaterialTemplateAsset layout;
        std::shared_ptr<const GpuMaterialTemplate> preparedTemplate;
        DescriptorPool pool;
    };

    struct MaterialEntry : VersionedEntry
    {
        MaterialResources resources;
    };

    struct MeshEntry : VersionedEntry
    {
        Mesh mesh;
    };

    template <typename Resource> struct PreparedEntry : VersionedEntry
    {
        std::shared_ptr<const Resource> resource;
    };
    template <typename Handle> auto& entries(Handle)
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
    template <typename Handle> const auto& entries(Handle handle) const
    {
        return const_cast<RenderAssetCache*>(this)->entries(handle);
    }
    template <typename Handle, typename Resource>
    void publishPrepared(RetiredResources&, const ResourcePreparationTarget&,
                         std::shared_ptr<const Resource>);
    std::vector<PreparedEntry<GpuShader>> shaders_;
    std::vector<PreparedEntry<GpuShaderProgram>> programs_;
    std::vector<PreparedEntry<GpuMaterialTemplate>> templates_;
    // Builds all new descriptors first, then exchanges whole binding versions.
    // The returned old texture is owned by retired until moved by the legacy path.
    GpuTexture& replaceTexture(const Device& device, RetiredResources& retired,
                               asset::TextureAssetHandle handle, GpuTexture replacement,
                               asset::AssetContentRevision revision);
    asset::AssetDomainId domain_;
    uint64_t nextPublication_ = 1; // Does not reset: GUI tokens must notice cache reuse.
    std::vector<TextureEntry> textures_;
    std::vector<MaterialEntry> materials_;
    std::vector<MeshEntry> meshes_;

};

} // namespace rubia::rhi::vulkan
